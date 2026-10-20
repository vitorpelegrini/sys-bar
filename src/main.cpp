#include "app.h"

#include <shellapi.h>

#include <cwchar>

namespace {

// `sys-bar.exe --quit`: asks a running instance to exit cleanly (it restores the taskbar layout)
// and waits for it. Used by the installer/uninstaller; returns 0 when nothing is left running.
int QuitRunningInstance() {
    HWND controller = FindWindowW(kControllerClass, kControllerTitle);
    if (!controller) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(controller, &pid);
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    PostMessageW(controller, WM_CLOSE, 0, 0);
    if (process) {
        const DWORD result = WaitForSingleObject(process, 10000);
        CloseHandle(process);
        return result == WAIT_OBJECT_0 ? 0 : 1;
    }
    // Cannot wait on the process (e.g. it runs elevated): at least wait for its window to go away, and
    // never report success while it is still there.
    for (int i = 0; i < 100 && FindWindowW(kControllerClass, kControllerTitle); ++i) Sleep(100);
    return FindWindowW(kControllerClass, kControllerTitle) ? 1 : 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    // DPI awareness (Per-Monitor V2) comes from the manifest.
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(commandLine, &argc)) {
        for (int i = 0; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--quit") == 0 || _wcsicmp(argv[i], L"/quit") == 0) {
                LocalFree(argv);
                return QuitRunningInstance();
            }
        }
        LocalFree(argv);
    }

    // One instance per user session.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\SysBar.SingleInstance");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    int result = 0;
    {
        App app;
        result = app.Run(instance);
    }

    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return result;
}
