#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// Private messages between the sampling thread and the controller window.
constexpr UINT WM_APP_SAMPLE = WM_APP + 1;
constexpr UINT WM_APP_REPOSITION = WM_APP + 2;
constexpr UINT WM_APP_UPDATE = WM_APP + 3;    // wParam = update::State
constexpr UINT WM_APP_SENSORS = WM_APP + 4;   // wParam = 1 install / 0 uninstall, lParam = exit code
constexpr UINT WM_APP_TRAYEDGE = WM_APP + 5;  // wParam = taskbar HWND, lParam = right controls width (-1 unknown)

// Window identity, also used by `sys-bar.exe --quit` and the installer to find a running instance.
constexpr wchar_t kControllerClass[] = L"SysBar.Controller";
constexpr wchar_t kControllerTitle[] = L"sys-bar";
