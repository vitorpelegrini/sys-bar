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

// Window identity, also used by `sys-bar.exe --quit` and the installer to find a running instance.
constexpr wchar_t kControllerClass[] = L"SysBar.Controller";
constexpr wchar_t kControllerTitle[] = L"sys-bar";
