#pragma once

#include <string>
#include <string_view>

// User-interface strings in 14 languages. The language follows Windows by default and can be
// overridden from the context menu. Strings may contain {0}/{1} placeholders (see text::Format).
namespace i18n {

enum class Lang {
    English,
    Portuguese,
    Spanish,
    French,
    German,
    Italian,
    Polish,
    Turkish,
    Russian,
    Ukrainian,
    Japanese,
    Korean,
    ChineseSimplified,
    ChineseTraditional,
    Count,
};

enum class Str {
    MenuCpu,
    MenuRam,
    MenuGpu,
    MenuVram,
    MenuInterval,
    MenuSeconds,  // "{0} s"
    MenuHighlight,
    MenuClickOpens,
    MenuAutostart,
    MenuLanguage,
    MenuLanguageAuto,
    MenuTaskManager,
    MenuAbout,  // "About sys-bar {0}"
    MenuExit,
    CardTitle,
    CardCpu,
    CardMemory,
    CardGpu,
    CardGpuMemory,
    CardOfTotal,  // "of {0} · {1} in use"
    CardPeak,     // "max. {0}"
    CardThreads,  // "{0} threads"
    CardLastSeconds,
    CardLastMinutes,
    CardHintClick,
    CardHintMenu,
    UnitGB,
    PercentFormat,  // "{0}%"
    StripDisk,      // strip label, short ("DISK")
    MenuDisk,
    MenuTemperatures,
    CardDisk,
    CardDiskIo,  // "Read {0} · Write {1}"
    UnitMBps,
    CardSensors,
    CardRpm,  // "{0} RPM"
    MenuUpdates,
    MenuCheckUpdates,
    MenuAutoUpdate,
    MenuUpdateInstall,   // "Install version {0}"
    MenuUpdateDownload,  // "Download version {0}" (portable copies)
    UpdateUpToDate,      // "You have the latest version ({0})"
    UpdateChecking,
    UpdateDownloading,  // "Downloading version {0}…"
    UpdateFailed,
    CardUpdateAvailable,  // "Version {0} available"
    MenuAdvancedSensors,
    MenuAdvancedSensorsActive,
    CardSensorsHint,
    MenuTemperatureUnit,
    MenuTemperatureAuto,
    MenuDisks,
    MenuDisksEach,
    MenuDisksCombined,
    CardFanCase,
    CardFanPump,
    CardFanPsu,
    CardFanGeneric,
    CardFanStopped,
    StripNet,
    StripBattery,
    CardNetwork,
    CardNetIo,
    MenuNetwork,
    MenuBattery,
    UnitKBps,
    UnitGBps,
    CardBattery,
    CardBatteryCharging,
    CardBatteryOnBattery,
    CardBatteryFull,
    CardBatteryPlugged,
    CardBatteryLeft,
    MenuFans,
    MenuFanAuto,
    Count,
};

constexpr int kLanguageCount = static_cast<int>(Lang::Count);

// Picks the best supported language for a BCP-47 tag ("pt-BR", "zh-TW", "zh-Hant-HK", ...).
Lang FromTag(std::wstring_view tag);
// Language of the Windows user interface.
Lang FromSystem();
// Stable code stored in settings ("en", "pt", ..., "zh-Hans", "zh-Hant").
const wchar_t* Code(Lang lang);
// Language name in the language itself ("Português", "日本語", ...).
const wchar_t* NativeName(Lang lang);
// Locale used for DirectWrite text shaping and font fallback.
const wchar_t* Locale(Lang lang);
// Parses a stored code; returns false for "auto"/unknown.
bool FromCode(std::wstring_view code, Lang& lang);

const wchar_t* Get(Lang lang, Str id);

}  // namespace i18n
