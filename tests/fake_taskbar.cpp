// A stand-in for a taskbar on another monitor, for the multi-monitor integration test
// (tools/test-taskbars.ps1). It builds the same window structure Explorer does, with the same class
// names, off screen so it never covers anything:
//   classic (Windows 10):  Shell_SecondaryTrayWnd > WorkerW > MSTaskListWClass
//   modern  (Windows 11):  Shell_SecondaryTrayWnd with plain child windows standing in for the XAML
//                          controls UI Automation reports (task buttons in the middle, clock on the right).
// Like Explorer, the classic one puts its task list back to full width on every layout pass.
#include "fake_taskbar.h"

#include <cstdio>
#include <string>

namespace {

constexpr int kWidth = 1600;
constexpr int kHeight = 40;
constexpr int kX = -6000;  // off every monitor
constexpr int kY = -3000;
constexpr UINT_PTR kRelayoutTimer = 1;

HWND g_workerW = nullptr;
HWND g_taskList = nullptr;

void Relayout() {
    // Explorer's own layout pass: task list from after the start button to before the clock.
    if (g_taskList) SetWindowPos(g_taskList, nullptr, 48, 0, 1450 - 48, kHeight, SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK TrayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            if (wp == kRelayoutTimer) Relayout();
            return 0;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND Make(const wchar_t* cls, DWORD style, int x, int y, int w, int h, HWND parent, DWORD exStyle = 0) {
    return CreateWindowExW(exStyle, cls, cls, style, x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

}  // namespace

int RunFakeTaskbar(const std::wstring& kind, int seconds) {
    WNDCLASSW tray{};
    tray.lpfnWndProc = TrayProc;
    tray.hInstance = GetModuleHandleW(nullptr);
    tray.lpszClassName = L"Shell_SecondaryTrayWnd";
    RegisterClassW(&tray);
    for (const wchar_t* name : {L"WorkerW", L"MSTaskListWClass"}) {
        WNDCLASSW c{};
        c.lpfnWndProc = DefWindowProcW;
        c.hInstance = GetModuleHandleW(nullptr);
        c.lpszClassName = name;
        RegisterClassW(&c);
    }

    HWND window = Make(L"Shell_SecondaryTrayWnd", WS_POPUP | WS_VISIBLE, kX, kY, kWidth, kHeight, nullptr,
                       WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    if (!window) return 2;
    if (kind == L"classic") {
        g_workerW = Make(L"WorkerW", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 1450, kHeight, window);
        g_taskList = Make(L"MSTaskListWClass", WS_CHILD | WS_VISIBLE, 48, 0, 1450 - 48, kHeight, g_workerW);
        SetTimer(window, kRelayoutTimer, 3000, nullptr);
    } else {
        // Centered task buttons, then the clock and the notification button against the right edge.
        for (int x = 700; x < 900; x += 40) Make(L"BUTTON", WS_CHILD | WS_VISIBLE, x, 0, 40, kHeight, window);
        Make(L"BUTTON", WS_CHILD | WS_VISIBLE, 1480, 0, 96, kHeight, window);  // clock
        Make(L"BUTTON", WS_CHILD | WS_VISIBLE, 1578, 0, 22, kHeight, window);  // notifications
    }
    std::printf("fake %ls taskbar %p ready\n", kind.c_str(), static_cast<void*>(window));
    std::fflush(stdout);
    SetTimer(nullptr, 0, static_cast<UINT>(seconds) * 1000, [](HWND, UINT, UINT_PTR, DWORD) { PostQuitMessage(0); });

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (IsWindow(window)) DestroyWindow(window);
    return 0;
}
