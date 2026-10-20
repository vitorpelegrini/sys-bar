#include "app.h"

#include "format.h"
#include "logic.h"
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

constexpr int kModernGapDip = 4;                   // space between the strip and the Windows 11 tray
constexpr int kModernFallbackRightDip = 380;       // used only when the tray area cannot be located
constexpr size_t kMaxTaskListMoves = 12;           // tug-of-war guard against Explorer...
constexpr ULONGLONG kTaskListMoveWindowMs = 2000;  // ...per time window
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
    kCmdInterval0 = 200,  // + index into kIntervals
    kCmdLanguageAuto = 300,
    kCmdLanguage0 = 301,  // + i18n::Lang
};

constexpr DWORD kIntervals[] = {500, 1000, 2000, 5000};
constexpr int kIntervalCount = static_cast<int>(std::size(kIntervals));

// The WinEvent callback carries no context pointer, hence this single global.
App* g_app = nullptr;

// What the crash handler needs to give the taskbar its space back (no allocations involved).
struct CrashRestore {
    HWND taskList = nullptr;
    RECT rect{};
    bool active = false;
} g_crashRestore;

void Log(const wchar_t* format, ...) {
    wchar_t buffer[512];
    int n = swprintf_s(buffer, L"[sys-bar] ");
    va_list args;
    va_start(args, format);
    vswprintf_s(buffer + n, std::size(buffer) - n, format, args);
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
    // Explorer would keep the task list shrunk until its next layout pass: restore it right away.
    if (g_crashRestore.active && IsWindow(g_crashRestore.taskList)) {
        const RECT& r = g_crashRestore.rect;
        SetWindowPos(g_crashRestore.taskList, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
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
    Detach(true);
    g_app = nullptr;
    return static_cast<int>(msg.wParam);
}

// ---------------------------------------------------------------------------------------------------
// Taskbar integration
// ---------------------------------------------------------------------------------------------------

bool App::Attach() {
    tray_ = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray_) return false;
    notify_ = FindWindowExW(tray_, nullptr, L"TrayNotifyWnd", nullptr);

    // Decide by structure, not by version: Windows 11 with a restored classic taskbar (ExplorerPatcher,
    // StartAllBack...) still has a visible ReBar + task list and needs the classic path.
    rebar_ = FindWindowExW(tray_, nullptr, L"ReBarWindow32", nullptr);
    taskList_ = rebar_ ? FindWindowExW(rebar_, nullptr, L"MSTaskSwWClass", nullptr) : nullptr;
    RECT listRect{};
    const bool classicVisible = taskList_ && IsWindowVisible(rebar_) && IsWindowVisible(taskList_) &&
                                GetWindowRect(taskList_, &listRect) && listRect.right > listRect.left &&
                                listRect.bottom > listRect.top;
    if (classicVisible) {
        kind_ = TaskbarKind::Classic;
        parent_ = rebar_;
        naturalTaskList_ = ToParent(rebar_, taskList_);
    } else if (WindowsBuild() >= kWindows11Build) {
        kind_ = TaskbarKind::Modern;
        parent_ = tray_;
        rebar_ = taskList_ = nullptr;
    } else {
        Log(L"taskbar not ready (no visible task list yet)");
        tray_ = rebar_ = taskList_ = notify_ = nullptr;
        return false;
    }

    // WS_EX_NOPARENTNOTIFY: no synchronous WM_PARENTNOTIFY round-trips to Explorer on every click.
    bar_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOPARENTNOTIFY, kBarClass, kControllerTitle,
                           WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1, parent_, nullptr, instance_, this);
    if (!bar_) {
        Log(L"CreateWindowEx(strip) failed: %lu", GetLastError());
        tray_ = rebar_ = taskList_ = notify_ = parent_ = nullptr;
        return false;
    }

    // Only the taskbar thread's own windows: Explorer file windows live in the same process.
    DWORD pid = 0;
    const DWORD thread = GetWindowThreadProcessId(tray_, &pid);
    hook_ = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, OnWinEvent, pid, thread,
                            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    Log(L"attached (%s taskbar)", kind_ == TaskbarKind::Classic ? L"classic" : L"modern");
    return true;
}

void App::RestoreTaskList() {
    if (kind_ != TaskbarKind::Classic || !shrunk_ || !IsWindow(rebar_) || !IsWindow(taskList_)) return;
    // Recompute the natural rectangle from the current ReBar layout (toolbars respected).
    logic::ClassicInput in;
    GetClientRect(rebar_, &in.rebarClient);
    in.taskList = ToParent(rebar_, taskList_);
    in.vertical = vertical_;
    for (HWND child = GetWindow(rebar_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (child != taskList_ && child != bar_ && IsWindowVisible(child))
            in.toolbars.push_back(ToParent(rebar_, child));
    }
    const RECT natural = logic::PlaceClassic(in).natural;
    SetWindowPos(taskList_, nullptr, natural.left, natural.top, natural.right - natural.left,
                 natural.bottom - natural.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void App::Detach(bool restoreTaskbar) {
    if (hook_) UnhookWinEvent(hook_), hook_ = nullptr;
    if (restoreTaskbar) RestoreTaskList();
    shrunk_ = false;
    g_crashRestore.active = false;

    if (cardWindow_) {
        KillTimer(controller_, kCardDelayTimer);
        KillTimer(controller_, kCardFadeTimer);
        ShowWindow(cardWindow_, SW_HIDE);
        cardVisible_ = cardWanted_ = false;
        cardAlpha_ = 0.0f;
    }
    if (bar_ && IsWindow(bar_)) DestroyWindow(bar_);
    bar_ = nullptr;
    barShown_ = hover_ = pressed_ = trackingMouse_ = false;
    tray_ = rebar_ = taskList_ = notify_ = parent_ = nullptr;
    taskListMoves_.clear();
    accessibleName_.clear();
}

bool App::IsAttached() const {
    return bar_ && IsWindow(bar_) && tray_ && IsWindow(tray_) && IsWindow(parent_) &&
           (kind_ == TaskbarKind::Modern || IsWindow(taskList_)) && FindWindowW(L"Shell_TrayWnd", nullptr) == tray_;
}

void App::RequestReposition() {
    if (repositionPending_ || !controller_) return;
    repositionPending_ = true;
    PostMessageW(controller_, WM_APP_REPOSITION, 0, 0);
}

void App::Reposition() {
    repositionPending_ = false;
    if (quitting_ || !snapshot_.valid || displayOff_ || locked_) return;
    if (!IsAttached()) {
        Detach(false);
        if (!Attach()) return;
    }

    const UINT dpi = std::max<UINT>(GetDpiForWindow(tray_), USER_DEFAULT_SCREEN_DPI);
    RECT trayRect{};
    GetWindowRect(tray_, &trayRect);
    vertical_ = kind_ == TaskbarKind::Classic && (trayRect.bottom - trayRect.top) > (trayRect.right - trayRect.left);

    RECT target{};
    if (kind_ == TaskbarKind::Classic) {
        logic::ClassicInput in;
        GetClientRect(rebar_, &in.rebarClient);
        in.taskList = ToParent(rebar_, taskList_);
        in.vertical = vertical_;
        for (HWND child = GetWindow(rebar_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            if (child != taskList_ && child != bar_ && IsWindowVisible(child)) {
                in.toolbars.push_back(ToParent(rebar_, child));
            }
        }
        // The strip is as thick as the task list row (handles multi-row taskbars too).
        layout_ = {dpi, vertical_,
                   vertical_ ? in.taskList.right - in.taskList.left : in.taskList.bottom - in.taskList.top};
        if (layout_.thicknessPx <= 0) return;
        RebuildItems();
        in.strip = renderer_.Layout(layout_, items_);
        const logic::ClassicPlacement placed = logic::PlaceClassic(in);
        naturalTaskList_ = placed.natural;

        if (!SameRect(in.taskList, placed.taskList)) {
            const ULONGLONG now = GetTickCount64();
            while (!taskListMoves_.empty() && now - taskListMoves_.front() > kTaskListMoveWindowMs) {
                taskListMoves_.pop_front();
            }
            if (taskListMoves_.size() < kMaxTaskListMoves) {
                taskListMoves_.push_back(now);
                const RECT& r = placed.taskList;
                SetWindowPos(taskList_, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                shrunk_ = true;
            } else {
                Log(L"task list keeps being reset by Explorer; backing off");
            }
        }
        g_crashRestore = {taskList_, naturalTaskList_, shrunk_};
        target = placed.strip;
    } else {
        logic::ModernInput in;
        GetClientRect(tray_, &in.trayClient);
        if (!IsWindow(notify_)) notify_ = FindWindowExW(tray_, nullptr, L"TrayNotifyWnd", nullptr);
        if (notify_ && IsWindowVisible(notify_)) in.notify = ToParent(tray_, notify_);
        layout_ = {dpi, false, in.trayClient.bottom - in.trayClient.top};
        if (layout_.thicknessPx <= 0) return;
        RebuildItems();
        in.strip = renderer_.Layout(layout_, items_);
        in.gapPx = MulDiv(kModernGapDip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
        in.offsetPx = MulDiv(settings_.offsetDip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
        in.fallbackRightPx = MulDiv(kModernFallbackRightDip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
        target = logic::PlaceModern(in);
    }

    const RECT current = ToParent(parent_, bar_);
    const bool moved = !SameRect(current, target);
    if (moved) {
        SetWindowPos(bar_, HWND_TOP, target.left, target.top, target.right - target.left, target.bottom - target.top,
                     SWP_NOACTIVATE);
    } else if (GetWindow(bar_, GW_HWNDPREV)) {
        // Explorer may re-order its children (e.g. the Windows 11 XAML bridge): stay on top.
        SetWindowPos(bar_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    Render(moved);
    if (!barShown_) {
        ShowWindow(bar_, SW_SHOWNA);
        barShown_ = true;
    }
    if (moved && cardVisible_) UpdateCard(true);  // the card follows the strip
}

// ---------------------------------------------------------------------------------------------------
// Data and drawing
// ---------------------------------------------------------------------------------------------------

void App::RebuildItems() {
    items_.clear();
    const auto& s = snapshot_;
    const std::wstring gb = S(i18n::Str::UnitGB);

    auto percent = [&](MetricId id, const wchar_t* label, double value) {
        ItemView item;
        item.id = id;
        item.label = label;
        item.strong = text::Percent(value);
        item.weak = L"%";
        item.worstStrong = L"100";
        item.worstWeak = L"%";
        item.load = static_cast<float>(value / 100.0);
        item.fraction = shown_[static_cast<int>(id)];
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
        item.fraction = shown_[static_cast<int>(id)];
        items_.push_back(std::move(item));
    };

    // Temperature badge on the label line ("54°"), colored when warm/hot.
    auto badge = [&](MetricId id, const std::optional<double>& celsius) {
        if (!settings_.showTemperatures || items_.empty()) return;
        // Once a metric has shown a temperature, its column keeps the room for it: the strip must not
        // grow and shrink whenever a reading is momentarily missing (e.g. an idle drive).
        bool& seen = badgeSeen_[static_cast<int>(id)];
        seen = seen || celsius.has_value();
        if (!seen) return;
        ItemView& item = items_.back();
        item.worstBadge = L"100°";
        if (!celsius) return;
        item.badge = std::to_wstring(static_cast<int>(std::lround(*celsius))) + L"°";
        item.badgeLevel = settings_.colorByLoad ? logic::TemperatureLevel(id, *celsius) : 0;
    };

    for (MetricId id : logic::VisibleMetrics(settings_, Available())) {
        switch (id) {
            case MetricId::Cpu:
                percent(id, L"CPU", s.cpu);
                badge(id, s.cpuTemperature);
                break;
            case MetricId::Ram: memory(id, L"RAM", s.ramUsed, s.ramTotal); break;
            case MetricId::Gpu:
                percent(id, L"GPU", s.gpu);
                badge(id, s.gpuTemperature);
                break;
            case MetricId::Vram: memory(id, L"VRAM", s.vramUsed, s.vramTotal); break;
            case MetricId::Disk: {
                percent(id, S(i18n::Str::StripDisk), s.diskActive);
                std::optional<double> hottest;
                for (const DiskSnapshot& disk : s.disks) {
                    if (disk.temperature && (!hottest || *disk.temperature > *hottest)) hottest = disk.temperature;
                }
                badge(id, hottest);
                break;
            }
        }
    }
}

logic::Availability App::Available() const {
    return {snapshot_.gpuAvailable, snapshot_.gpuAvailable && snapshot_.vramTotal > 0, snapshot_.diskAvailable};
}

void App::Render(bool force) {
    if (!bar_ || layout_.thicknessPx <= 0) return;
    RebuildItems();
    renderer_.Layout(layout_, items_);
    if (!force && !renderer_.NeedsPaint(items_, hover_)) return;  // nothing visible changed
    if (!renderer_.Paint(items_, hover_)) {
        // Render target recreated (rare): redo the layout and try once more.
        renderer_.Layout(layout_, items_);
        if (!renderer_.Paint(items_, hover_)) return;
    }
    renderer_.Present(bar_);
}

void App::UpdateAccessibleName() {
    // Screen readers (Narrator) read the window text of the strip.
    if (!bar_) return;
    std::wstring name = L"sys-bar";
    for (const ItemView& item : items_) name += L" · " + item.label + L" " + item.strong + item.weak;
    if (name != accessibleName_) {
        accessibleName_ = name;
        SetWindowTextW(bar_, name.c_str());
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

    target_[static_cast<int>(MetricId::Cpu)] = static_cast<float>(snapshot_.cpu / 100.0);
    target_[static_cast<int>(MetricId::Ram)] = text::Ratio(snapshot_.ramUsed, snapshot_.ramTotal);
    target_[static_cast<int>(MetricId::Gpu)] = static_cast<float>(snapshot_.gpu / 100.0);
    target_[static_cast<int>(MetricId::Vram)] = text::Ratio(snapshot_.vramUsed, snapshot_.vramTotal);
    target_[static_cast<int>(MetricId::Disk)] = static_cast<float>(snapshot_.diskActive / 100.0);

    for (int i = 0; i < kMetricCount; ++i) {
        history_[i].push_back(target_[i]);
        while (history_[i].size() > kHistory) history_[i].pop_front();
    }

    float largest = 0.0f;
    for (int i = 0; i < kMetricCount; ++i) largest = std::max(largest, std::fabs(target_[i] - shown_[i]));
    if (!AnimationsAllowed() || largest < kAnimationMinDelta) {
        shown_ = target_;
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
    for (int i = 0; i < kMetricCount; ++i) {
        const float delta = target_[i] - shown_[i];
        if (std::fabs(delta) < kAnimationSnap) {
            shown_[i] = target_[i];
        } else {
            shown_[i] += delta * kAnimationEase;
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
    const Theme theme = CurrentTheme();
    renderer_.SetTheme(theme);
    card_.SetTheme(theme);
    ApplyMenuTheme(light_ && !highContrast_);
    Render(true);
    if (cardVisible_) UpdateCard(false);
}

void App::ApplyLanguage() {
    i18n::Lang chosen;
    lang_ = i18n::FromCode(settings_.language, chosen) ? chosen : i18n::FromSystem();
    const Theme theme = CurrentTheme();
    renderer_.SetTheme(theme);
    card_.SetTheme(theme);
}

void App::SetPaused(bool paused) {
    sampler_.SetPaused(paused);
    if (paused) {
        KillTimer(controller_, kAnimationTimer);
        animating_ = false;
        shown_ = target_;
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
    model.window = text::HistoryWindow(static_cast<unsigned>(kHistory), settings_.intervalMs,
                                       S(i18n::Str::CardLastSeconds), S(i18n::Str::CardLastMinutes));
    model.hintLeft = settings_.clickOpensTaskManager ? S(i18n::Str::CardHintClick) : L"";
    model.hintRight = S(i18n::Str::CardHintMenu);
    model.capacity = static_cast<int>(kHistory);

    auto row = [&](MetricId id, const wchar_t* title) {
        CardRow r;
        r.id = id;
        r.title = title;
        const auto& h = history_[static_cast<int>(id)];
        r.history.assign(h.begin(), h.end());
        r.load = h.empty() ? 0.0f : h.back();
        return r;
    };
    auto peakOf = [](const CardRow& r) {
        return r.history.empty() ? 0.0f : *std::max_element(r.history.begin(), r.history.end());
    };
    auto percentRow = [&](MetricId id, const wchar_t* title, double value, std::wstring subtitle) {
        CardRow r = row(id, title);
        r.value = percentText(value);
        r.subtitle = std::move(subtitle);
        r.peak = text::Format(S(i18n::Str::CardPeak), {percentText(peakOf(r) * 100.0)});
        model.rows.push_back(std::move(r));
    };
    auto memoryRow = [&](MetricId id, const wchar_t* title, uint64_t used, uint64_t total) {
        CardRow r = row(id, title);
        r.value = gbText(text::GiB(used));
        r.subtitle = text::Format(S(i18n::Str::CardOfTotal),
                                  {gbText(text::GiB(total)), percentText(text::Ratio(used, total) * 100.0)});
        r.peak = text::Format(S(i18n::Str::CardPeak), {gbText(text::GiB(total) * peakOf(r))});
        model.rows.push_back(std::move(r));
    };

    percentRow(MetricId::Cpu, S(i18n::Str::CardCpu), s.cpu,
               cpuDisplayName_ + L" · " + text::Format(S(i18n::Str::CardThreads), {std::to_wstring(info.logicalCpus)}));
    memoryRow(MetricId::Ram, S(i18n::Str::CardMemory), s.ramUsed, s.ramTotal);
    if (s.gpuAvailable) {
        percentRow(MetricId::Gpu, S(i18n::Str::CardGpu), s.gpu, info.gpuName);
        if (s.vramTotal > 0) memoryRow(MetricId::Vram, S(i18n::Str::CardGpuMemory), s.vramUsed, s.vramTotal);
    }
    if (s.diskAvailable) {
        auto rate = [&](uint64_t bytesPerSecond) {
            return text::Fixed1(static_cast<double>(bytesPerSecond) / (1024.0 * 1024.0), decimal_) + L" " +
                   S(i18n::Str::UnitMBps);
        };
        percentRow(MetricId::Disk, S(i18n::Str::CardDisk), s.diskActive,
                   text::Format(S(i18n::Str::CardDiskIo), {rate(s.diskReadBps), rate(s.diskWriteBps)}));
    }

    // Sensors: one line per device, temperature and fan together ("54 °C · 576 RPM").
    for (const SensorReading& reading : s.sensors) {
        const MetricId group = reading.group == SensorGroup::Gpu    ? MetricId::Gpu
                               : reading.group == SensorGroup::Disk ? MetricId::Disk
                                                                    : MetricId::Cpu;
        auto line = std::find_if(model.sensors.begin(), model.sensors.end(),
                                 [&](const CardSensorLine& l) { return l.name == reading.name; });
        if (line == model.sensors.end()) {
            model.sensors.push_back({reading.name, L"", 0, group});
            line = model.sensors.end() - 1;
        }
        std::wstring value;
        if (reading.kind == SensorKind::Temperature) {
            value = std::to_wstring(static_cast<int>(std::lround(reading.value))) + L" °C";
            if (settings_.colorByLoad)
                line->level = std::max(line->level, logic::TemperatureLevel(group, reading.value));
        } else {
            value =
                text::Format(S(i18n::Str::CardRpm), {std::to_wstring(static_cast<int>(std::lround(reading.value)))});
        }
        line->value = line->value.empty() ? value : line->value + L" · " + value;
    }
    if (!model.sensors.empty()) model.sensorsTitle = S(i18n::Str::CardSensors);

    // Notice: a message the user asked for (e.g. "you have the latest version"), else update news.
    if (!notice_.empty() && GetTickCount64() < noticeUntil_) {
        model.notice = notice_;
    } else if (const auto release = updater_.latest()) {
        const std::wstring version = update::ToString(release->version);
        switch (updater_.state()) {
            case update::State::Available:
                model.notice = text::Format(S(i18n::Str::CardUpdateAvailable), {version});
                break;
            case update::State::Downloading:
            case update::State::Ready:
            case update::State::Installing:
                model.notice = text::Format(S(i18n::Str::UpdateDownloading), {version});
                break;
            default: break;
        }
    }
    return model;
}

void App::PresentCard() {
    const float eased = EaseOutCubic(cardAlpha_);
    const POINT at{cardPosition_.x + static_cast<LONG>(std::lround(cardSlide_.x * (1.0f - eased))),
                   cardPosition_.y + static_cast<LONG>(std::lround(cardSlide_.y * (1.0f - eased)))};
    card_.Present(cardWindow_, at, static_cast<BYTE>(std::lround(255.0f * eased)));
}

void App::UpdateCard(bool reposition) {
    if (!cardWindow_ || !bar_) return;
    const CardModel model = BuildCardModel();
    const SIZE size = card_.Measure(layout_.dpi, model);
    if (!card_.Paint(model)) {
        Log(L"card paint failed");
        return;
    }

    if (reposition) {
        logic::CardInput in;
        GetWindowRect(bar_, &in.strip);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(bar_, MONITOR_DEFAULTTONEAREST), &mi);
        in.monitor = mi.rcMonitor;
        in.card = size;
        in.marginPx = card_.ShadowMarginPx();
        const int dpi = static_cast<int>(layout_.dpi);
        in.gapPx = MulDiv(kCardGapDip, dpi, USER_DEFAULT_SCREEN_DPI);
        in.slidePx = MulDiv(kCardSlideDip, dpi, USER_DEFAULT_SCREEN_DPI);
        in.verticalTaskbar = vertical_;
        const logic::CardPlacement placed = logic::PlaceCard(in);
        cardPosition_ = placed.position;
        cardSlide_ = placed.slide;
    }
    PresentCard();
}

void App::ShowCard() {
    if (!cardWindow_ || !bar_ || !snapshot_.valid) return;
    cardWanted_ = true;
    if (!cardVisible_) {
        cardAlpha_ = AnimationsAllowed() ? 0.0f : 1.0f;
        UpdateCard(true);
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
                ShowNotice(state == State::Available
                               ? text::Format(S(i18n::Str::CardUpdateAvailable), {version})
                               : text::Format(S(i18n::Str::UpdateUpToDate), {SYSBAR_VERSION_WTEXT}));
            }
            break;
        case State::Ready: MaybeInstallUpdate(); break;
        case State::Failed:
            Log(L"update failed");
            if (manualUpdateCheck_ || installWhenReady_) ShowNotice(S(i18n::Str::UpdateFailed));
            manualUpdateCheck_ = installWhenReady_ = false;
            break;
        default: break;
    }
    if (cardVisible_) UpdateCard(false);
}

// Installs a downloaded update at a quiet moment: never while the user is looking at the card.
void App::MaybeInstallUpdate() {
    if (!installWhenReady_ || updater_.state() != update::State::Ready || hover_ || cardVisible_) return;
    Log(L"installing update");
    installWhenReady_ = false;
    updater_.Install();  // the installer closes us cleanly and starts the new version
}

void App::ShowNotice(std::wstring text) {
    notice_ = std::move(text);
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
// Menu and commands
// ---------------------------------------------------------------------------------------------------

void App::ShowMenu(POINT screen) {
    HideCard();
    HMENU menu = CreatePopupMenu();
    HMENU intervals = CreatePopupMenu();
    HMENU languages = CreatePopupMenu();
    HMENU updates = CreatePopupMenu();
    if (!menu || !intervals || !languages) {
        if (menu) DestroyMenu(menu);
        if (intervals) DestroyMenu(intervals);
        if (languages) DestroyMenu(languages);
        if (updates) DestroyMenu(updates);
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
    AppendMenuW(menu, check(settings_.showTemperatures), kCmdShowTemperatures, S(i18n::Str::MenuTemperatures));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

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
    if (updates) {
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

    AppendMenuW(menu, check(settings_.colorByLoad), kCmdColorByLoad, S(i18n::Str::MenuHighlight));
    AppendMenuW(menu, check(settings_.clickOpensTaskManager), kCmdClickOpensTaskManager, S(i18n::Str::MenuClickOpens));
    AppendMenuW(menu, check(Settings::IsAutostartEnabled()), kCmdAutostart, S(i18n::Str::MenuAutostart));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdTaskManager, S(i18n::Str::MenuTaskManager));
    AppendMenuW(menu, MF_STRING, kCmdAbout, text::Format(S(i18n::Str::MenuAbout), {SYSBAR_VERSION_WTEXT}).c_str());
    AppendMenuW(menu, MF_STRING, kCmdExit, S(i18n::Str::MenuExit));

    // The menu needs a foreground window to close properly when the user clicks elsewhere.
    SetForegroundWindow(controller_);
    TPMPARAMS params{sizeof(params)};
    if (tray_) GetWindowRect(tray_, &params.rcExclude);
    const UINT command = static_cast<UINT>(TrackPopupMenuEx(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY | TPM_VERTICAL, screen.x, screen.y, controller_, &params));
    PostMessageW(controller_, WM_NULL, 0, 0);
    DestroyMenu(menu);  // destroys the submenus too

    // The pointer may have left the strip while the menu was open.
    POINT cursor{};
    if (bar_ && GetCursorPos(&cursor) && WindowFromPoint(cursor) != bar_ && hover_) {
        hover_ = trackingMouse_ = pressed_ = false;
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
                for (auto& h : history_) h.clear();  // the graph's time scale changed: start over
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
    Detach(true);
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
    if (!app || !hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF || hwnd == app->bar_) return;
    if (hwnd == app->taskList_ || hwnd == app->rebar_ || hwnd == app->tray_ || hwnd == app->notify_ ||
        (app->rebar_ && GetParent(hwnd) == app->rebar_)) {  // toolbars moving inside the ReBar
        app->RequestReposition();
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
        // Explorer restarted (or re-broadcast this message): rebuild only if our taskbar is gone.
        if (!IsAttached()) Detach(false);
        Reposition();
        return 0;
    }

    switch (msg) {
        case WM_APP_SAMPLE: OnSample(); return 0;
        case WM_APP_REPOSITION: Reposition(); return 0;
        case WM_APP_UPDATE: OnUpdateState(static_cast<update::State>(wp)); return 0;
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
                    if (hover_) ShowCard();
                    break;
                case kCardFadeTimer: OnCardFadeTick(); break;
                case kUpdateTimer: OnUpdateTimer(); break;
                case kNoticeTimer:
                    KillTimer(hwnd, kNoticeTimer);
                    if (!hover_) HideCard();
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
                Detach(false);
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
        case WM_MOUSEMOVE:
            if (!trackingMouse_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                trackingMouse_ = TrackMouseEvent(&tme) != FALSE;
            }
            if (!hover_) {
                hover_ = true;
                Render();
                SetTimer(controller_, kCardDelayTimer, kCardDelayMs, nullptr);
            }
            return 0;
        case WM_MOUSELEAVE:
            trackingMouse_ = pressed_ = false;
            HideCard();
            if (hover_) {
                hover_ = false;
                Render();
            }
            return 0;
        case WM_LBUTTONDOWN:
            pressed_ = true;
            HideCard();
            return 0;
        case WM_RBUTTONDOWN: HideCard(); return 0;
        case WM_LBUTTONUP:
            // Only a click that started on the strip counts (not a drag released over it).
            if (pressed_ && settings_.clickOpensTaskManager) ShellOpen(L"taskmgr.exe");
            pressed_ = false;
            return 0;
        case WM_RBUTTONUP: {
            POINT pt{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
            ClientToScreen(hwnd, &pt);
            ShowMenu(pt);
            return 0;
        }
        case WM_DPICHANGED_AFTERPARENT: RequestReposition(); return 0;
        case WM_NCDESTROY:
            if (bar_ == hwnd) {
                bar_ = nullptr;
                barShown_ = hover_ = pressed_ = trackingMouse_ = false;
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
