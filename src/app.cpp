#include "app.h"

#include "format.h"
#include "icons.h"
#include "logic.h"
#include "trayprobe.h"
#include "version.h"

#include <objbase.h>
#include <shellapi.h>
#include <wtsapi32.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <ctime>
#include <cwchar>
#include <thread>

namespace {

constexpr wchar_t kBarClass[] = L"SysBar.Bar";
constexpr wchar_t kCardClass[] = L"SysBar.Card";

constexpr UINT_PTR kHousekeepingTimer = 1;
constexpr UINT_PTR kAnimationTimer = 2;
constexpr UINT_PTR kCardDelayTimer = 3;
constexpr UINT_PTR kCardFadeTimer = 4;
constexpr UINT_PTR kUpdateTimer = 5;
constexpr UINT_PTR kNoticeTimer = 6;
constexpr UINT kFirstUpdateCheckMs = 2 * 60 * 1000;  // shortly after start, not during logon
constexpr UINT kUpdateTimerMs = 6 * 60 * 60 * 1000;  // re-evaluate every 6 hours...
constexpr int64_t kUpdateIntervalS = 24 * 60 * 60;   // ...but check at most once a day
constexpr UINT kNoticeMs = 5000;
constexpr UINT kHousekeepingMs = 2000;
constexpr ULONG kHousekeepingToleranceMs = 500;  // lets Windows coalesce our wake-ups
constexpr UINT kFrameMs = 15;

constexpr float kAnimationEase = 0.28f;
constexpr float kAnimationMinDelta = 0.02f;  // smaller changes jump straight to the target
constexpr float kAnimationSnap = 0.004f;
constexpr UINT kCardDelayMs = 350;
constexpr float kCardFadeInStep = 0.16f;  // ~100 ms
constexpr float kCardFadeOutStep = 0.25f;
constexpr int kCardSlideDip = 6;
constexpr int kCardGapDip = 10;

constexpr int kModernGapDip = 4;                       // space between the strip and the Windows 11 tray
constexpr int kModernFallbackRightDip = 380;           // used only when the tray area cannot be located
constexpr int kModernSecondaryFallbackRightDip = 160;  // other monitors: clock and notifications only
constexpr ULONGLONG kTrayProbeIntervalMs = 60'000;     // the right-side controls are measured again...
constexpr ULONGLONG kTrayProbeRetryMs = 10'000;        // ...or sooner when it could not be measured
constexpr size_t kMaxTaskListMoves = 12;               // tug-of-war guard against Explorer...
constexpr ULONGLONG kTaskListMoveWindowMs = 2000;      // ...per time window
constexpr DWORD kWindows11Build = 22000;
constexpr DWORD kDarkMenusBuild = 18362;  // Windows 10 1903

enum Command : UINT {
    kCmdShowCpu = 100,
    kCmdShowRam,
    kCmdShowGpu,
    kCmdShowVram,
    kCmdShowDisk,
    kCmdShowTemperatures,
    kCmdColorByLoad,
    kCmdClickOpensTaskManager,
    kCmdAutostart,
    kCmdTaskManager,
    kCmdAbout,
    kCmdExit,
    kCmdIntervalCustom,
    kCmdCheckUpdates,
    kCmdAutoUpdate,
    kCmdInstallUpdate,
    kCmdAdvancedSensors,
    kCmdDisksEach,
    kCmdDisksCombined,
    kCmdTemperatureAuto,
    kCmdTemperatureC,
    kCmdTemperatureF,
    kCmdShowNet,
    kCmdShowBattery,
    kCmdAllTaskbars,
    kCmdInterval0 = 200,  // + index into kIntervals
    kCmdLanguageAuto = 300,
    kCmdLanguage0 = 301,  // + i18n::Lang
    kCmdFan0 = 500,       // + fan index * kFanCommandStride + FanRole (0 = automatic)
};
constexpr UINT kFanCommandStride = 8;
constexpr size_t kMaxMenuFans = 24;

constexpr DWORD kIntervals[] = {500, 1000, 2000, 5000};
constexpr int kIntervalCount = static_cast<int>(std::size(kIntervals));

// The WinEvent callback carries no context pointer, hence this single global.
App* g_app = nullptr;

// What the crash handler needs to give the taskbar its space back (no allocations involved).
struct CrashRestore {
    HWND taskList = nullptr;
    RECT rect{};
    bool active = false;
} g_crashRestore[8];

void Log(const wchar_t* format, ...) {
    wchar_t buffer[512];
    int n = swprintf_s(buffer, L"[sys-bar] ");
    va_list args;
    va_start(args, format);
    // Truncate instead of invoking the invalid-parameter handler (which would end the process) on long
    // texts; one character is kept for the newline.
    _vsnwprintf_s(buffer + n, std::size(buffer) - n - 1, _TRUNCATE, format, args);
    va_end(args);
    wcscat_s(buffer, L"\n");
    OutputDebugStringW(buffer);

    // SYSBAR_LOG=<file> also appends to a file (handy for bug reports).
    static const std::wstring path = [] {
        wchar_t value[MAX_PATH] = {};
        const DWORD n = GetEnvironmentVariableW(L"SYSBAR_LOG", value, MAX_PATH);
        return n > 0 && n < MAX_PATH ? std::wstring(value) : std::wstring();
    }();
    if (path.empty()) return;
    const HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME now;
    GetLocalTime(&now);
    char line[700];
    const int prefix = sprintf_s(line, "%02u:%02u:%02u.%03u ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    const int body = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, line + prefix, static_cast<int>(sizeof(line)) - prefix,
                                         nullptr, nullptr);
    DWORD written = 0;
    if (body > 1) WriteFile(file, line, static_cast<DWORD>(prefix + body - 1), &written, nullptr);
    CloseHandle(file);
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS*) {
    // Explorer would keep the task lists shrunk until its next layout pass: restore them right away.
    for (const CrashRestore& restore : g_crashRestore) {
        if (!restore.active || !IsWindow(restore.taskList)) continue;
        const RECT& r = restore.rect;
        SetWindowPos(restore.taskList, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WindowsBuild() {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW info{sizeof(info)};
    if (const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) {
            if (fn(&info) == 0) return info.dwBuildNumber;
        }
    }
    return 0;
}

// Dark Win32 menus. Undocumented uxtheme ordinals, stable since Windows 10 1903 and used by Explorer
// itself; resolved dynamically and skipped on older builds.
void ApplyMenuTheme(bool light) {
    if (WindowsBuild() < kDarkMenusBuild) return;
    static const HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme) return;
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    static const auto setMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    static const auto flush = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
    if (setMode) setMode(light ? 3 /*ForceLight*/ : 2 /*ForceDark*/);
    if (flush) flush();
}

bool TaskbarUsesLightTheme() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    // Missing value = dark taskbar (the default).
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS &&
           value != 0;
}

bool HighContrastOn() {
    HIGHCONTRASTW hc{sizeof(hc)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
}

wchar_t UserDecimalSeparator() {
    wchar_t buffer[8] = {};
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL, buffer, 8) > 0 && buffer[0]) return buffer[0];
    return L'.';
}

float EaseOutCubic(float t) {
    const float u = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - u * u * u;
}

RECT ToParent(HWND parent, HWND child) {
    RECT r{};
    GetWindowRect(child, &r);
    MapWindowPoints(HWND_DESKTOP, parent, reinterpret_cast<POINT*>(&r), 2);
    return r;
}

bool SameRect(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

// Shell calls can take hundreds of milliseconds and need COM; they run off the UI thread so the
// taskbar (which shares our input queue) never stalls.
void ShellOpen(std::wstring target) {
    std::thread([target = std::move(target)] {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        SHELLEXECUTEINFOW info{sizeof(info)};
        info.fMask = SEE_MASK_NOASYNC;
        info.lpVerb = L"open";
        info.lpFile = target.c_str();
        info.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&info)) Log(L"ShellExecuteEx(%s) failed: %lu", target.c_str(), GetLastError());
        if (SUCCEEDED(hr)) CoUninitialize();
    }).detach();
}

}  // namespace

int App::Run(HINSTANCE instance) {
    instance_ = instance;
    g_app = this;
    SetUnhandledExceptionFilter(OnCrash);
    decimal_ = UserDecimalSeparator();
    settings_.Load();
    ApplyLanguage();

    if (!gfx_.Init()) {
        Log(L"Direct2D/DirectWrite initialization failed");
        return 1;
    }

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = ControllerProc;
    wc.hInstance = instance;
    wc.lpszClassName = kControllerClass;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);

    WNDCLASSEXW bc{sizeof(bc)};
    bc.lpfnWndProc = BarProc;
    bc.hInstance = instance;
    bc.lpszClassName = kBarClass;
    bc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&bc);

    WNDCLASSEXW cc{sizeof(cc)};
    cc.lpfnWndProc = DefWindowProcW;
    cc.hInstance = instance;
    cc.lpszClassName = kCardClass;
    RegisterClassExW(&cc);

    // Top-level window that is never shown: receives TaskbarCreated, WM_SETTINGCHANGE, power events...
    controller_ = CreateWindowExW(WS_EX_TOOLWINDOW, kControllerClass, kControllerTitle, WS_POPUP, 0, 0, 0, 0, nullptr,
                                  nullptr, instance, this);
    if (!controller_) return 1;

    // Hover card: never activated, transparent to the mouse, always on top.
    cardWindow_ =
        CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                        kCardClass, kControllerTitle, WS_POPUP, 0, 0, 1, 1, controller_, nullptr, instance, nullptr);

    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(controller_, taskbarCreated_, MSGFLT_ALLOW, nullptr);
    displayNotify_ =
        RegisterPowerSettingNotification(controller_, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);
    WTSRegisterSessionNotification(controller_, NOTIFY_FOR_THIS_SESSION);

    RefreshTheme(true);
    if (!sampler_.Start(controller_, WM_APP_SAMPLE, settings_.intervalMs)) Log(L"sampler failed to start");
    SetCoalescableTimer(controller_, kHousekeepingTimer, kHousekeepingMs, nullptr, kHousekeepingToleranceMs);
    updater_.Init(controller_, WM_APP_UPDATE, SYSBAR_VERSION_WTEXT, SYSBAR_RELEASES_API);
    SetTimer(controller_, kUpdateTimer, kFirstUpdateCheckMs, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    sampler_.Stop();
    DetachAll(true);
    g_app = nullptr;
    return static_cast<int>(msg.wParam);
}

// ---------------------------------------------------------------------------------------------------
// Taskbar integration
// ---------------------------------------------------------------------------------------------------

namespace {

// Windows 11 builds a taskbar without HWNDs for its controls. SYSBAR_FORCE_MODERN=1 takes the modern
// path on any build when the classic structure is absent (used by the multi-monitor integration test).
bool ModernTaskbars() {
    static const bool forced = [] {
        wchar_t value[4] = {};
        return GetEnvironmentVariableW(L"SYSBAR_FORCE_MODERN", value, 4) == 1 && value[0] == L'1';
    }();
    return forced || WindowsBuild() >= kWindows11Build;
}

}  // namespace

App::Taskbar* App::Attach(HWND tray, bool primary) {
    auto t = std::make_unique<Taskbar>();
    t->tray = tray;
    t->primary = primary;
    // Primary: Shell_TrayWnd > ReBarWindow32 > MSTaskSwWClass, plus the notification area.
    // Other monitors: Shell_SecondaryTrayWnd > WorkerW > MSTaskListWClass (the clock has no HWND we need).
    if (primary) {
        t->notify = FindWindowExW(tray, nullptr, L"TrayNotifyWnd", nullptr);
        t->container = FindWindowExW(tray, nullptr, L"ReBarWindow32", nullptr);
        t->taskList = t->container ? FindWindowExW(t->container, nullptr, L"MSTaskSwWClass", nullptr) : nullptr;
    } else {
        t->container = FindWindowExW(tray, nullptr, L"WorkerW", nullptr);
        t->taskList = t->container ? FindWindowExW(t->container, nullptr, L"MSTaskListWClass", nullptr) : nullptr;
    }

    // Decide by structure, not by version: Windows 11 with a restored classic taskbar (ExplorerPatcher,
    // StartAllBack...) still has a visible task list inside its container and needs the classic path.
    RECT listRect{};
    const bool classicVisible = t->taskList && IsWindowVisible(t->container) && IsWindowVisible(t->taskList) &&
                                GetWindowRect(t->taskList, &listRect) && listRect.right > listRect.left &&
                                listRect.bottom > listRect.top;
    if (classicVisible) {
        t->kind = TaskbarKind::Classic;
        t->parent = t->container;
        t->natural = ToParent(t->container, t->taskList);
    } else if (ModernTaskbars()) {
        t->kind = TaskbarKind::Modern;
        t->parent = tray;
        t->container = t->taskList = nullptr;
    } else {
        return nullptr;  // not ready yet (Explorer still building it): tried again on the next pass
    }

    t->renderer = std::make_unique<Renderer>(gfx_);
    t->renderer->SetTheme(theme_);
    // WS_EX_NOPARENTNOTIFY: no synchronous WM_PARENTNOTIFY round-trips to Explorer on every click.
    t->bar = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOPARENTNOTIFY, kBarClass, kControllerTitle,
                             WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1, t->parent, nullptr, instance_, this);
    if (!t->bar) {
        Log(L"CreateWindowEx(strip) failed: %lu", GetLastError());
        return nullptr;
    }
    if (!accessibleName_.empty()) SetWindowTextW(t->bar, accessibleName_.c_str());

    // Layout changes of the taskbar windows are watched on their own thread (out of context).
    DWORD pid = 0;
    const DWORD thread = GetWindowThreadProcessId(tray, &pid);
    if (thread && !hooks_.count(thread)) {
        if (HWINEVENTHOOK hook =
                SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, OnWinEvent, pid,
                                thread, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS)) {
            hooks_[thread] = hook;
        }
    }
    Log(L"attached to the %s taskbar (%s)", primary ? L"main" : L"secondary",
        t->kind == TaskbarKind::Classic ? L"classic" : L"modern");
    bars_.push_back(std::move(t));
    return bars_.back().get();
}

void App::RestoreTaskList(Taskbar& t) {
    if (t.kind != TaskbarKind::Classic || !t.shrunk || !IsWindow(t.container) || !IsWindow(t.taskList)) return;
    // Recompute the natural rectangle from the current layout (toolbars respected).
    logic::ClassicInput in;
    GetClientRect(t.container, &in.rebarClient);
    in.taskList = ToParent(t.container, t.taskList);
    in.vertical = t.vertical;
    for (HWND child = GetWindow(t.container, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (child != t.taskList && child != t.bar && IsWindowVisible(child))
            in.toolbars.push_back(ToParent(t.container, child));
    }
    const RECT natural = logic::PlaceClassic(in).natural;
    SetWindowPos(t.taskList, nullptr, natural.left, natural.top, natural.right - natural.left,
                 natural.bottom - natural.top, SWP_NOZORDER | SWP_NOACTIVATE);
    t.shrunk = false;
}

void App::Detach(Taskbar& t, bool restoreTaskbar) {
    if (restoreTaskbar) RestoreTaskList(t);
    if (t.bar && t.bar == hoverBar_) {
        hoverBar_ = nullptr;
        HideCard();
    }
    if (t.bar && t.bar == pressedBar_) pressedBar_ = nullptr;
    if (t.bar && t.bar == noticeBar_) noticeBar_ = nullptr;
    if (t.bar && IsWindow(t.bar)) DestroyWindow(t.bar);
    t.bar = nullptr;
}

void App::DetachAll(bool restoreTaskbar) {
    // Detaching calls into Explorer (task list, window destruction), which may deliver messages to us:
    // take the strips out of bars_ first, so a nested call never sees half-destroyed entries.
    inTaskbarWork_ = true;
    std::vector<std::unique_ptr<Taskbar>> bars = std::move(bars_);
    bars_.clear();
    for (auto& t : bars) Detach(*t, restoreTaskbar);
    bars.clear();
    inTaskbarWork_ = false;
    for (auto& [thread, hook] : hooks_) UnhookWinEvent(hook);
    hooks_.clear();
    UpdateCrashRestore();
    if (cardWindow_) {
        KillTimer(controller_, kCardDelayTimer);
        KillTimer(controller_, kCardFadeTimer);
        ShowWindow(cardWindow_, SW_HIDE);
        cardVisible_ = cardWanted_ = false;
        cardAlpha_ = 0.0f;
    }
    hoverBar_ = pressedBar_ = noticeBar_ = nullptr;
    accessibleName_.clear();
}

bool App::IsAttached(const Taskbar& t) const {
    if (!t.bar || !IsWindow(t.bar) || !IsWindow(t.tray) || !IsWindow(t.parent)) return false;
    if (t.kind == TaskbarKind::Classic && !IsWindow(t.taskList)) return false;
    // After an Explorer restart a new Shell_TrayWnd exists while the old handle may linger briefly.
    return !t.primary || FindWindowW(L"Shell_TrayWnd", nullptr) == t.tray;
}

App::Taskbar* App::FindBar(HWND bar) {
    if (!bar) return nullptr;  // destroyed strips keep a null handle until the next pass
    for (auto& t : bars_) {
        if (t->bar == bar) return t.get();
    }
    return nullptr;
}

App::Taskbar* App::CardBar() {
    if (Taskbar* t = FindBar(hoverBar_)) return t;
    if (Taskbar* t = FindBar(noticeBar_)) return t;
    for (auto& t : bars_) {
        if (t->primary && t->bar) return t.get();
    }
    return nullptr;
}

void App::SyncTaskbars() {
    // The taskbars that should carry a strip: the main one, and those on other monitors.
    std::vector<std::pair<HWND, bool>> wanted;
    if (HWND primary = FindWindowW(L"Shell_TrayWnd", nullptr)) wanted.emplace_back(primary, true);
    if (settings_.allTaskbars) {
        for (HWND w = FindWindowExW(nullptr, nullptr, L"Shell_SecondaryTrayWnd", nullptr);
             w && wanted.size() < kMaxTaskbars; w = FindWindowExW(nullptr, w, L"Shell_SecondaryTrayWnd", nullptr)) {
            if (IsWindowVisible(w)) wanted.emplace_back(w, false);
        }
    }
    auto isWanted = [&](HWND tray) {
        return std::any_of(wanted.begin(), wanted.end(), [&](const auto& w) { return w.first == tray; });
    };

    // Strips whose taskbar went away (monitor unplugged, Explorer restarted, option turned off). They leave
    // bars_ before being detached: detaching calls into Explorer, which may deliver messages to us.
    std::vector<std::unique_ptr<Taskbar>> gone;
    for (auto it = bars_.begin(); it != bars_.end();) {
        if (IsAttached(**it) && isWanted((*it)->tray)) {
            ++it;
        } else {
            gone.push_back(std::move(*it));
            it = bars_.erase(it);
        }
    }
    for (auto& t : gone) {
        // Give the task list its space back only when it still exists (not after an Explorer crash).
        Detach(*t, IsWindow(t->container) && IsWindow(t->taskList));
    }
    gone.clear();
    // Unhook threads that no longer own any taskbar (Explorer restarted).
    for (auto it = hooks_.begin(); it != hooks_.end();) {
        const bool used = std::any_of(bars_.begin(), bars_.end(), [&](const auto& t) {
            return GetWindowThreadProcessId(t->tray, nullptr) == it->first;
        });
        if (used) {
            ++it;
        } else {
            UnhookWinEvent(it->second);
            it = hooks_.erase(it);
        }
    }
    for (const auto& [tray, primary] : wanted) {
        const bool known = std::any_of(bars_.begin(), bars_.end(), [&](const auto& t) { return t->tray == tray; });
        if (!known) Attach(tray, primary);
    }
}

void App::UpdateCrashRestore() {
    // Fixed storage: the crash handler must not allocate.
    size_t n = 0;
    for (const auto& t : bars_) {
        if (n >= std::size(g_crashRestore)) break;
        if (t->kind == TaskbarKind::Classic && t->shrunk) g_crashRestore[n++] = {t->taskList, t->natural, true};
    }
    for (; n < std::size(g_crashRestore); ++n) g_crashRestore[n].active = false;
}

void App::RequestReposition() {
    if (repositionPending_ || !controller_) return;
    repositionPending_ = true;
    PostMessageW(controller_, WM_APP_REPOSITION, 0, 0);
}

void App::Reposition() {
    repositionPending_ = false;
    if (quitting_ || !snapshot_.valid || displayOff_ || locked_) return;
    // Moving Explorer's windows is a synchronous cross-thread call: messages sent to us meanwhile (an
    // Explorer restart, a setting change) must not rebuild bars_ under our feet. Run once more later.
    if (inTaskbarWork_) {
        RequestReposition();
        return;
    }
    inTaskbarWork_ = true;
    SyncTaskbars();
    RebuildItems();
    bool cardBarMoved = false;
    for (auto& t : bars_) {
        if (t->bar && RepositionBar(*t) && t->bar == hoverBar_) cardBarMoved = true;
    }
    UpdateCrashRestore();
    inTaskbarWork_ = false;
    if (cardBarMoved && cardVisible_) UpdateCard(true);  // the card follows the strip
}

bool App::RepositionBar(Taskbar& t) {
    const UINT dpi = std::max<UINT>(GetDpiForWindow(t.tray), USER_DEFAULT_SCREEN_DPI);
    RECT trayRect{};
    GetWindowRect(t.tray, &trayRect);
    t.vertical = t.kind == TaskbarKind::Classic && (trayRect.bottom - trayRect.top) > (trayRect.right - trayRect.left);

    RECT target{};
    if (t.kind == TaskbarKind::Classic) {
        logic::ClassicInput in;
        GetClientRect(t.container, &in.rebarClient);
        in.taskList = ToParent(t.container, t.taskList);
        in.vertical = t.vertical;
        for (HWND child = GetWindow(t.container, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            if (child != t.taskList && child != t.bar && IsWindowVisible(child)) {
                in.toolbars.push_back(ToParent(t.container, child));
            }
        }
        // The strip is as thick as the task list row (handles multi-row taskbars too).
        t.layout = {dpi, t.vertical,
                    t.vertical ? in.taskList.right - in.taskList.left : in.taskList.bottom - in.taskList.top};
        if (t.layout.thicknessPx <= 0) return false;
        in.strip = t.renderer->Layout(t.layout, items_);
        const logic::ClassicPlacement placed = logic::PlaceClassic(in);
        t.natural = placed.natural;

        if (!SameRect(in.taskList, placed.taskList)) {
            const ULONGLONG now = GetTickCount64();
            while (!t.moves.empty() && now - t.moves.front() > kTaskListMoveWindowMs) t.moves.pop_front();
            if (t.moves.size() < kMaxTaskListMoves) {
                t.moves.push_back(now);
                const RECT& r = placed.taskList;
                SetWindowPos(t.taskList, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                t.shrunk = true;
            } else {
                Log(L"task list keeps being reset by Explorer; backing off");
            }
        } else {
            // Already at our size: maybe left that way by a previous instance that was killed. It is ours
            // to give back on exit all the same.
            t.shrunk = !SameRect(placed.taskList, placed.natural);
        }
        target = placed.strip;
    } else {
        logic::ModernInput in;
        GetClientRect(t.tray, &in.trayClient);
        if (t.primary) {
            if (!IsWindow(t.notify)) t.notify = FindWindowExW(t.tray, nullptr, L"TrayNotifyWnd", nullptr);
            if (t.notify && IsWindowVisible(t.notify)) in.notify = ToParent(t.tray, t.notify);
        }
        // Without a usable notification-area window (other monitors, newer Windows 11 builds), the
        // taskbar's own right-side controls (clock, tray icons) are measured through UI Automation.
        if (!logic::IsPlausibleNotifyRect(in.trayClient, in.notify)) {
            ProbeTrayEdge(t);
            if (t.reservePx > 0) {
                in.notify = in.trayClient;
                in.notify.left = std::max(in.trayClient.left, in.trayClient.right - t.reservePx);
            }
        }
        t.layout = {dpi, false, in.trayClient.bottom - in.trayClient.top};
        if (t.layout.thicknessPx <= 0) return false;
        in.strip = t.renderer->Layout(t.layout, items_);
        in.gapPx = MulDiv(kModernGapDip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
        in.offsetPx = MulDiv(settings_.offsetDip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
        in.fallbackRightPx = MulDiv(t.primary ? kModernFallbackRightDip : kModernSecondaryFallbackRightDip,
                                    static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
        target = logic::PlaceModern(in);
    }

    const RECT current = ToParent(t.parent, t.bar);
    const bool moved = !SameRect(current, target);
    if (moved) {
        SetWindowPos(t.bar, HWND_TOP, target.left, target.top, target.right - target.left, target.bottom - target.top,
                     SWP_NOACTIVATE);
    } else if (GetWindow(t.bar, GW_HWNDPREV)) {
        // Explorer may re-order its children (e.g. the Windows 11 XAML bridge): stay on top.
        SetWindowPos(t.bar, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    RenderBar(t, moved);
    if (!t.shown) {
        ShowWindow(t.bar, SW_SHOWNA);
        t.shown = true;
    }
    return moved;
}

void App::ProbeTrayEdge(Taskbar& t) {
    // UI Automation talks to Explorer across processes: never on the UI thread, which shares its input
    // queue with the taskbar. Measured again when the taskbar changes size, and once a minute.
    RECT tray{};
    GetWindowRect(t.tray, &tray);
    const int width = tray.right - tray.left;
    const ULONGLONG now = GetTickCount64();
    if (t.probing) return;  // never two measurements of one taskbar at once (the probe has its own timeouts)
    // Measured again when the taskbar changes size and once a minute; after a failure, every 10 s.
    const ULONGLONG interval = t.reservePx >= 0 ? kTrayProbeIntervalMs : kTrayProbeRetryMs;
    if (t.probeStarted && width == t.probedWidth && now - t.probeStarted < interval) return;
    t.probing = true;
    t.probeStarted = now;
    t.probedWidth = width;
    std::thread([tray = t.tray, controller = controller_] {
        const int reserve = trayprobe::MeasureRightControls(tray);
        PostMessageW(controller, WM_APP_TRAYEDGE, reinterpret_cast<WPARAM>(tray), static_cast<LPARAM>(reserve));
    }).detach();
}

// ---------------------------------------------------------------------------------------------------
// Data and drawing
// ---------------------------------------------------------------------------------------------------

void App::RebuildItems() {
    items_.clear();
    const auto& s = snapshot_;
    const std::wstring gb = S(i18n::Str::UnitGB);

    const bool fahrenheit = settings_.Fahrenheit();
    auto percent = [&](MetricId id, int key, const std::wstring& label, double value) {
        ItemView item;
        item.id = id;
        item.label = label;
        item.strong = text::Percent(value);
        item.weak = L"%";
        item.worstStrong = L"100";
        item.worstWeak = L"%";
        item.load = static_cast<float>(value / 100.0);
        item.fraction = tracks_[key].shown;
        items_.push_back(std::move(item));
    };
    auto memory = [&](MetricId id, const wchar_t* label, uint64_t used, uint64_t total) {
        ItemView item;
        item.id = id;
        item.label = label;
        const std::wstring totalText = text::Fixed1(text::GiB(total), decimal_);
        item.strong = total ? text::Fixed1(text::GiB(used), decimal_) : L"—";
        item.weak = total ? L" / " + totalText + L" " + gb : L"";
        item.worstStrong = totalText;
        item.worstWeak = L" / " + totalText + L" " + gb;
        item.load = text::Ratio(used, total);
        item.fraction = tracks_[Key(id)].shown;
        items_.push_back(std::move(item));
    };

    // Temperature badge on the label line ("54°"), colored when warm/hot.
    auto badge = [&](MetricId id, int key, const std::optional<double>& celsius) {
        if (!settings_.showTemperatures || items_.empty()) return;
        // Once a metric has shown a temperature, its column keeps the room for it: the strip must not
        // grow and shrink whenever a reading is momentarily missing (e.g. an idle drive).
        bool& seen = tracks_[key].badgeSeen;
        seen = seen || celsius.has_value();
        if (!seen) return;
        ItemView& item = items_.back();
        item.worstBadge = text::TemperatureBadge(100.0, fahrenheit);  // "100°" / "212°F"
        if (!celsius) return;
        item.badge = text::TemperatureBadge(*celsius, fahrenheit);
        item.badgeLevel = settings_.colorByLoad ? logic::TemperatureLevel(id, *celsius) : 0;
    };

    for (MetricId id : logic::VisibleMetrics(settings_, Available())) {
        switch (id) {
            case MetricId::Cpu:
                percent(id, Key(id), L"CPU", s.cpu);
                badge(id, Key(id), s.cpuTemperature);
                break;
            case MetricId::Ram: memory(id, L"RAM", s.ramUsed, s.ramTotal); break;
            case MetricId::Gpu:
                percent(id, Key(id), L"GPU", s.gpu);
                badge(id, Key(id), s.gpuTemperature);
                break;
            case MetricId::Vram: memory(id, L"VRAM", s.vramUsed, s.vramTotal); break;
            case MetricId::Disk: {
                if (logic::SeparateDisks(settings_, s.disks.size())) {
                    for (const DiskSnapshot& disk : s.disks) {
                        const int key = kDiskKeyBase + disk.number;
                        percent(id, key,
                                logic::DiskStripLabel(disk.kind, disk.letters, disk.number, S(i18n::Str::StripDisk)),
                                disk.active);
                        badge(id, key, disk.temperature);
                    }
                    break;
                }
                percent(id, Key(id), S(i18n::Str::StripDisk), s.diskActive);
                std::optional<double> hottest;
                for (const DiskSnapshot& disk : s.disks) {
                    if (disk.temperature && (!hottest || *disk.temperature > *hottest)) hottest = disk.temperature;
                }
                badge(id, Key(id), hottest);
                break;
            }
            case MetricId::Net: {
                // Download as the value, upload on the label line; the bar is the link utilization.
                const std::wstring widest = S(i18n::Str::UnitMBps);
                const text::RateText down = text::Rate(s.network.downBps, decimal_);
                const text::RateText up = text::Rate(s.network.upBps, decimal_);
                ItemView item;
                item.id = id;
                item.label = S(i18n::Str::StripNet);
                item.strong = L"\u2193 " + down.number;
                item.weak = std::wstring(L" ") + RateUnit(down.unit);
                item.worstStrong = L"\u2193 999";
                item.worstWeak = L" " + widest;
                item.badge = L"\u2191 " + up.number + L" " + RateUnit(up.unit);
                item.worstBadge = L"\u2191 999 " + widest;
                item.fraction = tracks_[Key(id)].shown;
                items_.push_back(std::move(item));
                break;
            }
            case MetricId::Battery: {
                const hw::BatteryReading& b = s.battery;
                ItemView item;
                item.id = id;
                item.label = S(i18n::Str::StripBattery);
                item.strong = text::Percent(b.percent);
                item.weak = L"%";
                item.worstStrong = L"100";
                item.worstWeak = L"%";
                // Low charge on battery is what deserves attention (amber at 20%, red at 10%).
                const bool draining = !b.acOnline;
                item.load = draining && b.percent <= 10.0   ? kCriticalLoad
                            : draining && b.percent <= 20.0 ? kWarnLoad
                                                            : 0.0f;
                item.fraction = tracks_[Key(id)].shown;
                bool& seen = tracks_[Key(id)].badgeSeen;  // power in watts, when the driver reports it
                seen = seen || b.watts.has_value();
                if (seen) {
                    item.worstBadge = L"\u2212999 W";
                    if (b.watts) item.badge = text::Watts(*b.watts, decimal_);
                }
                items_.push_back(std::move(item));
                break;
            }
        }
    }
}

const wchar_t* App::RateUnit(int unit) const {
    return S(unit == 0 ? i18n::Str::UnitKBps : unit == 1 ? i18n::Str::UnitMBps : i18n::Str::UnitGBps);
}

std::wstring App::RateText(uint64_t bytesPerSecond) const {
    const text::RateText rate = text::Rate(bytesPerSecond, decimal_);
    return rate.number + L" " + RateUnit(rate.unit);
}

std::wstring App::FanRoleName(FanRole role, const std::wstring& name) const {
    switch (role) {
        case FanRole::Cpu: return L"CPU";
        case FanRole::Gpu: return L"GPU";
        case FanRole::Case: return S(i18n::Str::CardFanCase);
        case FanRole::Pump: return S(i18n::Str::CardFanPump);
        case FanRole::Psu: return S(i18n::Str::CardFanPsu);
        default: return text::Format(S(i18n::Str::CardFanGeneric), {logic::FanNumber(name)});
    }
}

logic::Availability App::Available() const {
    return {snapshot_.gpuAvailable, snapshot_.gpuAvailable && snapshot_.vramTotal > 0, snapshot_.diskAvailable,
            snapshot_.network.available, snapshot_.battery.present};
}

void App::Render(bool force) {
    if (bars_.empty()) return;
    RebuildItems();
    for (auto& t : bars_) RenderBar(*t, force);
}

void App::RenderBar(Taskbar& t, bool force) {
    if (!t.bar || !t.renderer || t.layout.thicknessPx <= 0) return;
    const bool hover = t.bar == hoverBar_;
    t.renderer->Layout(t.layout, items_);
    if (!force && !t.renderer->NeedsPaint(items_, hover)) return;  // nothing visible changed
    if (!t.renderer->Paint(items_, hover)) {
        // Render target recreated (rare): redo the layout and try once more.
        t.renderer->Layout(t.layout, items_);
        if (!t.renderer->Paint(items_, hover)) return;
    }
    t.renderer->Present(t.bar);
}

void App::UpdateAccessibleName() {
    // Screen readers (Narrator) read the window text of the strip.
    if (bars_.empty()) return;
    std::wstring name = L"sys-bar";
    for (const ItemView& item : items_) name += L" · " + item.label + L" " + item.strong + item.weak;
    if (name != accessibleName_) {
        accessibleName_ = name;
        for (const auto& t : bars_) SetWindowTextW(t->bar, name.c_str());
    }
}

bool App::AnimationsAllowed() const {
    BOOL enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
    if (!enabled || GetSystemMetrics(SM_REMOTESESSION)) return false;  // RDP: every frame costs bandwidth
    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power) && (power.SystemStatusFlag & 1)) return false;  // battery saver
    return true;
}

void App::OnSample() {
    snapshot_ = sampler_.Latest();
    if (!snapshot_.valid || quitting_) return;
    if (cpuDisplayName_.empty()) cpuDisplayName_ = text::CleanCpuName(sampler_.Info().cpuName);

    // Disks are listed in physical order, so their columns never swap places.
    std::sort(snapshot_.disks.begin(), snapshot_.disks.end(),
              [](const DiskSnapshot& a, const DiskSnapshot& b) { return a.number < b.number; });

    std::map<int, float> samples{
        {Key(MetricId::Cpu), static_cast<float>(snapshot_.cpu / 100.0)},
        {Key(MetricId::Ram), text::Ratio(snapshot_.ramUsed, snapshot_.ramTotal)},
        {Key(MetricId::Gpu), static_cast<float>(snapshot_.gpu / 100.0)},
        {Key(MetricId::Vram), text::Ratio(snapshot_.vramUsed, snapshot_.vramTotal)},
        {Key(MetricId::Disk), static_cast<float>(snapshot_.diskActive / 100.0)},
        {Key(MetricId::Battery), static_cast<float>(snapshot_.battery.percent / 100.0)},
    };
    // Network: the bar is the utilization of the link; the card graph scales itself to the traffic.
    const uint64_t busier = std::max(snapshot_.network.downBps, snapshot_.network.upBps);
    samples[Key(MetricId::Net)] =
        snapshot_.network.linkBps ? std::min(1.0f, static_cast<float>(static_cast<double>(busier) * 8.0 /
                                                                      static_cast<double>(snapshot_.network.linkBps)))
                                  : 0.0f;
    netHistory_.push_back(static_cast<double>(busier));
    while (netHistory_.size() > kHistory) netHistory_.pop_front();
    for (const DiskSnapshot& disk : snapshot_.disks) {
        samples[kDiskKeyBase + disk.number] = static_cast<float>(disk.active / 100.0);
    }
    // Forget disks that went away (an unplugged USB drive): their column and history go with them.
    for (auto it = tracks_.begin(); it != tracks_.end();) {
        it = samples.count(it->first) ? std::next(it) : tracks_.erase(it);
    }

    float largest = 0.0f;
    for (const auto& [key, value] : samples) {
        Track& track = tracks_[key];
        track.target = value;
        track.history.push_back(value);
        while (track.history.size() > kHistory) track.history.pop_front();
        largest = std::max(largest, std::fabs(track.target - track.shown));
    }
    if (!AnimationsAllowed() || largest < kAnimationMinDelta) {
        for (auto& [key, track] : tracks_) track.shown = track.target;
        if (animating_) KillTimer(controller_, kAnimationTimer), animating_ = false;
    } else if (!animating_) {
        animating_ = true;
        SetTimer(controller_, kAnimationTimer, kFrameMs, nullptr);
    }

    Reposition();
    UpdateAccessibleName();
    if (cardVisible_) UpdateCard(false);
    MaybeInstallUpdate();
}

void App::OnAnimationTick() {
    bool moving = false;
    for (auto& [key, track] : tracks_) {
        const float delta = track.target - track.shown;
        if (std::fabs(delta) < kAnimationSnap) {
            track.shown = track.target;
        } else {
            track.shown += delta * kAnimationEase;
            moving = true;
        }
    }
    Render();  // skipped automatically when the bars did not move by a visible amount
    if (!moving) {
        KillTimer(controller_, kAnimationTimer);
        animating_ = false;
    }
}

Theme App::CurrentTheme() const {
    Theme theme;
    theme.light = light_;
    theme.colorByLoad = settings_.colorByLoad;
    theme.highContrast = highContrast_;
    if (highContrast_) {
        theme.hcText = GetSysColor(COLOR_WINDOWTEXT);
        theme.hcBackground = GetSysColor(COLOR_WINDOW);
        theme.hcHighlight = GetSysColor(COLOR_HIGHLIGHT);
    }
    theme.locale = i18n::Locale(lang_);
    return theme;
}

void App::RefreshTheme(bool force) {
    const bool light = TaskbarUsesLightTheme();
    const bool highContrast = HighContrastOn();
    if (!force && light == light_ && highContrast == highContrast_) return;
    light_ = light;
    highContrast_ = highContrast;
    theme_ = CurrentTheme();
    for (auto& t : bars_) t->renderer->SetTheme(theme_);
    card_.SetTheme(theme_);
    ApplyMenuTheme(light_ && !highContrast_);
    Render(true);
    if (cardVisible_) UpdateCard(false);
}

void App::ApplyLanguage() {
    i18n::Lang chosen;
    lang_ = i18n::FromCode(settings_.language, chosen) ? chosen : i18n::FromSystem();
    theme_ = CurrentTheme();
    for (auto& t : bars_) t->renderer->SetTheme(theme_);
    card_.SetTheme(theme_);
}

void App::SetPaused(bool paused) {
    sampler_.SetPaused(paused);
    if (paused) {
        KillTimer(controller_, kAnimationTimer);
        animating_ = false;
        for (auto& [key, track] : tracks_) track.shown = track.target;
        HideCard();
    } else {
        RequestReposition();
    }
}

// ---------------------------------------------------------------------------------------------------
// Hover card
// ---------------------------------------------------------------------------------------------------

CardModel App::BuildCardModel() const {
    const auto& s = snapshot_;
    const SystemInfo info = sampler_.Info();
    const std::wstring gb = S(i18n::Str::UnitGB);
    auto percentText = [&](double value) { return text::Format(S(i18n::Str::PercentFormat), {text::Percent(value)}); };
    auto gbText = [&](double value) { return text::Fixed1(value, decimal_) + L" " + gb; };

    CardModel model;
    model.title = S(i18n::Str::CardTitle);
    model.footerLeft = text::Chip(icon::kInfo, L"sys-bar " SYSBAR_VERSION_WTEXT);
    FooterUpdateStatus(model);
    model.capacity = static_cast<int>(kHistory);

    auto row = [&](MetricId id, int key, const std::wstring& title) {
        CardRow r;
        r.id = id;
        r.title = title;
        if (const auto track = tracks_.find(key); track != tracks_.end()) {
            r.history.assign(track->second.history.begin(), track->second.history.end());
        }
        r.load = r.history.empty() ? 0.0f : r.history.back();
        return r;
    };
    auto peakOf = [](const CardRow& r) {
        return r.history.empty() ? 0.0f : *std::max_element(r.history.begin(), r.history.end());
    };
    auto percentRow = [&](MetricId id, int key, const std::wstring& title, double value, std::wstring subtitle) {
        CardRow r = row(id, key, title);
        r.value = percentText(value);
        r.subtitle = std::move(subtitle);
        r.peak = text::Chip(icon::kPeak, text::Format(S(i18n::Str::CardPeak), {percentText(peakOf(r) * 100.0)}));
        model.rows.push_back(std::move(r));
    };
    auto memoryRow = [&](MetricId id, const wchar_t* title, uint64_t used, uint64_t total) {
        CardRow r = row(id, Key(id), title);
        r.value = gbText(text::GiB(used));
        r.subtitle =
            text::IconParts(text::Format(S(i18n::Str::CardOfTotal),
                                         {gbText(text::GiB(total)), percentText(text::Ratio(used, total) * 100.0)}),
                            {icon::kMemory, icon::kUsage});
        r.peak = text::Chip(icon::kPeak, text::Format(S(i18n::Str::CardPeak), {gbText(text::GiB(total) * peakOf(r))}));
        model.rows.push_back(std::move(r));
    };

    percentRow(MetricId::Cpu, Key(MetricId::Cpu), S(i18n::Str::CardCpu), s.cpu,
               text::JoinChips({(cpuDisplayName_.empty() ? std::wstring() : text::Chip(icon::kChip, cpuDisplayName_)),
                                text::Chip(icon::kThreads, text::Format(S(i18n::Str::CardThreads),
                                                                        {std::to_wstring(info.logicalCpus)}))}));
    memoryRow(MetricId::Ram, S(i18n::Str::CardMemory), s.ramUsed, s.ramTotal);
    if (s.gpuAvailable) {
        percentRow(MetricId::Gpu, Key(MetricId::Gpu), S(i18n::Str::CardGpu), s.gpu,
                   (info.gpuName.empty() ? std::wstring() : text::Chip(icon::kDisplay, info.gpuName)));
        if (s.vramTotal > 0) memoryRow(MetricId::Vram, S(i18n::Str::CardGpuMemory), s.vramUsed, s.vramTotal);
    }
    const logic::Availability available = Available();
    if (available.net) {
        // Total traffic as the value; the graph scales to the busiest moment of the window (at least
        // 64 KB/s, so an idle link stays flat), like Task Manager.
        const hw::NetRates& n = s.network;
        CardRow r = row(MetricId::Net, Key(MetricId::Net), S(i18n::Str::CardNetwork));
        double top = 64.0 * 1024.0;
        for (double v : netHistory_) top = std::max(top, v);
        r.history.clear();
        for (double v : netHistory_) r.history.push_back(static_cast<float>(v / top));
        r.load = 0.0f;
        r.neutral = true;
        r.value = RateText(n.downBps + n.upBps);
        std::wstring adapter = n.names;
        if (n.linkBps) adapter += (adapter.empty() ? L"" : L" \u00b7 ") + text::LinkSpeed(n.linkBps, decimal_);
        r.subtitle = text::JoinChips(
            {text::IconParts(text::Format(S(i18n::Str::CardNetIo), {RateText(n.downBps), RateText(n.upBps)}),
                             {icon::kDown, icon::kUp}),
             adapter.empty() ? std::wstring() : text::Chip(n.wireless ? icon::kWifi : icon::kEthernet, adapter)});
        double peak = 0.0;
        for (double v : netHistory_) peak = std::max(peak, v);
        r.peak = text::Chip(icon::kPeak, text::Format(S(i18n::Str::CardPeak), {RateText(static_cast<uint64_t>(peak))}));
        model.rows.push_back(std::move(r));
    }
    if (s.diskAvailable) {
        auto io = [&](uint64_t read, uint64_t write) {
            return text::IconParts(text::Format(S(i18n::Str::CardDiskIo), {RateText(read), RateText(write)}),
                                   {icon::kDown, icon::kUp});
        };
        if (logic::SeparateDisks(settings_, s.disks.size())) {
            // One row per drive: "NVMe (C: E:)", its model and its own read/write rates.
            for (const DiskSnapshot& disk : s.disks) {
                const wchar_t* kind = hw::DiskKindName(disk.kind);
                std::wstring title = *kind ? kind : S(i18n::Str::CardDisk);
                if (!disk.letters.empty()) title += L" (" + disk.letters + L")";
                // Side by side, so compact: the model, then read/write on their own line with just the
                // arrows (the line never changes shape as the rates change).
                std::wstring subtitle = text::JoinChips(
                    {text::Chip(icon::kDown, RateText(disk.readBps)), text::Chip(icon::kUp, RateText(disk.writeBps))});
                if (!disk.model.empty()) subtitle = text::Chip(icon::kDrive, disk.model) + L"\n" + subtitle;
                percentRow(MetricId::Disk, kDiskKeyBase + disk.number, title, disk.active, std::move(subtitle));
                model.rows.back().half = true;  // drives side by side: the card stays short
            }
        } else {
            percentRow(MetricId::Disk, Key(MetricId::Disk), S(i18n::Str::CardDisk), s.diskActive,
                       io(s.diskReadBps, s.diskWriteBps));
        }
    }

    if (available.battery) {
        const hw::BatteryReading& b = s.battery;
        CardRow r = row(MetricId::Battery, Key(MetricId::Battery), S(i18n::Str::CardBattery));
        r.neutral = true;
        r.load = !b.acOnline && b.percent <= 10.0 ? kCriticalLoad : !b.acOnline && b.percent <= 20.0 ? kWarnLoad : 0.0f;
        r.value = text::Format(S(i18n::Str::PercentFormat), {text::Percent(b.percent)});
        const std::wstring state = S(b.charging        ? i18n::Str::CardBatteryCharging
                                     : !b.acOnline     ? i18n::Str::CardBatteryOnBattery
                                     : b.percent >= 99 ? i18n::Str::CardBatteryFull
                                                       : i18n::Str::CardBatteryPlugged);
        r.subtitle = text::JoinChips(
            {text::Chip(b.acOnline ? icon::kCharging : icon::kBattery, state),
             b.watts ? text::Chip(icon::kPower, text::Watts(*b.watts, decimal_)) : std::wstring(),
             b.secondsLeft ? text::Chip(icon::kClock, text::Format(S(i18n::Str::CardBatteryLeft),
                                                                   {text::HoursMinutes(*b.secondsLeft)}))
                           : std::wstring()});
        model.rows.push_back(std::move(r));
    }

    // Sensors: one tile per device, with the fans that cool it (see CardSensorLine).
    const bool fahrenheit = settings_.Fahrenheit();
    auto roleOf = [&](const SensorReading& r) { return logic::EffectiveFanRole(settings_.fanRoles, r.name, r.role); };
    auto rpm = [&](double value) {
        if (value < 1.0) return std::wstring(S(i18n::Str::CardFanStopped));  // zero-RPM mode at idle
        return text::Format(S(i18n::Str::CardRpm), {std::to_wstring(static_cast<int>(std::lround(value)))});
    };
    auto join = [](std::wstring& into, const std::wstring& part) { into += (into.empty() ? L"" : L" · ") + part; };
    auto temperatureTile = [&](MetricId id, std::wstring title, double celsius) {
        CardSensorLine tile;
        tile.id = id;
        tile.title = std::move(title);
        tile.value = text::TemperatureText(celsius, fahrenheit);
        tile.gauge = logic::TemperatureGauge(id, celsius);
        tile.warmAt = logic::TemperatureGauge(id, logic::WarmTemperature(id));
        tile.hotAt = logic::TemperatureGauge(id, logic::HotTemperature(id));
        tile.tickStep = logic::TemperatureGauge(id, 30.0);  // 10 °C above the scale's 20 °C start
        tile.level = settings_.colorByLoad ? logic::TemperatureLevel(id, celsius) : 0;
        return tile;
    };

    // CPU, with its fans (and an AIO pump) as the detail.
    std::optional<CardSensorLine> cpuTile;
    if (s.cpuTemperature) cpuTile = temperatureTile(MetricId::Cpu, L"CPU", *s.cpuTemperature);
    for (const SensorReading& r : s.sensors) {
        if (r.kind != SensorKind::Fan || r.group == SensorGroup::Gpu || roleOf(r) != FanRole::Cpu) continue;
        if (!cpuTile) cpuTile = CardSensorLine{L"CPU", L"", L"", -1.0f, 0, MetricId::Cpu, false};
        join(cpuTile->detail, rpm(r.value));
    }
    if (cpuTile) model.sensors.push_back(std::move(*cpuTile));

    // GPUs ("GPU", or "GPU 1", "GPU 2" with several), with their fan.
    size_t gpus = 0;
    for (const SensorReading& r : s.sensors) gpus += r.group == SensorGroup::Gpu && r.kind == SensorKind::Temperature;
    size_t gpuIndex = 0;
    for (size_t i = 0; i < s.sensors.size(); ++i) {
        const SensorReading& r = s.sensors[i];
        if (r.group != SensorGroup::Gpu || r.kind != SensorKind::Temperature) continue;
        std::wstring title = gpus > 1 ? L"GPU " + std::to_wstring(++gpuIndex) : L"GPU";
        CardSensorLine tile = temperatureTile(MetricId::Gpu, std::move(title), r.value);
        // The sampler lists each adapter's fan right after its temperature: two identical cards
        // (same name) never get each other's fan.
        if (i + 1 < s.sensors.size()) {
            const SensorReading& f = s.sensors[i + 1];
            if (f.group == SensorGroup::Gpu && f.kind == SensorKind::Fan && f.name == r.name)
                join(tile.detail, rpm(f.value));
        }
        if (tile.detail.empty()) tile.detail = r.name;
        model.sensors.push_back(std::move(tile));
    }

    // Drives: "NVMe C:" with the model as the detail; then drives only the service could read.
    for (const DiskSnapshot& disk : s.disks) {
        if (!disk.temperature) continue;
        CardSensorLine tile = temperatureTile(
            MetricId::Disk, logic::DiskStripLabel(disk.kind, disk.letters, disk.number, S(i18n::Str::CardDisk)),
            *disk.temperature);
        tile.detail = disk.model;
        model.sensors.push_back(std::move(tile));
    }
    for (const SensorReading& r : s.sensors) {
        if (r.group == SensorGroup::Disk && r.kind == SensorKind::Temperature) {
            model.sensors.push_back(temperatureTile(MetricId::Disk, r.name, r.value));
        }
    }

    // Remaining fans: case, pump, power supply, or an unlabeled header ("Fan 3").
    for (const SensorReading& r : s.sensors) {
        const FanRole role = roleOf(r);
        if (r.kind != SensorKind::Fan || r.group == SensorGroup::Gpu || role == FanRole::Cpu) continue;
        CardSensorLine tile;
        tile.fan = true;
        tile.id = MetricId::Cpu;
        tile.value = rpm(r.value);
        tile.title = FanRoleName(role, r.name);
        if (role != FanRole::Unknown) tile.detail = r.name;  // "Fan #1": which header it is
        model.sensors.push_back(std::move(tile));
    }
    if (!s.advancedSensors && !SensorsTool().empty()) {
        model.sensorsHint = text::Chip(icon::kThermometer, S(i18n::Str::CardSensorsHint));
    }
    if (!model.sensors.empty() || !model.sensorsHint.empty()) model.sensorsTitle = S(i18n::Str::CardSensors);

    // Notice: a short-lived message the user asked for (e.g. advanced sensors turned on).
    if (!notice_.empty() && GetTickCount64() < noticeUntil_) model.notice = notice_;
    return model;
}

void App::PresentCard() {
    const float eased = EaseOutCubic(cardAlpha_);
    const POINT at{cardPosition_.x + static_cast<LONG>(std::lround(cardSlide_.x * (1.0f - eased))),
                   cardPosition_.y + static_cast<LONG>(std::lround(cardSlide_.y * (1.0f - eased)))};
    card_.Present(cardWindow_, at, static_cast<BYTE>(std::lround(255.0f * eased)));
}

bool App::UpdateCard(bool reposition) {
    const Taskbar* t = CardBar();
    if (!cardWindow_ || !t) return false;
    const CardModel model = BuildCardModel();
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(t->bar, MONITOR_DEFAULTTONEAREST), &mi);
    const int dpi = static_cast<int>(t->layout.dpi);
    const int gapPx = MulDiv(kCardGapDip, dpi, USER_DEFAULT_SCREEN_DPI);
    // The card must fit between the taskbar and the other edge of the screen (the work area).
    const int room = mi.rcWork.bottom - mi.rcWork.top;
    const int roomWide = mi.rcWork.right - mi.rcWork.left;
    const SIZE size =
        card_.Measure(t->layout.dpi, model, std::max(room - 2 * gapPx, 200), std::max(roomWide - 2 * gapPx, 200));
    // A recreated render target (display driver reset) fails once: retry like the strip does.
    if (!card_.Paint(model) && !card_.Paint(model)) {
        Log(L"card paint failed");
        return false;
    }

    // Live values change the card's size: place it again so it never grows over the taskbar.
    if (size.cx != cardSize_.cx || size.cy != cardSize_.cy) reposition = true;
    cardSize_ = size;
    if (reposition) {
        logic::CardInput in;
        GetWindowRect(t->bar, &in.strip);
        in.monitor = mi.rcMonitor;
        in.card = size;
        in.marginPx = card_.ShadowMarginPx();
        in.gapPx = gapPx;
        in.slidePx = MulDiv(kCardSlideDip, dpi, USER_DEFAULT_SCREEN_DPI);
        in.verticalTaskbar = t->vertical;
        const logic::CardPlacement placed = logic::PlaceCard(in);
        cardPosition_ = placed.position;
        cardSlide_ = placed.slide;
    }
    PresentCard();
    return true;
}

void App::ShowCard() {
    if (!cardWindow_ || !CardBar() || !snapshot_.valid) return;
    cardWanted_ = true;
    if (!cardVisible_) {
        cardAlpha_ = AnimationsAllowed() ? 0.0f : 1.0f;
        if (!UpdateCard(true)) {
            cardWanted_ = false;
            return;
        }
        SetWindowPos(cardWindow_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        cardVisible_ = true;
    }
    if (cardAlpha_ < 1.0f) SetTimer(controller_, kCardFadeTimer, kFrameMs, nullptr);
}

void App::HideCard() {
    KillTimer(controller_, kCardDelayTimer);
    cardWanted_ = false;
    if (!cardVisible_) return;
    if (!AnimationsAllowed()) cardAlpha_ = 0.0f;
    SetTimer(controller_, kCardFadeTimer, kFrameMs, nullptr);
    OnCardFadeTick();
}

void App::OnCardFadeTick() {
    cardAlpha_ =
        cardWanted_ ? std::min(1.0f, cardAlpha_ + kCardFadeInStep) : std::max(0.0f, cardAlpha_ - kCardFadeOutStep);
    if (!cardWanted_ && cardAlpha_ <= 0.0f) {
        KillTimer(controller_, kCardFadeTimer);
        ShowWindow(cardWindow_, SW_HIDE);
        cardVisible_ = false;
        card_.ForgetWidth();
        return;
    }
    PresentCard();  // only re-presents the painted bitmap with a new alpha/position (cheap)
    if (cardWanted_ && cardAlpha_ >= 1.0f) KillTimer(controller_, kCardFadeTimer);
}

// ---------------------------------------------------------------------------------------------------
// Updates
// ---------------------------------------------------------------------------------------------------

void App::OnUpdateTimer() {
    SetTimer(controller_, kUpdateTimer, kUpdateTimerMs, nullptr);
    if (!settings_.checkUpdates) return;
    const int64_t now = static_cast<int64_t>(time(nullptr));
    if (now - settings_.lastUpdateCheck >= kUpdateIntervalS) updater_.Check();
}

void App::OnUpdateState(update::State state) {
    using update::State;
    Log(L"updater state %d", static_cast<int>(state));
    const auto release = updater_.latest();
    const std::wstring version = release ? update::ToString(release->version) : L"";
    switch (state) {
        case State::UpToDate:
        case State::Available:
            settings_.lastUpdateCheck = static_cast<int64_t>(time(nullptr));
            settings_.Save();
            Log(L"update check: %s (latest %s)", state == State::Available ? L"available" : L"up to date",
                version.c_str());
            if (state == State::Available && updater_.installedCopy() && (settings_.autoUpdate || installWhenReady_)) {
                installWhenReady_ = true;
                updater_.Download();
            }
            if (manualUpdateCheck_) {
                manualUpdateCheck_ = false;
                ShowNotice({});  // the footer shows the result
            }
            break;
        case State::Ready: MaybeInstallUpdate(); break;
        case State::Failed:
            Log(L"update failed");
            if (manualUpdateCheck_ || installWhenReady_) ShowNotice({});
            manualUpdateCheck_ = installWhenReady_ = false;
            break;
        default: break;
    }
    if (cardVisible_) UpdateCard(false);
}

// Update status for the card's footer: the result of the last check, or what is going on now.
void App::FooterUpdateStatus(CardModel& model) const {
    using update::State;
    const auto release = updater_.latest();
    const std::wstring version = release ? update::ToString(release->version) : L"";
    switch (updater_.state()) {
        case State::Checking: model.footerRight = text::Chip(icon::kSync, S(i18n::Str::UpdateChecking)); break;
        case State::Available:
            model.footerRight = text::Chip(icon::kUpdate, text::Format(S(i18n::Str::CardUpdateAvailable), {version}));
            model.footerAccent = true;
            break;
        case State::Downloading:
        case State::Ready:
        case State::Installing:
            model.footerRight = text::Chip(icon::kUpdate, text::Format(S(i18n::Str::UpdateDownloading), {version}));
            model.footerAccent = true;
            break;
        case State::Failed: model.footerRight = text::Chip(icon::kWarning, S(i18n::Str::UpdateFailed)); break;
        case State::UpToDate: model.footerRight = text::Chip(icon::kCheck, S(i18n::Str::CardUpToDate)); break;
        default: break;  // never checked (or checks turned off): nothing to say
    }
}

// Installs a downloaded update at a quiet moment: never while the user is looking at the card.
void App::MaybeInstallUpdate() {
    if (!installWhenReady_ || updater_.state() != update::State::Ready || hoverBar_ || cardVisible_) return;
    Log(L"installing update");
    installWhenReady_ = false;
    updater_.Install();  // the installer closes us cleanly and starts the new version
}

void App::ShowNotice(std::wstring text) {
    notice_ = std::move(text);
    // The mouse may be anywhere (e.g. the result of "check for updates" arrives later): show it above
    // the strip the user last used, or the main one.
    if (!hoverBar_) noticeBar_ = pressedBar_ ? pressedBar_ : noticeBar_;
    noticeUntil_ = GetTickCount64() + kNoticeMs;
    if (cardVisible_) {
        UpdateCard(false);
    } else {
        ShowCard();
    }
    SetTimer(controller_, kNoticeTimer, kNoticeMs, nullptr);
}

std::wstring App::UpdateStatusText() const {
    using update::State;
    const auto release = updater_.latest();
    const std::wstring version = release ? update::ToString(release->version) : L"";
    switch (updater_.state()) {
        case State::Checking: return S(i18n::Str::UpdateChecking);
        case State::Available: return text::Format(S(i18n::Str::CardUpdateAvailable), {version});
        case State::Downloading:
        case State::Ready:
        case State::Installing: return text::Format(S(i18n::Str::UpdateDownloading), {version});
        case State::Failed: return S(i18n::Str::UpdateFailed);
        default: return text::Format(S(i18n::Str::UpdateUpToDate), {SYSBAR_VERSION_WTEXT});
    }
}
// ---------------------------------------------------------------------------------------------------
// Advanced sensors
// ---------------------------------------------------------------------------------------------------

std::wstring App::SensorsTool() const {
#if defined(_M_X64)
    wchar_t path[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    std::wstring tool(path, n);
    tool = tool.substr(0, tool.find_last_of(L'\\')) + L"\\sensors\\sys-bar-sensors.exe";
    return GetFileAttributesW(tool.c_str()) == INVALID_FILE_ATTRIBUTES ? std::wstring() : tool;
#else
    return {};  // the PawnIO driver is x64-only
#endif
}

// Runs the service installer elevated (UAC) on a worker thread and reports back with WM_APP_SENSORS.
void App::RunSensorsTool(bool install) {
    // Removal runs the installed copy in Program Files, which only administrators can change; installing
    // has to start from our folder, and that copy checks every file it takes (see sensors/Installer.cs).
    std::wstring tool = SensorsTool();
    if (!install) {
        wchar_t programFiles[MAX_PATH] = {};
        const DWORD n = GetEnvironmentVariableW(L"ProgramW6432", programFiles, MAX_PATH);
        const std::wstring installed =
            std::wstring(programFiles, n < MAX_PATH ? n : 0) + L"\\sys-bar\\sensors\\sys-bar-sensors.exe";
        if (n > 0 && GetFileAttributesW(installed.c_str()) != INVALID_FILE_ATTRIBUTES) tool = installed;
    }
    if (tool.empty()) return;
    std::thread([tool, install, controller = controller_] {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        SHELLEXECUTEINFOW info{sizeof(info)};
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        info.lpVerb = L"runas";
        info.lpFile = tool.c_str();
        info.lpParameters = install ? L"--install" : L"--uninstall";
        info.nShow = SW_HIDE;
        DWORD code = ERROR_CANCELLED;
        if (ShellExecuteExW(&info) && info.hProcess) {
            WaitForSingleObject(info.hProcess, INFINITE);
            GetExitCodeProcess(info.hProcess, &code);
            CloseHandle(info.hProcess);
        } else {
            code = GetLastError();
        }
        if (SUCCEEDED(hr)) CoUninitialize();
        PostMessageW(controller, WM_APP_SENSORS, install ? 1 : 0, static_cast<LPARAM>(code));
    }).detach();
}
// ---------------------------------------------------------------------------------------------------
// Menu and commands
// ---------------------------------------------------------------------------------------------------

void App::ShowMenu(HWND bar, POINT screen) {
    HideCard();
    HMENU menu = CreatePopupMenu();
    HMENU intervals = CreatePopupMenu();
    HMENU languages = CreatePopupMenu();
    HMENU updates = CreatePopupMenu();
    HMENU units = CreatePopupMenu();
    HMENU disks = CreatePopupMenu();
    if (!menu || !intervals || !languages || !updates || !units || !disks) {
        for (HMENU m : {menu, intervals, languages, updates, units, disks}) {
            if (m) DestroyMenu(m);
        }
        return;
    }

    const logic::Availability available = Available();
    auto check = [](bool on) { return static_cast<UINT>(MF_STRING | (on ? MF_CHECKED : MF_UNCHECKED)); };
    auto toggle = [&](UINT id, bool on, MetricId metric, bool present, i18n::Str label) {
        UINT flags = check(on);
        if (!present || (on && !logic::CanHide(settings_, metric, available))) flags |= MF_GRAYED;
        AppendMenuW(menu, flags, id, S(label));
    };
    toggle(kCmdShowCpu, settings_.showCpu, MetricId::Cpu, true, i18n::Str::MenuCpu);
    toggle(kCmdShowRam, settings_.showRam, MetricId::Ram, true, i18n::Str::MenuRam);
    toggle(kCmdShowGpu, settings_.showGpu, MetricId::Gpu, available.gpu, i18n::Str::MenuGpu);
    toggle(kCmdShowVram, settings_.showVram, MetricId::Vram, available.vram, i18n::Str::MenuVram);
    toggle(kCmdShowDisk, settings_.showDisk, MetricId::Disk, available.disk, i18n::Str::MenuDisk);
    toggle(kCmdShowNet, settings_.showNet, MetricId::Net, available.net, i18n::Str::MenuNetwork);
    if (available.battery) {
        toggle(kCmdShowBattery, settings_.showBattery, MetricId::Battery, true, i18n::Str::MenuBattery);
    }
    AppendMenuW(menu, check(settings_.showTemperatures), kCmdShowTemperatures, S(i18n::Str::MenuTemperatures));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // Disks: one column per drive or a single combined one (only meaningful with several drives).
    AppendMenuW(disks, MF_STRING, kCmdDisksEach, S(i18n::Str::MenuDisksEach));
    AppendMenuW(disks, MF_STRING, kCmdDisksCombined, S(i18n::Str::MenuDisksCombined));
    CheckMenuRadioItem(disks, kCmdDisksEach, kCmdDisksCombined,
                       settings_.disksSeparate ? kCmdDisksEach : kCmdDisksCombined, MF_BYCOMMAND);
    const bool severalDisks = snapshot_.disks.size() > 1 && snapshot_.disks.size() <= logic::kMaxDiskColumns;
    AppendMenuW(menu, MF_POPUP | (severalDisks && settings_.showDisk ? 0u : static_cast<UINT>(MF_GRAYED)),
                reinterpret_cast<UINT_PTR>(disks), S(i18n::Str::MenuDisks));

    // Temperature unit: automatic follows the Windows region (Fahrenheit in the U.S.).
    AppendMenuW(units, MF_STRING, kCmdTemperatureAuto, S(i18n::Str::MenuTemperatureAuto));
    AppendMenuW(units, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(units, MF_STRING, kCmdTemperatureC, L"Celsius (°C)");
    AppendMenuW(units, MF_STRING, kCmdTemperatureF, L"Fahrenheit (°F)");
    const UINT unit = settings_.temperatureUnit == L"C"   ? kCmdTemperatureC
                      : settings_.temperatureUnit == L"F" ? kCmdTemperatureF
                                                          : kCmdTemperatureAuto;
    CheckMenuRadioItem(units, kCmdTemperatureAuto, kCmdTemperatureF, unit, MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(units), S(i18n::Str::MenuTemperatureUnit));

    // Fans: what each motherboard fan cools, when the automatic guess is wrong for this board.
    menuFans_.clear();
    for (const SensorReading& r : snapshot_.sensors) {
        if (r.kind == SensorKind::Fan && r.group == SensorGroup::Board && menuFans_.size() < kMaxMenuFans) {
            menuFans_.push_back(r.name);
        }
    }
    if (!menuFans_.empty()) {
        HMENU fans = CreatePopupMenu();
        for (size_t i = 0; i < menuFans_.size() && fans; ++i) {
            const SensorReading* reading = nullptr;
            for (const SensorReading& r : snapshot_.sensors) {
                if (r.kind == SensorKind::Fan && r.group == SensorGroup::Board && r.name == menuFans_[i]) reading = &r;
            }
            if (!reading) continue;
            HMENU roles = CreatePopupMenu();
            if (!roles) continue;
            const UINT base = kCmdFan0 + static_cast<UINT>(i) * kFanCommandStride;
            const FanRole chosen = logic::EffectiveFanRole(settings_.fanRoles, reading->name, FanRole::Unknown);
            AppendMenuW(roles, MF_STRING, base,
                        text::Format(S(i18n::Str::MenuFanAuto), {FanRoleName(reading->role, reading->name)}).c_str());
            AppendMenuW(roles, MF_SEPARATOR, 0, nullptr);
            for (FanRole role : {FanRole::Cpu, FanRole::Gpu, FanRole::Case, FanRole::Pump, FanRole::Psu}) {
                AppendMenuW(roles, MF_STRING, base + static_cast<UINT>(role), FanRoleName(role, reading->name).c_str());
            }
            CheckMenuRadioItem(roles, base, base + static_cast<UINT>(FanRole::Psu), base + static_cast<UINT>(chosen),
                               MF_BYCOMMAND);
            const std::wstring label =
                reading->name + L" \u00b7 " +
                text::Format(S(i18n::Str::CardRpm), {std::to_wstring(static_cast<int>(std::lround(reading->value)))});
            AppendMenuW(fans, MF_POPUP, reinterpret_cast<UINT_PTR>(roles), label.c_str());
        }
        if (fans) AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(fans), S(i18n::Str::MenuFans));
    }

    bool presetSelected = false;
    for (int i = 0; i < kIntervalCount; ++i) {
        AppendMenuW(intervals, MF_STRING, kCmdInterval0 + i,
                    text::IntervalLabel(kIntervals[i], decimal_, S(i18n::Str::MenuSeconds)).c_str());
        if (settings_.intervalMs == kIntervals[i]) {
            CheckMenuRadioItem(intervals, kCmdInterval0, kCmdInterval0 + kIntervalCount - 1, kCmdInterval0 + i,
                               MF_BYCOMMAND);
            presetSelected = true;
        }
    }
    if (!presetSelected) {  // custom value from settings.ini
        AppendMenuW(intervals, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(intervals, MF_STRING | MF_CHECKED, kCmdIntervalCustom,
                    text::IntervalLabel(settings_.intervalMs, decimal_, S(i18n::Str::MenuSeconds)).c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(intervals), S(i18n::Str::MenuInterval));

    i18n::Lang stored;
    const bool automatic = !i18n::FromCode(settings_.language, stored);
    AppendMenuW(languages, check(automatic), kCmdLanguageAuto, S(i18n::Str::MenuLanguageAuto));
    AppendMenuW(languages, MF_SEPARATOR, 0, nullptr);
    for (int i = 0; i < i18n::kLanguageCount; ++i) {
        const auto lang = static_cast<i18n::Lang>(i);
        AppendMenuW(languages, check(!automatic && lang == stored), kCmdLanguage0 + i, i18n::NativeName(lang));
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(languages), S(i18n::Str::MenuLanguage));

    // Updates: status line, check now, automatic install and, when available, install/download.
    {
        AppendMenuW(updates, MF_STRING | MF_GRAYED, 0, UpdateStatusText().c_str());
        AppendMenuW(updates, MF_SEPARATOR, 0, nullptr);
        if (const auto release = updater_.latest(); release && updater_.state() == update::State::Available) {
            const std::wstring version = update::ToString(release->version);
            AppendMenuW(
                updates, MF_STRING, kCmdInstallUpdate,
                text::Format(S(updater_.installedCopy() ? i18n::Str::MenuUpdateInstall : i18n::Str::MenuUpdateDownload),
                             {version})
                    .c_str());
        }
        const bool busy = updater_.state() == update::State::Checking || updater_.state() == update::State::Downloading;
        AppendMenuW(updates, MF_STRING | (busy ? MF_GRAYED : 0u), kCmdCheckUpdates, S(i18n::Str::MenuCheckUpdates));
        AppendMenuW(updates,
                    check(settings_.autoUpdate) | (updater_.installedCopy() ? 0u : static_cast<UINT>(MF_GRAYED)),
                    kCmdAutoUpdate, S(i18n::Str::MenuAutoUpdate));
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(updates), S(i18n::Str::MenuUpdates));
    }

    if (!SensorsTool().empty()) {
        AppendMenuW(
            menu, check(snapshot_.advancedSensors), kCmdAdvancedSensors,
            S(snapshot_.advancedSensors ? i18n::Str::MenuAdvancedSensorsActive : i18n::Str::MenuAdvancedSensors));
    }
    AppendMenuW(menu, check(settings_.colorByLoad), kCmdColorByLoad, S(i18n::Str::MenuHighlight));
    AppendMenuW(menu, check(settings_.clickOpensTaskManager), kCmdClickOpensTaskManager, S(i18n::Str::MenuClickOpens));
    if (FindWindowExW(nullptr, nullptr, L"Shell_SecondaryTrayWnd", nullptr)) {  // only with several taskbars
        AppendMenuW(menu, check(settings_.allTaskbars), kCmdAllTaskbars, S(i18n::Str::MenuAllTaskbars));
    }
    AppendMenuW(menu, check(Settings::IsAutostartEnabled()), kCmdAutostart, S(i18n::Str::MenuAutostart));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdTaskManager, S(i18n::Str::MenuTaskManager));
    AppendMenuW(menu, MF_STRING, kCmdAbout, text::Format(S(i18n::Str::MenuAbout), {SYSBAR_VERSION_WTEXT}).c_str());
    AppendMenuW(menu, MF_STRING, kCmdExit, S(i18n::Str::MenuExit));

    // The menu needs a foreground window to close properly when the user clicks elsewhere.
    SetForegroundWindow(controller_);
    TPMPARAMS params{sizeof(params)};
    if (const Taskbar* t = FindBar(bar)) GetWindowRect(t->tray, &params.rcExclude);
    const UINT command = static_cast<UINT>(TrackPopupMenuEx(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY | TPM_VERTICAL, screen.x, screen.y, controller_, &params));
    PostMessageW(controller_, WM_NULL, 0, 0);
    DestroyMenu(menu);  // destroys the submenus too

    // The pointer may have left the strip while the menu was open.
    POINT cursor{};
    if (hoverBar_ && GetCursorPos(&cursor) && WindowFromPoint(cursor) != hoverBar_) {
        if (Taskbar* t = FindBar(hoverBar_)) t->tracking = false;
        hoverBar_ = pressedBar_ = nullptr;
        Render();
    }
    if (command) Execute(command);
}

void App::Execute(UINT command) {
    Log(L"menu command %u", command);
    auto toggle = [&](bool& flag, MetricId id) {
        if (flag && !logic::CanHide(settings_, id, Available())) return;  // keep at least one metric
        flag = !flag;
        settings_.Save();
        Reposition();
    };

    switch (command) {
        case kCmdShowCpu: toggle(settings_.showCpu, MetricId::Cpu); break;
        case kCmdShowRam: toggle(settings_.showRam, MetricId::Ram); break;
        case kCmdShowGpu: toggle(settings_.showGpu, MetricId::Gpu); break;
        case kCmdShowVram: toggle(settings_.showVram, MetricId::Vram); break;
        case kCmdShowDisk: toggle(settings_.showDisk, MetricId::Disk); break;
        case kCmdShowNet: toggle(settings_.showNet, MetricId::Net); break;
        case kCmdShowBattery: toggle(settings_.showBattery, MetricId::Battery); break;
        case kCmdShowTemperatures:
            settings_.showTemperatures = !settings_.showTemperatures;
            settings_.Save();
            Reposition();
            break;
        case kCmdColorByLoad:
            settings_.colorByLoad = !settings_.colorByLoad;
            settings_.Save();
            RefreshTheme(true);
            break;
        case kCmdAllTaskbars:
            settings_.allTaskbars = !settings_.allTaskbars;
            settings_.Save();
            Reposition();
            break;
        case kCmdClickOpensTaskManager:
            settings_.clickOpensTaskManager = !settings_.clickOpensTaskManager;
            settings_.Save();
            break;
        case kCmdAutostart:
            if (!Settings::SetAutostart(!Settings::IsAutostartEnabled())) Log(L"autostart change failed");
            break;
        case kCmdTaskManager: ShellOpen(L"taskmgr.exe"); break;
        case kCmdAbout: ShellOpen(SYSBAR_PROJECT_URL); break;
        case kCmdExit: Quit(); break;
        case kCmdIntervalCustom: break;
        case kCmdAdvancedSensors: RunSensorsTool(!snapshot_.advancedSensors); break;
        case kCmdDisksEach:
        case kCmdDisksCombined:
            settings_.disksSeparate = command == kCmdDisksEach;
            settings_.Save();
            Reposition();
            break;
        case kCmdTemperatureAuto:
        case kCmdTemperatureC:
        case kCmdTemperatureF:
            settings_.temperatureUnit = command == kCmdTemperatureC   ? L"C"
                                        : command == kCmdTemperatureF ? L"F"
                                                                      : L"auto";
            settings_.Save();
            Reposition();
            if (cardVisible_) UpdateCard(false);
            break;
        case kCmdCheckUpdates:
            manualUpdateCheck_ = true;
            updater_.Check();
            break;
        case kCmdAutoUpdate:
            settings_.autoUpdate = !settings_.autoUpdate;
            settings_.Save();
            break;
        case kCmdInstallUpdate:
            if (updater_.installedCopy()) {
                installWhenReady_ = true;
                updater_.Download();
            } else if (const auto release = updater_.latest()) {
                ShellOpen(release->pageUrl.empty() ? std::wstring(SYSBAR_PROJECT_URL) + L"/releases/latest"
                                                   : release->pageUrl);
            }
            break;
        case kCmdLanguageAuto:
            settings_.language = L"auto";
            settings_.Save();
            ApplyLanguage();
            Reposition();
            break;
        default:
            if (command >= kCmdInterval0 && command < kCmdInterval0 + kIntervalCount) {
                settings_.intervalMs = kIntervals[command - kCmdInterval0];
                settings_.Save();
                sampler_.SetInterval(settings_.intervalMs);
                for (auto& [key, track] : tracks_) track.history.clear();  // new time scale: start over
                netHistory_.clear();
            } else if (command >= kCmdFan0 && command < kCmdFan0 + kMaxMenuFans * kFanCommandStride) {
                const size_t index = (command - kCmdFan0) / kFanCommandStride;
                const auto role = static_cast<FanRole>((command - kCmdFan0) % kFanCommandStride);
                if (index < menuFans_.size() && role <= FanRole::Psu) {
                    settings_.fanRoles = logic::WithFanRole(settings_.fanRoles, menuFans_[index], role);
                    settings_.Save();
                    if (cardVisible_) UpdateCard(false);
                }
            } else if (command >= kCmdLanguage0 && command < kCmdLanguage0 + i18n::kLanguageCount) {
                settings_.language = i18n::Code(static_cast<i18n::Lang>(command - kCmdLanguage0));
                settings_.Save();
                ApplyLanguage();
                Reposition();  // units ("GB"/"Go"/"ГБ") may change the strip width
            }
            break;
    }
}

void App::Quit() {
    if (quitting_) return;
    quitting_ = true;
    KillTimer(controller_, kHousekeepingTimer);
    KillTimer(controller_, kAnimationTimer);
    sampler_.Stop();
    DetachAll(true);
    if (displayNotify_) UnregisterPowerSettingNotification(displayNotify_), displayNotify_ = nullptr;
    WTSUnRegisterSessionNotification(controller_);
    if (cardWindow_) DestroyWindow(cardWindow_), cardWindow_ = nullptr;
    DestroyWindow(controller_);
}

// ---------------------------------------------------------------------------------------------------
// Window procedures
// ---------------------------------------------------------------------------------------------------

void CALLBACK App::OnWinEvent(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    App* app = g_app;
    if (!app || !hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    for (const auto& t : app->bars_) {
        if (hwnd == t->bar) return;
        if (hwnd == t->taskList || hwnd == t->container || hwnd == t->tray || hwnd == t->notify ||
            (t->container && GetParent(hwnd) == t->container)) {  // toolbars moving inside the ReBar
            app->RequestReposition();
            return;
        }
    }
}

LRESULT CALLBACK App::ControllerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return app ? app->HandleController(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK App::BarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return app ? app->HandleBar(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::HandleController(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbarCreated_ && taskbarCreated_ != 0) {
        // Explorer restarted (or re-broadcast this message): strips whose taskbar is gone are rebuilt. Posted,
        // never run here: this may arrive while we are in the middle of moving Explorer's windows.
        RequestReposition();
        return 0;
    }

    switch (msg) {
        case WM_APP_SAMPLE: OnSample(); return 0;
        case WM_APP_REPOSITION: Reposition(); return 0;
        case WM_APP_TRAYEDGE:
            for (auto& t : bars_) {
                if (t->tray != reinterpret_cast<HWND>(wp)) continue;
                t->probing = false;
                const int reserve = static_cast<int>(lp);
                if (reserve != t->reservePx) {
                    t->reservePx = reserve;
                    RequestReposition();
                }
            }
            return 0;
        case WM_APP_UPDATE: OnUpdateState(static_cast<update::State>(wp)); return 0;
        case WM_APP_SENSORS:
            Log(L"advanced sensors %s: exit code %lu", wp ? L"install" : L"uninstall", static_cast<DWORD>(lp));
            if (wp && lp == 0) ShowNotice(S(i18n::Str::MenuAdvancedSensorsActive));
            return 0;
        case WM_TIMER:
            switch (wp) {
                case kHousekeepingTimer:
                    if (!displayOff_ && !locked_) {
                        RefreshTheme(false);
                        Reposition();
                    }
                    break;
                case kAnimationTimer: OnAnimationTick(); break;
                case kCardDelayTimer:
                    KillTimer(hwnd, kCardDelayTimer);
                    if (hoverBar_) ShowCard();
                    break;
                case kCardFadeTimer: OnCardFadeTick(); break;
                case kUpdateTimer: OnUpdateTimer(); break;
                case kNoticeTimer:
                    KillTimer(hwnd, kNoticeTimer);
                    if (!hoverBar_) HideCard();
                    break;
            }
            return 0;
        case WM_SETTINGCHANGE:
            if ((lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0) ||
                wp == SPI_SETHIGHCONTRAST) {
                RefreshTheme(false);
            }
            decimal_ = UserDecimalSeparator();
            if (settings_.language == L"auto") ApplyLanguage();
            RequestReposition();
            return 0;
        case WM_SYSCOLORCHANGE: RefreshTheme(true); return 0;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED: RequestReposition(); return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_POWERSETTINGCHANGE) {
                const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lp);
                if (setting && IsEqualGUID(setting->PowerSetting, GUID_CONSOLE_DISPLAY_STATE) &&
                    setting->DataLength >= sizeof(DWORD)) {
                    displayOff_ = *reinterpret_cast<const DWORD*>(setting->Data) == 0;  // 0 off, 1 on, 2 dimmed
                    SetPaused(displayOff_ || locked_);
                }
            } else if (wp == PBT_APMRESUMEAUTOMATIC) {
                RequestReposition();
            }
            return TRUE;
        case WM_WTSSESSION_CHANGE:
            if (wp == WTS_SESSION_LOCK || wp == WTS_SESSION_UNLOCK) {
                locked_ = wp == WTS_SESSION_LOCK;
                SetPaused(displayOff_ || locked_);
            }
            return 0;
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION:
            if (wp) {
                // The session is going away: stop everything, but do not touch Explorer while it is
                // shutting down too (it starts from scratch at the next logon).
                quitting_ = true;
                KillTimer(hwnd, kHousekeepingTimer);
                KillTimer(hwnd, kAnimationTimer);
                sampler_.Stop();
                DetachAll(false);
            }
            return 0;
        case WM_CLOSE: Quit(); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::HandleBar(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;  // never steal focus from the app in use
        case WM_MOUSEMOVE: {
            Taskbar* t = FindBar(hwnd);
            if (!t) return 0;
            if (!t->tracking) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                t->tracking = TrackMouseEvent(&tme) != FALSE;
            }
            if (hoverBar_ != hwnd) {
                if (hoverBar_) HideCard();  // moved straight from another monitor's strip
                hoverBar_ = hwnd;
                Render();
                SetTimer(controller_, kCardDelayTimer, kCardDelayMs, nullptr);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (Taskbar* t = FindBar(hwnd)) t->tracking = false;
            if (pressedBar_ == hwnd) pressedBar_ = nullptr;
            if (hoverBar_ == hwnd) {
                HideCard();
                hoverBar_ = nullptr;
                Render();
            }
            return 0;
        case WM_LBUTTONDOWN:
            pressedBar_ = hwnd;
            HideCard();
            return 0;
        case WM_RBUTTONDOWN: HideCard(); return 0;
        case WM_LBUTTONUP:
            // Only a click that started on this strip counts (not a drag released over it).
            if (pressedBar_ == hwnd && settings_.clickOpensTaskManager) ShellOpen(L"taskmgr.exe");
            pressedBar_ = nullptr;
            return 0;
        case WM_RBUTTONUP: {
            POINT pt{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
            ClientToScreen(hwnd, &pt);
            ShowMenu(hwnd, pt);
            return 0;
        }
        case WM_DPICHANGED_AFTERPARENT: RequestReposition(); return 0;
        case WM_NCDESTROY:
            // Destroyed by us (Detach) or by Explorer tearing down its taskbar: forget the handle.
            for (auto& t : bars_) {
                if (t->bar == hwnd) {
                    t->bar = nullptr;
                    t->shown = t->tracking = false;
                }
            }
            if (hoverBar_ == hwnd) {
                hoverBar_ = nullptr;
                HideCard();
            }
            if (noticeBar_ == hwnd) noticeBar_ = nullptr;
            if (pressedBar_ == hwnd) pressedBar_ = nullptr;
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
