#include "test.h"

#include "common.h"

#include <objbase.h>

#include <chrono>
#include <cstdio>
#include <cstring>

namespace t {

std::wstring previewDir;
static int g_failures = 0;

std::vector<Case>& Registry() {
    static std::vector<Case> cases;
    return cases;
}

void Fail(const char* file, int line, const std::string& message) {
    ++g_failures;
    const char* name = file;
    for (const char* p = file; *p; ++p) {
        if (*p == '\\' || *p == '/') name = p + 1;
    }
    std::printf("      failed at %s:%d: %s\n", name, line, message.c_str());
}

std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

}  // namespace t

// Usage: sysbar-tests [--previews <dir>] [name-filter]
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::wstring filter;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--previews" && i + 1 < argc) {
            t::previewDir = argv[++i];
        } else {
            filter = argv[i];
        }
    }

    int passed = 0, failed = 0;
    const auto start = std::chrono::steady_clock::now();
    for (const auto& c : t::Registry()) {
        const std::wstring name(c.name, c.name + std::strlen(c.name));
        if (!filter.empty() && name.find(filter) == std::wstring::npos) continue;
        const int before = t::g_failures;
        const auto t0 = std::chrono::steady_clock::now();
        c.fn();
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (t::g_failures == before) {
            ++passed;
            std::printf("  ok    %s (%lld ms)\n", c.name, static_cast<long long>(ms));
        } else {
            ++failed;
            std::printf("  FAIL  %s\n", c.name);
        }
    }
    const auto total =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::printf("\n%d passed, %d failed (%lld ms)\n", passed, failed, static_cast<long long>(total));
    CoUninitialize();
    return failed ? 1 : 0;
}
