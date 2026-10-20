#pragma once

#include "common.h"
#include "gfx.h"
#include "hardware.h"

#include <string>
#include <vector>

struct Settings;

// Pure decision logic (geometry and visibility), kept free of window handles so it is fully
// unit-tested. All rectangles are in pixels.
namespace logic {

// ---- Classic taskbar (Windows 10, or Windows 11 with a restored classic taskbar) ----------------
// The strip lives inside ReBarWindow32, right after the task list (MSTaskSwWClass), which is
// shrunk to make room. Other toolbars in the ReBar ("Desktop", "Links", ...) are respected.
struct ClassicInput {
    RECT rebarClient{};          // ReBar client area (ReBar coordinates)
    RECT taskList{};             // task list (ReBar coordinates)
    std::vector<RECT> toolbars;  // other visible ReBar children (ReBar coordinates)
    SIZE strip{};                // desired strip size; the thickness comes from the task list
    bool vertical = false;
};

struct ClassicPlacement {
    RECT strip{};
    RECT taskList{};  // new task list rectangle
    RECT natural{};   // task list rectangle without us (used to restore it on exit)
};

ClassicPlacement PlaceClassic(const ClassicInput& in);

// ---- Modern taskbar (Windows 11) --------------------------------------------------------------
// The strip is a child of Shell_TrayWnd, right-aligned against the notification area.
struct ModernInput {
    RECT trayClient{};        // Shell_TrayWnd client area
    RECT notify{};            // TrayNotifyWnd (tray coordinates); may be empty or bogus
    SIZE strip{};             // desired strip size
    int gapPx = 0;            // space between the strip and the notification area
    int offsetPx = 0;         // user offset (settings)
    int fallbackRightPx = 0;  // distance from the right edge when the notification area is unknown
};

// True when the notification-area rectangle looks real (non-empty, on the right side of the bar).
bool IsPlausibleNotifyRect(const RECT& trayClient, const RECT& notify);
RECT PlaceModern(const ModernInput& in);

// Taskbars without a notification-area HWND (Windows 11, other monitors): width of the cluster of
// controls touching the right edge (clock, tray icons, "show desktop"), from the bounding rectangles UI
// Automation reports; elements closer than `maxGapPx` belong to the same cluster. -1 when there is none
// or it looks wrong (more than half the taskbar: the task buttons were reached).
constexpr int kTrayClusterGapDip = 12;
int RightControlsWidth(const RECT& tray, const std::vector<RECT>& elements, int maxGapPx);

// ---- Hover card --------------------------------------------------------------------------------
struct CardInput {
    RECT strip{};    // strip window (screen)
    RECT monitor{};  // monitor containing the strip (screen)
    SIZE card{};     // card bitmap size, shadow margin included
    int marginPx = 0;
    int gapPx = 0;
    int slidePx = 0;
    bool verticalTaskbar = false;
};

struct CardPlacement {
    POINT position{};  // final window position
    POINT slide{};     // starting offset of the entrance animation
};

CardPlacement PlaceCard(const CardInput& in);

// ---- Visible metrics ---------------------------------------------------------------------------
// Which metrics the current machine can provide.
struct Availability {
    bool gpu = false;
    bool vram = false;
    bool disk = false;
    bool net = false;
    bool battery = false;
};

// Metrics shown in the strip, never empty: when every enabled metric is unavailable, CPU is shown.
std::vector<MetricId> VisibleMetrics(const Settings& settings, const Availability& available);
// Whether `id` may be turned off without leaving the strip empty.
bool CanHide(const Settings& settings, MetricId id, const Availability& available);

// ---- Temperatures -----------------------------------------------------------------------------
// 0 = normal, 1 = warm (amber), 2 = hot (red). Processors (CPU/GPU) warm at 85 °C and are hot at
// 95 °C; drives warm at 60 °C and are hot at 70 °C.
int TemperatureLevel(MetricId id, double celsius);
// Number of a generic fan name: "Fan #2" -> "2"; other names are returned unchanged.
std::wstring FanNumber(const std::wstring& name);
// User corrections of fan roles, stored as "Fan #1=case;Fan #2=cpu" (names without '=' or ';').
std::vector<std::pair<std::wstring, FanRole>> ParseFanRoles(const std::wstring& text);
std::wstring FormatFanRoles(const std::vector<std::pair<std::wstring, FanRole>>& roles);
// Sets (or, with FanRole::Unknown, removes) the correction for `name`.
std::wstring WithFanRole(const std::wstring& text, const std::wstring& name, FanRole role);
// The correction for `name`, or `detected` when there is none.
FanRole EffectiveFanRole(const std::wstring& text, const std::wstring& name, FanRole detected);

// Fill of the card's temperature gauge, 0..1: 20 °C empty, full at 100 °C (processors) or 80 °C (drives).
float TemperatureGauge(MetricId id, double celsius);

// ---- Disks ------------------------------------------------------------------------------------
// Separate strip columns (and card rows) per physical disk, when the user wants them and there are
// between 2 and kMaxDiskColumns drives; more than that would crowd the taskbar, so they are combined.
constexpr size_t kMaxDiskColumns = 4;
bool SeparateDisks(const Settings& settings, size_t diskCount);

// Strip label of one disk: its kind and first drive letter ("NVMe C:", "SSD D:"). Unknown kinds use
// `fallback` (the translated "DISK"); a disk without letters is identified by its number ("HDD 2").
std::wstring DiskStripLabel(hw::DiskKind kind, const std::wstring& letters, int number, const wchar_t* fallback);

}  // namespace logic
