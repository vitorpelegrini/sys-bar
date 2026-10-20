#pragma once

#include "common.h"

// Measures the taskbar's own controls on its right edge (clock, tray icons, "show desktop") on
// taskbars that have no HWND for them: Windows 11 and the taskbars on other monitors.
namespace trayprobe {

// Width in pixels of the right-edge control cluster of `tray`, or -1 when it cannot be measured.
// Blocking (cross-process UI Automation, tens to hundreds of milliseconds): call it off the UI thread.
int MeasureRightControls(HWND tray);

}  // namespace trayprobe
