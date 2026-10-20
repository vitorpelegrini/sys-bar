#pragma once

#include "common.h"

#include <string>

// User preferences, stored in %APPDATA%\sys-bar\settings.ini
// (or in %SYSBAR_CONFIG_DIR%\settings.ini, used by the tests).
struct Settings {
    static constexpr DWORD kMinIntervalMs = 250;
    static constexpr DWORD kMaxIntervalMs = 10000;
    static constexpr int kMaxOffsetDip = 2000;

    bool showCpu = true;
    bool showRam = true;
    bool showGpu = true;
    bool showVram = true;
    bool showDisk = true;
    bool showTemperatures = true;  // temperature badges in the strip
    bool colorByLoad = true;
    bool clickOpensTaskManager = true;
    DWORD intervalMs = 1000;
    std::wstring language = L"auto";  // "auto" or an i18n code ("en", "pt", "zh-Hans", ...)
    // Extra space (DIPs) kept between the strip and the notification area. Lets users with
    // widgets or many tray icons nudge the strip; 0 = automatic placement only.
    int offsetDip = 0;

    void Load();
    void Save() const;

    static std::wstring FilePath();
    static bool IsAutostartEnabled();
    static bool SetAutostart(bool enable);
};
