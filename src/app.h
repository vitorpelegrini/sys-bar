#pragma once

#include "card.h"
#include "common.h"
#include "gfx.h"
#include "i18n.h"
#include "logic.h"
#include "metrics.h"
#include "renderer.h"
#include "settings.h"

#include <array>
#include <deque>
#include <string>
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

    static LRESULT CALLBACK ControllerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK BarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static void CALLBACK OnWinEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                    DWORD thread, DWORD time);

    LRESULT HandleController(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleBar(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    // Taskbar integration
    bool Attach();
    void Detach(bool restoreTaskbar);
    bool IsAttached() const;
    void RequestReposition();
    void Reposition();
    void RestoreTaskList();

    // Data and drawing
    void Render(bool force = false);
    void OnSample();
    void OnAnimationTick();
    void RebuildItems();
    logic::Availability Available() const;
    void RefreshTheme(bool force);
    Theme CurrentTheme() const;
    void ApplyLanguage();
    const wchar_t* S(i18n::Str id) const { return i18n::Get(lang_, id); }
    void UpdateAccessibleName();
    void SetPaused(bool paused);
    bool AnimationsAllowed() const;

    // Hover card
    CardModel BuildCardModel() const;
    void ShowCard();
    void HideCard();
    void UpdateCard(bool reposition);
    void PresentCard();
    void OnCardFadeTick();

    // Menu and commands
    void ShowMenu(POINT screen);
    void Execute(UINT command);
    void Quit();

    HINSTANCE instance_ = nullptr;
    HWND controller_ = nullptr;
    HWND bar_ = nullptr;
    HWND cardWindow_ = nullptr;
    HPOWERNOTIFY displayNotify_ = nullptr;

    // Explorer windows.
    TaskbarKind kind_ = TaskbarKind::Classic;
    HWND tray_ = nullptr;      // Shell_TrayWnd
    HWND rebar_ = nullptr;     // ReBarWindow32 (classic)
    HWND taskList_ = nullptr;  // MSTaskSwWClass (classic)
    HWND notify_ = nullptr;    // TrayNotifyWnd
    HWND parent_ = nullptr;    // where our strip is inserted
    HWINEVENTHOOK hook_ = nullptr;
    UINT taskbarCreated_ = 0;
    RECT naturalTaskList_{};  // task list rectangle without us (ReBar coordinates)
    std::deque<ULONGLONG> taskListMoves_;

    bool vertical_ = false;
    bool shrunk_ = false;
    bool barShown_ = false;
    bool hover_ = false;
    bool pressed_ = false;
    bool trackingMouse_ = false;
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

    // Declaration order matters: renderer_ and card_ keep a reference to gfx_.
    Gfx gfx_;
    Renderer renderer_{gfx_};
    Card card_{gfx_};
    Settings settings_;
    MetricsSampler sampler_;
    MetricsSnapshot snapshot_;
    LayoutInput layout_{};
    i18n::Lang lang_ = i18n::Lang::English;
    bool light_ = false;
    bool highContrast_ = false;
    std::array<float, kMetricCount> shown_{};
    std::array<float, kMetricCount> target_{};
    std::array<std::deque<float>, kMetricCount> history_;
    std::vector<ItemView> items_;
    std::wstring cpuDisplayName_;
    std::wstring accessibleName_;
    wchar_t decimal_ = L'.';
};
