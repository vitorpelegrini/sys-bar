#pragma once

#include "common.h"
#include "gfx.h"

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
// Metrics shown in the strip, never empty: when every enabled metric is unavailable, CPU is shown.
std::vector<MetricId> VisibleMetrics(const Settings& settings, bool gpuAvailable, bool vramAvailable);
// Whether `id` may be turned off without leaving the strip empty.
bool CanHide(const Settings& settings, MetricId id, bool gpuAvailable, bool vramAvailable);

}  // namespace logic
