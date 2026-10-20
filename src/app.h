#pragma once

#include "card.h"
#include "common.h"
#include "gfx.h"
#include "i18n.h"
#include "logic.h"
#include "metrics.h"
#include "renderer.h"
#include "settings.h"
#include "update.h"

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Owns the application: a hidden controller window (receives system broadcasts and timers), the
// strip embedded in the taskbar, the hover card, the sampler, theme, language and context menu.
class App {
public:
    App() = default;
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    int Run(HINSTANCE instance);

private:
    static constexpr size_t kHistory = 60;

    enum class TaskbarKind { Classic, Modern };

    // Animated bar, history graph and badge memory of one strip column or card row. Keyed by the
    // metric (MetricId) or, for the per-disk columns, by kDiskKeyBase + the physical disk number.
    struct Track {
        float shown = 0.0f;   // drawn value (animated)
        float target = 0.0f;  // latest sample
        std::deque<float> history;
        bool badgeSeen = false;  // has shown a temperature at least once
    };
    static constexpr int kDiskKeyBase = 100;
    static int Key(MetricId id) { return static_cast<int>(id); }

    // One strip per taskbar: the main one and, optionally, those on other monitors.
    struct Taskbar {
        HWND tray = nullptr;       // Shell_TrayWnd / Shell_SecondaryTrayWnd
        HWND container = nullptr;  // classic: ReBarWindow32 (main) / WorkerW (other monitors)
        HWND taskList = nullptr;   // classic: MSTaskSwWClass (main) / MSTaskListWClass (other monitors)
        HWND notify = nullptr;     // TrayNotifyWnd (main taskbar only)
        HWND parent = nullptr;     // where the strip is inserted
        HWND bar = nullptr;        // the strip
        TaskbarKind kind = TaskbarKind::Classic;
        bool primary = false;
        bool vertical = false;
        bool shrunk = false;  // the task list was made smaller for us
        bool shown = false;
        bool tracking = false;  // TrackMouseEvent armed
        RECT natural{};         // task list rectangle without us (container coordinates)
        std::deque<ULONGLONG> moves;
        LayoutInput layout{};
        std::unique_ptr<Renderer> renderer;
        // Modern taskbars without a notification-area HWND: width of the right-side controls (UI Automation).
        int reservePx = -1;  // -1 = unknown
        int probedWidth = 0;
        bool probing = false;
        ULONGLONG probeStarted = 0;
    };
    static constexpr size_t kMaxTaskbars = 8;

    static LRESULT CALLBACK ControllerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK BarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static void CALLBACK OnWinEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                    DWORD thread, DWORD time);

    LRESULT HandleController(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleBar(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    // Taskbar integration
    Taskbar* Attach(HWND tray, bool primary);  // nullptr when that taskbar is not ready
    void Detach(Taskbar& t, bool restoreTaskbar);
    void DetachAll(bool restoreTaskbar);
    bool IsAttached(const Taskbar& t) const;
    void SyncTaskbars();
    Taskbar* FindBar(HWND bar);
    Taskbar* CardBar();  // the strip the card belongs to: hovered, else the notice's, else the main one
    void RequestReposition();
    void Reposition();
    bool RepositionBar(Taskbar& t);  // true when the strip moved
    void RestoreTaskList(Taskbar& t);
    void ProbeTrayEdge(Taskbar& t);
    void UpdateCrashRestore();

    // Data and drawing
    void Render(bool force = false);
    void RenderBar(Taskbar& t, bool force);
    void OnSample();
    void OnAnimationTick();
    void RebuildItems();
    logic::Availability Available() const;
    void RefreshTheme(bool force);
    Theme CurrentTheme() const;
    void ApplyLanguage();
    const wchar_t* S(i18n::Str id) const { return i18n::Get(lang_, id); }
    const wchar_t* RateUnit(int unit) const;               // KB/s, MB/s, GB/s
    std::wstring RateText(uint64_t bytesPerSecond) const;  // "4,2 MB/s" in the current language
    std::wstring FanRoleName(FanRole role, const std::wstring& name) const;
    void UpdateAccessibleName();
    void SetPaused(bool paused);
    bool AnimationsAllowed() const;

    // Hover card
    CardModel BuildCardModel() const;
    void ShowCard();
    void HideCard();
    bool UpdateCard(bool reposition);
    void PresentCard();
    void OnCardFadeTick();

    // Updates
    void OnUpdateTimer();
    void OnUpdateState(update::State state);
    void MaybeInstallUpdate();
    void ShowNotice(std::wstring text);
    std::wstring UpdateStatusText() const;
    void FooterUpdateStatus(CardModel& model) const;

    // Advanced sensors (optional service)
    std::wstring SensorsTool() const;  // path of sys-bar-sensors.exe next to us, or empty
    void RunSensorsTool(bool install);

    // Menu and commands
    void ShowMenu(HWND bar, POINT screen);
    void Execute(UINT command);
    void Quit();

    HINSTANCE instance_ = nullptr;
    HWND controller_ = nullptr;
    HWND cardWindow_ = nullptr;
    HPOWERNOTIFY displayNotify_ = nullptr;

    // Explorer windows.
    std::vector<std::unique_ptr<Taskbar>> bars_;
    std::unordered_map<DWORD, HWINEVENTHOOK> hooks_;  // per taskbar thread
    UINT taskbarCreated_ = 0;
    HWND hoverBar_ = nullptr;     // strip under the mouse (the card belongs to it)
    HWND pressedBar_ = nullptr;   // strip where the left button went down
    HWND noticeBar_ = nullptr;    // strip a notice is shown above when the mouse is elsewhere
    bool inTaskbarWork_ = false;  // Reposition/DetachAll running: re-entrant calls are deferred
    SIZE cardSize_{};             // last card size: a change moves the card
    bool animating_ = false;
    bool repositionPending_ = false;
    bool quitting_ = false;
    bool displayOff_ = false;
    bool locked_ = false;

    // Hover card state.
    bool cardVisible_ = false;
    bool cardWanted_ = false;
    float cardAlpha_ = 0.0f;
    POINT cardPosition_{};
    POINT cardSlide_{};

    // Declaration order matters: the renderers and card_ keep a reference to gfx_.
    Gfx gfx_;
    Card card_{gfx_};
    Theme theme_{};
    Settings settings_;
    MetricsSampler sampler_;
    update::Updater updater_;
    bool manualUpdateCheck_ = false;  // the user asked: report the result
    bool installWhenReady_ = false;   // the user asked to install (or auto-update is on)
    std::wstring notice_;             // transient message shown in the card
    ULONGLONG noticeUntil_ = 0;
    MetricsSnapshot snapshot_;
    i18n::Lang lang_ = i18n::Lang::English;
    bool light_ = false;
    bool highContrast_ = false;
    std::map<int, Track> tracks_;
    std::deque<double> netHistory_;       // bytes/s (busier direction), for the card's self-scaling graph
    std::vector<std::wstring> menuFans_;  // fans listed in the open context menu (index -> name)
    std::vector<ItemView> items_;
    std::wstring cpuDisplayName_;
    std::wstring accessibleName_;
    wchar_t decimal_ = L'.';
};
