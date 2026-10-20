#include "test.h"

#include "metrics.h"
#include "settings.h"

#include <chrono>
#include <filesystem>

// Integration tests against the real system (PDH, DXGI, the filesystem). Timing assertions use
// generous margins so they stay reliable on busy CI machines.

namespace {

// Message-only window counting the sampler notifications.
struct SampleSink {
    HWND hwnd = nullptr;
    int count = 0;
    MetricsSampler* sampler = nullptr;  // when set, re-arms the notification like the app does

    SampleSink() {
        WNDCLASSW wc{};
        wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            if (m == WM_APP_SAMPLE) {
                if (auto* self = reinterpret_cast<SampleSink*>(GetWindowLongPtrW(h, GWLP_USERDATA))) {
                    ++self->count;
                    if (self->sampler) self->sampler->Latest();
                }
                return 0;
            }
            return DefWindowProcW(h, m, w, l);
        };
        wc.lpszClassName = L"SysBarTests.Sink";
        wc.hInstance = GetModuleHandleW(nullptr);
        RegisterClassW(&wc);
        hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    }
    ~SampleSink() { DestroyWindow(hwnd); }

    // Pumps messages until `target` notifications arrived or the timeout expired.
    bool WaitFor(int target, int timeoutMs) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (count < target && std::chrono::steady_clock::now() < deadline) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        }
        return count >= target;
    }
};

struct TempConfigDir {
    std::filesystem::path dir;
    TempConfigDir() {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        dir = std::filesystem::path(temp) / (L"sysbar-tests-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::remove_all(dir);
        SetEnvironmentVariableW(L"SYSBAR_CONFIG_DIR", dir.c_str());
    }
    ~TempConfigDir() {
        SetEnvironmentVariableW(L"SYSBAR_CONFIG_DIR", nullptr);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    std::wstring File() const { return (dir / L"settings.ini").wstring(); }
};

double ElapsedMs(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}

}  // namespace

TEST(Sampler_delivers_valid_samples) {
    SampleSink sink;
    MetricsSampler sampler;
    sink.sampler = &sampler;
    CHECK(sampler.Start(sink.hwnd, WM_APP_SAMPLE, 250));
    CHECK(sink.WaitFor(4, 8000));
    const MetricsSnapshot s = sampler.Latest();
    const SystemInfo info = sampler.Info();
    sampler.Stop();

    CHECK(s.valid);
    CHECK(s.cpu >= 0.0 && s.cpu <= 100.0);

    MEMORYSTATUSEX mem{sizeof(mem)};
    GlobalMemoryStatusEx(&mem);
    CHECK_EQ(s.ramTotal, mem.ullTotalPhys);
    CHECK(s.ramUsed > 0 && s.ramUsed <= s.ramTotal);
    // "In use" must match the system (with room for the drift between both reads).
    CHECK_NEAR(static_cast<double>(s.ramUsed), static_cast<double>(mem.ullTotalPhys - mem.ullAvailPhys),
               0.03 * static_cast<double>(mem.ullTotalPhys));

    CHECK(!info.cpuName.empty());
    CHECK_EQ(info.logicalCpus, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));

    CHECK(s.diskAvailable);  // every machine has a disk
    CHECK(s.diskActive >= 0.0 && s.diskActive <= 100.0);
    if (s.gpuTemperature) CHECK(*s.gpuTemperature > 0.0 && *s.gpuTemperature < 150.0);
    if (s.gpuAvailable) {
        CHECK(s.gpu >= 0.0 && s.gpu <= 100.0);
        CHECK(s.vramUsed <= s.vramTotal || s.vramTotal == 0);
        CHECK(!info.gpuName.empty());
    }
    std::printf("      disk %.1f%% (read %.2f / write %.2f MB/s) | gpu temp %s | gpu fan %s rpm\n", s.diskActive,
                s.diskReadBps / 1048576.0, s.diskWriteBps / 1048576.0,
                s.gpuTemperature ? std::to_string(*s.gpuTemperature).c_str() : "n/a",
                s.gpuFanRpm ? std::to_string(*s.gpuFanRpm).c_str() : "n/a");
    std::printf("      cpu %.1f%% | ram %.1f/%.1f GB | gpu [%s] %.1f%% | vram %.2f/%.1f GB\n", s.cpu,
                s.ramUsed / 1073741824.0, s.ramTotal / 1073741824.0, t::Narrow(info.gpuName).c_str(), s.gpu,
                s.vramUsed / 1073741824.0, s.vramTotal / 1073741824.0);
}

TEST(Sampler_keeps_its_cadence) {
    SampleSink sink;
    MetricsSampler sampler;
    sink.sampler = &sampler;
    sampler.Start(sink.hwnd, WM_APP_SAMPLE, 500);
    CHECK(sink.WaitFor(1, 4000));  // the first sample arrives quickly (~400 ms)
    const auto t0 = std::chrono::steady_clock::now();
    const int before = sink.count;
    sink.WaitFor(before + 4, 8000);
    const double ms = ElapsedMs(t0);
    sampler.Stop();
    CHECK_EQ(sink.count, before + 4);
    CHECK_NEAR(ms, 2000.0, 600.0);  // 4 samples x 500 ms, no accumulated drift
}

TEST(Sampler_coalesces_notifications) {
    // Without re-arming (Latest), only one notification may be pending at a time.
    SampleSink sink;
    MetricsSampler sampler;
    sampler.Start(sink.hwnd, WM_APP_SAMPLE, 100);
    Sleep(1000);  // several samples, but nobody pumps messages
    sink.WaitFor(100, 300);
    CHECK_EQ(sink.count, 1);
    sampler.Stop();
}

TEST(Sampler_applies_a_new_interval_immediately) {
    SampleSink sink;
    MetricsSampler sampler;
    sink.sampler = &sampler;
    sampler.Start(sink.hwnd, WM_APP_SAMPLE, 5000);
    CHECK(sink.WaitFor(1, 4000));
    sampler.SetInterval(200);
    CHECK(sink.WaitFor(sink.count + 3, 2500));  // with 5 s this could never fit
    sampler.Stop();
}

TEST(Sampler_pauses_and_resumes) {
    SampleSink sink;
    MetricsSampler sampler;
    sink.sampler = &sampler;
    sampler.Start(sink.hwnd, WM_APP_SAMPLE, 150);
    CHECK(sink.WaitFor(2, 4000));
    sampler.SetPaused(true);
    sink.WaitFor(1000, 300);  // drain anything already in flight
    const int paused = sink.count;
    sink.WaitFor(1000, 800);
    CHECK_EQ(sink.count, paused);  // nothing while paused
    sampler.SetPaused(false);
    CHECK(sink.WaitFor(paused + 2, 3000));
    sampler.Stop();
}

TEST(Sampler_stops_quickly_and_restarts) {
    for (int round = 0; round < 3; ++round) {
        SampleSink sink;
        MetricsSampler sampler;
        sink.sampler = &sampler;
        sampler.Start(sink.hwnd, WM_APP_SAMPLE, 1000);
        CHECK(sink.WaitFor(1, 4000));
        const auto t0 = std::chrono::steady_clock::now();
        sampler.Stop();
        CHECK(ElapsedMs(t0) < 500.0);  // Stop() does not wait for the interval to elapse
    }
}

TEST(Settings_defaults_without_a_file) {
    TempConfigDir temp;
    Settings s;
    s.Load();
    CHECK(s.showCpu && s.showRam && s.showGpu && s.showVram && s.colorByLoad && s.clickOpensTaskManager);
    CHECK_EQ(s.intervalMs, 1000ul);
    CHECK_EQ(s.language, std::wstring(L"auto"));
    CHECK_EQ(s.offsetDip, 0);
}

TEST(Settings_round_trip) {
    TempConfigDir temp;
    Settings a;
    a.showRam = false;
    a.showVram = false;
    a.showDisk = false;
    a.showTemperatures = false;
    a.colorByLoad = false;
    a.clickOpensTaskManager = false;
    a.intervalMs = 2000;
    a.language = L"zh-Hant";
    a.offsetDip = 120;
    a.Save();
    Settings b;
    b.Load();
    CHECK(b.showCpu);
    CHECK(!b.showRam);
    CHECK(b.showGpu);
    CHECK(!b.showVram);
    CHECK(!b.showDisk);
    CHECK(!b.showTemperatures);
    CHECK(!b.colorByLoad);
    CHECK(!b.clickOpensTaskManager);
    CHECK_EQ(b.intervalMs, 2000ul);
    CHECK_EQ(b.language, std::wstring(L"zh-Hant"));
    CHECK_EQ(b.offsetDip, 120);
}

TEST(Settings_fix_absurd_values) {
    TempConfigDir temp;
    std::filesystem::create_directories(temp.dir);
    const std::wstring file = temp.File();
    WritePrivateProfileStringW(L"sys-bar", L"IntervalMs", L"5", file.c_str());
    WritePrivateProfileStringW(L"sys-bar", L"OffsetDip", L"-50", file.c_str());
    for (const wchar_t* key : {L"ShowCpu", L"ShowRam", L"ShowGpu", L"ShowVram", L"ShowDisk"}) {
        WritePrivateProfileStringW(L"sys-bar", key, L"0", file.c_str());
    }
    WritePrivateProfileStringW(L"sys-bar", L"Language", L"", file.c_str());
    Settings s;
    s.Load();
    CHECK_EQ(s.intervalMs, Settings::kMinIntervalMs);
    CHECK_EQ(s.offsetDip, 0);
    CHECK(s.showCpu);  // never everything hidden
    CHECK_EQ(s.language, std::wstring(L"auto"));

    WritePrivateProfileStringW(L"sys-bar", L"IntervalMs", L"999999", file.c_str());
    WritePrivateProfileStringW(L"sys-bar", L"OffsetDip", L"999999", file.c_str());
    s.Load();
    CHECK_EQ(s.intervalMs, Settings::kMaxIntervalMs);
    CHECK_EQ(s.offsetDip, Settings::kMaxOffsetDip);
}
