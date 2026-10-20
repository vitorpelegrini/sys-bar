#pragma once

// Segoe MDL2 Assets glyphs (Windows 10 and 11) used inline in the card's texts. The card draws every
// Private Use Area character with that font, in the row's color; text::Chip keeps an icon with its text.
namespace icon {

constexpr wchar_t kChip = 0xE950;     // processor
constexpr wchar_t kThreads = 0xE81E;  // stacked layers
constexpr wchar_t kMemory = 0xE964;   // memory module
constexpr wchar_t kUsage = 0xEC4A;    // gauge
constexpr wchar_t kDisplay = 0xE7F4;  // graphics card / monitor
constexpr wchar_t kDrive = 0xEDA2;    // drive
constexpr wchar_t kDown = 0xE896;     // read / download
constexpr wchar_t kUp = 0xE898;       // write / upload
constexpr wchar_t kEthernet = 0xE839;
constexpr wchar_t kWifi = 0xE701;
constexpr wchar_t kPeak = 0xE9D2;   // area chart
constexpr wchar_t kPower = 0xE945;  // lightning bolt
constexpr wchar_t kBattery = 0xEBA7;
constexpr wchar_t kCharging = 0xEBB5;
constexpr wchar_t kClock = 0xE823;
constexpr wchar_t kThermometer = 0xE9CA;
constexpr wchar_t kOptions = 0xE9F5;  // gears
constexpr wchar_t kTaskManager = 0xE9D9;
constexpr wchar_t kUpdate = 0xE896;
constexpr wchar_t kInfo = 0xE946;
constexpr wchar_t kSync = 0xE895;
constexpr wchar_t kCheck = 0xE73E;
constexpr wchar_t kWarning = 0xE7BA;

constexpr bool IsIcon(wchar_t c) {
    return c >= 0xE000 && c <= 0xF8FF;
}

}  // namespace icon
