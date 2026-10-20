#pragma once

#include "common.h"

#include <cstdint>
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
    bool showNet = false;                    // download/upload (off by default: the strip stays compact)
    bool showBattery = true;                 // only ever shown on machines with a battery
    bool showTemperatures = true;            // temperature badges in the strip
    std::wstring temperatureUnit = L"auto";  // "auto" (from the region), "C" or "F"
    bool disksSeparate = true;               // one strip column per physical disk (else one combined)
    std::wstring fanRoles;                   // user corrections: "Fan #1=case;Fan #2=cpu"
    bool colorByLoad = true;
    bool clickOpensTaskManager = true;
    bool allTaskbars = true;  // also on the taskbars of other monitors
    DWORD intervalMs = 1000;
    std::wstring language = L"auto";  // "auto" or an i18n code ("en", "pt", "zh-Hans", ...)
    // Extra space (DIPs) kept between the strip and the notification area. Lets users with
    // widgets or many tray icons nudge the strip; 0 = automatic placement only.
    int offsetDip = 0;
    bool checkUpdates = true;     // look for new releases once a day
    bool autoUpdate = true;       // download, verify and install them silently (installed copies)
    int64_t lastUpdateCheck = 0;  // Unix time of the last successful check

    void Load();
    void Save() const;

    // Whether temperatures are shown in Fahrenheit (explicit choice, else the Windows region).
    bool Fahrenheit() const;
    static bool RegionUsesFahrenheit();

    static std::wstring FilePath();
    static bool IsAutostartEnabled();
    static bool SetAutostart(bool enable);
};
