#include "format.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <regex>

namespace text {

std::wstring Fixed1(double value, wchar_t decimal) {
    if (!std::isfinite(value)) value = 0.0;
    wchar_t buffer[48];
    swprintf(buffer, 48, L"%.1f", value);
    for (wchar_t* p = buffer; *p; ++p) {
        if (*p == L'.') *p = decimal;
    }
    return buffer;
}

std::wstring Percent(double value) {
    if (!std::isfinite(value)) value = 0.0;
    wchar_t buffer[16];
    swprintf(buffer, 16, L"%.0f", std::clamp(value, 0.0, 100.0));
    return buffer;
}

double GiB(uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
}

float Ratio(uint64_t used, uint64_t total) {
    if (total == 0) return 0.0f;
    return static_cast<float>(std::min(1.0, static_cast<double>(used) / static_cast<double>(total)));
}

std::wstring Trim(std::wstring_view s) {
    const auto first = s.find_first_not_of(L" \t\r\n");
    if (first == std::wstring_view::npos) return {};
    const auto last = s.find_last_not_of(L" \t\r\n");
    return std::wstring(s.substr(first, last - first + 1));
}

std::wstring CleanCpuName(const std::wstring& raw) {
    std::wstring s = raw;
    // The nominal clock is dropped: the real one changes all the time with boost.
    if (const auto at = s.find(L'@'); at != std::wstring::npos) s.erase(at);
    static const std::wregex noise(LR"(\((R|TM|tm|r)\)|\bCPU\b|\b\d+-Core Processor\b|\bProcessor\b)",
                                   std::regex::icase);
    s = std::regex_replace(s, noise, L"");
    static const std::wregex spaces(LR"(\s{2,})");
    s = std::regex_replace(s, spaces, L" ");
    s = Trim(s);
    return s.empty() ? Trim(raw) : s;
}

std::wstring Format(std::wstring_view pattern, std::initializer_list<std::wstring_view> args) {
    std::wstring out;
    out.reserve(pattern.size() + 32);
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == L'{' && i + 2 < pattern.size() && pattern[i + 2] == L'}' && pattern[i + 1] >= L'0' &&
            pattern[i + 1] <= L'9') {
            const size_t index = static_cast<size_t>(pattern[i + 1] - L'0');
            if (index < args.size()) {
                out += *(args.begin() + index);
                i += 2;
                continue;
            }
        }
        out += pattern[i];
    }
    return out;
}

std::wstring HistoryWindow(unsigned samples, unsigned intervalMs, std::wstring_view secondsPattern,
                           std::wstring_view minutesPattern) {
    const auto seconds =
        static_cast<unsigned>(std::lround(static_cast<double>(samples) * static_cast<double>(intervalMs) / 1000.0));
    if (seconds < 120) return Format(secondsPattern, {std::to_wstring(seconds)});
    return Format(minutesPattern, {std::to_wstring((seconds + 30) / 60)});
}

std::wstring IntervalLabel(unsigned intervalMs, wchar_t decimal, std::wstring_view secondsPattern) {
    std::wstring number;
    if (intervalMs % 1000 == 0) {
        number = std::to_wstring(intervalMs / 1000);
    } else {
        number = Fixed1(intervalMs / 1000.0, decimal);
        if (intervalMs % 100 != 0) {  // e.g. 750 ms -> "0,75"
            wchar_t buffer[32];
            swprintf(buffer, 32, L"%.2f", intervalMs / 1000.0);
            number = buffer;
            std::replace(number.begin(), number.end(), L'.', decimal);
        }
    }
    return Format(secondsPattern, {number});
}

std::wstring Degrees(double celsius, bool fahrenheit) {
    if (!std::isfinite(celsius)) celsius = 0.0;
    const double value = fahrenheit ? celsius * 9.0 / 5.0 + 32.0 : celsius;
    return std::to_wstring(static_cast<long>(std::lround(value)));
}

std::wstring TemperatureBadge(double celsius, bool fahrenheit) {
    return Degrees(celsius, fahrenheit) + (fahrenheit ? L"°F" : L"°");
}

std::wstring TemperatureText(double celsius, bool fahrenheit) {
    return Degrees(celsius, fahrenheit) + (fahrenheit ? L" °F" : L" °C");
}

RateText Rate(uint64_t bytesPerSecond, wchar_t decimal) {
    double value = static_cast<double>(bytesPerSecond) / 1024.0;
    int unit = 0;
    while (value >= 999.5 && unit < 2) {
        value /= 1024.0;
        ++unit;
    }
    RateText out;
    out.unit = unit;
    out.number = value < 9.95 ? Fixed1(value, decimal) : std::to_wstring(static_cast<long long>(std::llround(value)));
    return out;
}

std::wstring LinkSpeed(uint64_t bitsPerSecond, wchar_t decimal) {
    const double mbps = static_cast<double>(bitsPerSecond) / 1e6;
    if (mbps >= 1000.0) {
        const double gbps = mbps / 1000.0;
        const bool whole = std::fabs(gbps - std::round(gbps)) < 0.05;
        return (whole ? std::to_wstring(static_cast<long long>(std::llround(gbps))) : Fixed1(gbps, decimal)) + L" Gbps";
    }
    return std::to_wstring(static_cast<long long>(std::llround(mbps))) + L" Mbps";
}

std::wstring HoursMinutes(int seconds) {
    const int minutes = std::max(0, seconds) / 60;
    if (minutes < 60) return std::to_wstring(minutes) + L" min";
    wchar_t buffer[32];
    swprintf(buffer, 32, L"%d h %02d min", minutes / 60, minutes % 60);
    return buffer;
}

std::wstring Watts(double watts, wchar_t decimal) {
    if (!std::isfinite(watts) || std::fabs(watts) < 0.05) return L"0 W";  // never "-0,0 W"
    const double magnitude = std::fabs(watts);
    const std::wstring number = magnitude < 9.95 ? Fixed1(magnitude, decimal)
                                                 : std::to_wstring(static_cast<long long>(std::llround(magnitude)));
    return (watts > 0.0 ? L"+" : L"\u2212") + number + L" W";
}

std::wstring Chip(wchar_t icon, std::wstring_view text) {
    std::wstring out;
    if (icon) {
        out += icon;
        out += L'\u00a0';
    }
    out += text;
    return out;
}

std::wstring JoinChips(std::initializer_list<std::wstring_view> chips) {
    std::wstring out;
    for (std::wstring_view chip : chips) {
        if (chip.empty()) continue;
        if (!out.empty()) out += L'\u2003';
        out += chip;
    }
    return out;
}

std::vector<std::wstring> SplitParts(std::wstring_view text) {
    std::vector<std::wstring> parts;
    constexpr std::wstring_view kSeparator = L" \u00b7 ";
    size_t start = 0;
    for (;;) {
        const size_t at = text.find(kSeparator, start);
        parts.emplace_back(
            Trim(text.substr(start, at == std::wstring_view::npos ? std::wstring_view::npos : at - start)));
        if (at == std::wstring_view::npos) break;
        start = at + kSeparator.size();
    }
    return parts;
}

std::wstring IconParts(const std::wstring& text, std::initializer_list<wchar_t> icons) {
    const std::vector<std::wstring> parts = SplitParts(text);
    if (parts.size() != icons.size()) return text;
    std::wstring out;
    auto glyph = icons.begin();
    for (const std::wstring& part : parts) out = JoinChips({out, Chip(*glyph++, part)});
    return out;
}

std::wstring FirstDriveLetter(std::wstring_view letters) {
    std::wstring best;
    for (size_t i = 0; i + 1 < letters.size(); ++i) {
        if (letters[i + 1] != L':' || !std::iswalpha(letters[i])) continue;
        if (i > 0 && letters[i - 1] != L' ') continue;
        const std::wstring letter{static_cast<wchar_t>(std::towupper(letters[i])), L':'};
        if (best.empty() || letter < best) best = letter;
    }
    return best;
}

}  // namespace text
