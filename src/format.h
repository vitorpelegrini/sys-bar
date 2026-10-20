#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

// Pure formatting helpers (no window or system dependencies) - fully covered by tests.
namespace text {

std::wstring Fixed1(double value, wchar_t decimal);  // 12.34 -> "12,3" (decimal = ',')
std::wstring Percent(double value);                  // 0..100, rounded, without the "%" sign
double GiB(uint64_t bytes);
float Ratio(uint64_t used, uint64_t total);  // 0 when total == 0, never above 1
std::wstring Trim(std::wstring_view s);

// "Intel(R) Core(TM) i7-8700K CPU @ 3.70GHz" -> "Intel Core i7-8700K"
std::wstring CleanCpuName(const std::wstring& raw);

// Replaces {0}, {1}, ... in `pattern` with the given arguments.
std::wstring Format(std::wstring_view pattern, std::initializer_list<std::wstring_view> args);

// Duration covered by the history graph, e.g. "last 60 s" / "last 2 min".
std::wstring HistoryWindow(unsigned samples, unsigned intervalMs, std::wstring_view secondsPattern,
                           std::wstring_view minutesPattern);

// Update interval label for the menu: 500 ms -> "0,5 s".
std::wstring IntervalLabel(unsigned intervalMs, wchar_t decimal, std::wstring_view secondsPattern);

// Rounded temperature in the chosen unit: 54.4 -> "54" (Celsius) or "130" (Fahrenheit).
std::wstring Degrees(double celsius, bool fahrenheit);
// Strip badge: "54°" in Celsius, "130°F" in Fahrenheit (the unit is spelled out when it is not the
// metric default, so a US reader never mistakes it).
std::wstring TemperatureBadge(double celsius, bool fahrenheit);
// Card value: "54 °C" / "130 °F".
std::wstring TemperatureText(double celsius, bool fahrenheit);

// First drive letter of a PDH disk instance in alphabetical order: "E: C:" -> "C:"; "" when none.
std::wstring FirstDriveLetter(std::wstring_view letters);

}  // namespace text
