#include "test.h"

#include "format.h"

#include <limits>

using namespace text;

TEST(Fixed1_uses_the_user_decimal_separator) {
    CHECK_EQ(Fixed1(12.34, L','), std::wstring(L"12,3"));
    CHECK_EQ(Fixed1(12.34, L'.'), std::wstring(L"12.3"));
    CHECK_EQ(Fixed1(0.0, L','), std::wstring(L"0,0"));
    CHECK_EQ(Fixed1(9.96, L','), std::wstring(L"10,0"));
    CHECK_EQ(Fixed1(1023.98, L','), std::wstring(L"1024,0"));
}

TEST(Fixed1_handles_invalid_values) {
    CHECK_EQ(Fixed1(std::numeric_limits<double>::quiet_NaN(), L','), std::wstring(L"0,0"));
    CHECK_EQ(Fixed1(std::numeric_limits<double>::infinity(), L','), std::wstring(L"0,0"));
}

TEST(Percent_rounds_and_clamps) {
    CHECK_EQ(Percent(23.4), std::wstring(L"23"));
    CHECK_EQ(Percent(99.6), std::wstring(L"100"));
    CHECK_EQ(Percent(150.0), std::wstring(L"100"));
    CHECK_EQ(Percent(-5.0), std::wstring(L"0"));
    CHECK_EQ(Percent(0.4), std::wstring(L"0"));
    CHECK_EQ(Percent(std::numeric_limits<double>::quiet_NaN()), std::wstring(L"0"));
}

TEST(GiB_and_Ratio) {
    CHECK_NEAR(GiB(1073741824ull), 1.0, 1e-12);
    CHECK_NEAR(GiB(34195574784ull), 31.847, 0.001);  // "32 GB installed" -> 31.8 GB usable
    CHECK_EQ(Ratio(1, 0), 0.0f);
    CHECK_NEAR(Ratio(5, 10), 0.5, 1e-6);
    CHECK_EQ(Ratio(20, 10), 1.0f);  // never above 100%
}

TEST(Used_is_never_shown_above_total) {
    // Even with rounding, the formatted "used" value must never exceed the formatted total.
    const uint64_t total = 34195574784ull;
    for (uint64_t used = 0; used <= total; used += total / 997) {
        const double u = std::stod(Fixed1(GiB(used), L'.'));
        const double tt = std::stod(Fixed1(GiB(total), L'.'));
        CHECK(u <= tt);
    }
}

TEST(CleanCpuName_removes_marketing_noise) {
    CHECK_EQ(CleanCpuName(L"Intel(R) Xeon(R) CPU E5-2699 v3 @ 2.30GHz"), std::wstring(L"Intel Xeon E5-2699 v3"));
    CHECK_EQ(CleanCpuName(L"Intel(R) Core(TM) i7-8700K CPU @ 3.70GHz"), std::wstring(L"Intel Core i7-8700K"));
    CHECK_EQ(CleanCpuName(L"13th Gen Intel(R) Core(TM) i9-13900K"), std::wstring(L"13th Gen Intel Core i9-13900K"));
    CHECK_EQ(CleanCpuName(L"AMD Ryzen 9 5950X 16-Core Processor            "), std::wstring(L"AMD Ryzen 9 5950X"));
    CHECK_EQ(CleanCpuName(L"AMD Ryzen 7 7840HS w/ Radeon 780M Graphics"),
             std::wstring(L"AMD Ryzen 7 7840HS w/ Radeon 780M Graphics"));
    CHECK_EQ(CleanCpuName(L"Snapdragon(R) X Elite - X1E78100 - Qualcomm(R) Oryon(TM) CPU"),
             std::wstring(L"Snapdragon X Elite - X1E78100 - Qualcomm Oryon"));
}

TEST(CleanCpuName_never_returns_empty_for_non_empty_input) {
    CHECK_EQ(CleanCpuName(L"CPU"), std::wstring(L"CPU"));
    CHECK_EQ(CleanCpuName(L""), std::wstring(L""));
    CHECK_EQ(CleanCpuName(L"   "), std::wstring(L""));
}

TEST(Format_replaces_placeholders) {
    CHECK_EQ(Format(L"of {0} · {1} in use", {L"31,8 GB", L"57%"}), std::wstring(L"of 31,8 GB · 57% in use"));
    CHECK_EQ(Format(L"{1} before {0}", {L"a", L"b"}), std::wstring(L"b before a"));
    CHECK_EQ(Format(L"%{0}", {L"23"}), std::wstring(L"%23"));
    CHECK_EQ(Format(L"missing {3} stays", {L"x"}), std::wstring(L"missing {3} stays"));
    CHECK_EQ(Format(L"{0}{0}", {L"ab"}), std::wstring(L"abab"));
    CHECK_EQ(Format(L"trailing {", {L"x"}), std::wstring(L"trailing {"));
}

TEST(HistoryWindow_describes_the_time_span) {
    CHECK_EQ(HistoryWindow(60, 1000, L"last {0} s", L"last {0} min"), std::wstring(L"last 60 s"));
    CHECK_EQ(HistoryWindow(60, 500, L"last {0} s", L"last {0} min"), std::wstring(L"last 30 s"));
    CHECK_EQ(HistoryWindow(60, 2000, L"last {0} s", L"last {0} min"), std::wstring(L"last 2 min"));
    CHECK_EQ(HistoryWindow(60, 5000, L"last {0} s", L"last {0} min"), std::wstring(L"last 5 min"));
}

TEST(IntervalLabel_formats_seconds) {
    CHECK_EQ(IntervalLabel(500, L',', L"{0} s"), std::wstring(L"0,5 s"));
    CHECK_EQ(IntervalLabel(1000, L',', L"{0} s"), std::wstring(L"1 s"));
    CHECK_EQ(IntervalLabel(2500, L'.', L"{0} s"), std::wstring(L"2.5 s"));
    CHECK_EQ(IntervalLabel(750, L',', L"{0} s"), std::wstring(L"0,75 s"));
    CHECK_EQ(IntervalLabel(5000, L',', L"{0}초"), std::wstring(L"5초"));
}

TEST(Temperatures_in_both_units) {
    CHECK_EQ(Degrees(54.4, false), std::wstring(L"54"));
    CHECK_EQ(Degrees(54.4, true), std::wstring(L"130"));  // 129.92
    CHECK_EQ(Degrees(-40.0, true), std::wstring(L"-40"));
    CHECK_EQ(Degrees(100.0, true), std::wstring(L"212"));
    CHECK_EQ(TemperatureBadge(54.0, false), std::wstring(L"54\u00b0"));
    CHECK_EQ(TemperatureBadge(54.0, true), std::wstring(L"129\u00b0F"));
    CHECK_EQ(TemperatureText(40.0, false), std::wstring(L"40 \u00b0C"));
    CHECK_EQ(TemperatureText(40.0, true), std::wstring(L"104 \u00b0F"));
}

TEST(First_drive_letter) {
    CHECK_EQ(FirstDriveLetter(L"C: E:"), std::wstring(L"C:"));
    CHECK_EQ(FirstDriveLetter(L"F: D:"), std::wstring(L"D:"));
    CHECK_EQ(FirstDriveLetter(L"d:"), std::wstring(L"D:"));
    CHECK_EQ(FirstDriveLetter(L""), std::wstring(L""));
    CHECK_EQ(FirstDriveLetter(L"x"), std::wstring(L""));
}

TEST(Trim) {
    CHECK_EQ(Trim(L"  a b \t\r\n"), std::wstring(L"a b"));
    CHECK_EQ(Trim(L""), std::wstring(L""));
}
