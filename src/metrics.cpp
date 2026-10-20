#include "metrics.h"

#include "format.h"

#include <dxgi.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace {

constexpr DWORD kFirstSampleDelayMs = 400;     // rate counters need two samples
constexpr ULONGLONG kAdapterRetryMs = 30'000;  // when no GPU is found, look again every 30 s
constexpr int kAdapterMissLimit = 3;           // samples without our LUID before re-enumerating
constexpr UINT kMicrosoftVendor = 0x1414;      // "Microsoft Basic Render Driver"
constexpr UINT kBasicRenderDevice = 0x8c;

bool IsValidStatus(DWORD status) {
    return status == PDH_CSTATUS_VALID_DATA || status == PDH_CSTATUS_NEW_DATA;
}

// Case-insensitive substring search (PDH uses lowercase hex today, but we do not rely on it).
bool ContainsNoCase(const wchar_t* haystack, const std::wstring& needle) {
    const size_t n = needle.size();
    if (n == 0) return true;
    for (const wchar_t* p = haystack; *p; ++p) {
        if (_wcsnicmp(p, needle.c_str(), n) == 0) return true;
    }
    return false;
}

std::wstring ReadCpuName() {
    wchar_t buffer[256] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString",
                     RRF_RT_REG_SZ, nullptr, buffer, &size) == ERROR_SUCCESS) {
        return text::Trim(buffer);
    }
    return L"CPU";
}

// Efficiency mode (EcoQoS) for the sampling thread only; the UI thread shares its input queue
// with the taskbar and must stay responsive.
void UseEfficiencyMode() {
    THREAD_POWER_THROTTLING_STATE state{};
    state.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &state, sizeof(state));
}

}  // namespace

GpuEngineResult AggregateGpuEngines(const PDH_FMT_COUNTERVALUE_ITEM_W* items, DWORD count, const std::wstring& luidTag,
                                    std::unordered_map<std::wstring_view, double>& scratch) {
    // Instances look like: pid_1234_luid_0x..._0x..._phys_0_eng_3_engtype_3D
    // Sum per engine (over all processes) and report the busiest engine, like Task Manager.
    GpuEngineResult result;
    scratch.clear();
    if (luidTag.empty()) return result;
    for (DWORD i = 0; i < count; ++i) {
        const wchar_t* name = items[i].szName;
        if (!name || !ContainsNoCase(name, luidTag)) continue;
        result.sawAdapter = true;
        if (!IsValidStatus(items[i].FmtValue.CStatus)) continue;
        const wchar_t* phys = wcsstr(name, L"_phys_");
        const wchar_t* type = wcsstr(name, L"_engtype_");
        if (!phys || !type || type < phys) continue;
        const double v = items[i].FmtValue.doubleValue;
        if (v > 0.0 && std::isfinite(v)) scratch[std::wstring_view(phys, static_cast<size_t>(type - phys))] += v;
    }
    for (const auto& [key, value] : scratch) result.busiest = std::max(result.busiest, value);
    result.busiest = std::clamp(result.busiest, 0.0, 100.0);
    return result;
}

uint64_t SumAdapterMemory(const PDH_FMT_COUNTERVALUE_ITEM_W* items, DWORD count, const std::wstring& luidTag,
                          bool& sawAdapter) {
    uint64_t used = 0;
    sawAdapter = false;
    if (luidTag.empty()) return 0;
    for (DWORD i = 0; i < count; ++i) {
        if (!items[i].szName || !ContainsNoCase(items[i].szName, luidTag)) continue;
        sawAdapter = true;
        if (IsValidStatus(items[i].FmtValue.CStatus) && items[i].FmtValue.largeValue > 0) {
            used += static_cast<uint64_t>(items[i].FmtValue.largeValue);
        }
    }
    return used;
}

MetricsSampler::~MetricsSampler() {
    Stop();
}

bool MetricsSampler::Start(HWND notify, UINT message, DWORD intervalMs) {
    if (thread_.joinable()) return true;
    notify_ = notify;
    message_ = message;
    intervalMs_ = intervalMs;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!stopEvent_ || !wakeEvent_) return false;

    {
        std::lock_guard lock(mutex_);
        info_.cpuName = ReadCpuName();
        info_.logicalCpus = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    }

    thread_ = std::thread(&MetricsSampler::Run, this);
    return true;
}

void MetricsSampler::Stop() {
    if (thread_.joinable()) {
        SetEvent(stopEvent_);
        thread_.join();
    }
    if (stopEvent_) CloseHandle(stopEvent_), stopEvent_ = nullptr;
    if (wakeEvent_) CloseHandle(wakeEvent_), wakeEvent_ = nullptr;
}

void MetricsSampler::SetInterval(DWORD intervalMs) {
    intervalMs_ = intervalMs;
    if (wakeEvent_) SetEvent(wakeEvent_);
}

void MetricsSampler::SetPaused(bool paused) {
    if (paused_.exchange(paused) != paused && wakeEvent_) SetEvent(wakeEvent_);
}

MetricsSnapshot MetricsSampler::Latest() {
    notifyPending_ = false;
    std::lock_guard lock(mutex_);
    return latest_;
}

SystemInfo MetricsSampler::Info() const {
    std::lock_guard lock(mutex_);
    return info_;
}

void MetricsSampler::Run() {
    UseEfficiencyMode();
    OpenQuery();
    SelectAdapters();
    if (query_) PdhCollectQueryData(query_);  // prime the rate counters
    double primed = 0.0;
    ReadCpuFallback(primed);

    const HANDLE handles[2] = {stopEvent_, wakeEvent_};
    ULONGLONG nextDue = GetTickCount64() + kFirstSampleDelayMs;

    for (;;) {
        if (paused_) {
            if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0) break;
            if (!paused_) {
                // Resuming: start a fresh measurement window instead of averaging over the pause.
                if (query_) PdhCollectQueryData(query_);
                ReadCpuFallback(primed);
                nextDue = GetTickCount64() + kFirstSampleDelayMs;
            }
            continue;
        }

        const ULONGLONG now = GetTickCount64();
        const DWORD wait = nextDue > now ? static_cast<DWORD>(nextDue - now) : 0;
        const DWORD result = WaitForMultipleObjects(2, handles, FALSE, wait);
        if (result == WAIT_OBJECT_0) break;
        if (result == WAIT_OBJECT_0 + 1) {
            if (!paused_) nextDue = GetTickCount64() + intervalMs_.load();  // interval changed: reschedule
            continue;
        }

        MetricsSnapshot snapshot;
        Collect(snapshot);
        {
            std::lock_guard lock(mutex_);
            latest_ = snapshot;
            std::wstring names;
            for (const auto& adapter : adapters_) names += (names.empty() ? L"" : L" + ") + adapter.name;
            info_.gpuName = names;
        }
        // One pending notification at most: a busy UI thread never gets a burst of stale samples.
        if (!notifyPending_.exchange(true)) PostMessageW(notify_, message_, 0, 0);

        // Fixed cadence without drift; after a long stall (e.g. sleep) start over.
        const DWORD interval = intervalMs_.load();
        nextDue += interval;
        const ULONGLONG after = GetTickCount64();
        if (nextDue <= after) nextDue = after + interval;
    }

    CloseQuery();
}

bool MetricsSampler::OpenQuery() {
    if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) {
        query_ = nullptr;
        return false;
    }
    // English counter names work on every Windows display language (localized names would not).
    if (PdhAddEnglishCounterW(query_, L"\\Processor Information(_Total)\\% Processor Utility", 0, &cpuCounter_) !=
        ERROR_SUCCESS) {
        cpuCounter_ = nullptr;
        if (PdhAddEnglishCounterW(query_, L"\\Processor(_Total)\\% Processor Time", 0, &cpuCounter_) != ERROR_SUCCESS) {
            cpuCounter_ = nullptr;
        }
    }
    if (PdhAddEnglishCounterW(query_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &gpuEngineCounter_) !=
        ERROR_SUCCESS) {
        gpuEngineCounter_ = nullptr;
    }
    if (PdhAddEnglishCounterW(query_, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &gpuMemoryCounter_) !=
        ERROR_SUCCESS) {
        gpuMemoryCounter_ = nullptr;
    }
    return true;
}

void MetricsSampler::CloseQuery() {
    if (query_) PdhCloseQuery(query_);
    query_ = nullptr;
    cpuCounter_ = gpuEngineCounter_ = gpuMemoryCounter_ = nullptr;
}

// Lists the physical GPUs, main one (largest dedicated memory) first.
bool MetricsSampler::SelectAdapters() {
    adapters_.clear();
    adapterMisses_ = 0;
    nextAdapterSearch_ = GetTickCount64() + kAdapterRetryMs;

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            const bool software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ||
                                  (desc.VendorId == kMicrosoftVendor && desc.DeviceId == kBasicRenderDevice);
            wchar_t tag[64];
            swprintf_s(tag, L"luid_0x%08x_0x%08x", static_cast<unsigned>(desc.AdapterLuid.HighPart),
                       static_cast<unsigned>(desc.AdapterLuid.LowPart));
            const bool duplicate =
                std::any_of(adapters_.begin(), adapters_.end(), [&](const Adapter& a) { return a.tag == tag; });
            if (!software && !duplicate) {
                adapters_.push_back({tag, text::Trim(desc.Description), desc.DedicatedVideoMemory});
            }
        }
        adapter.Reset();
    }
    std::stable_sort(adapters_.begin(), adapters_.end(),
                     [](const Adapter& a, const Adapter& b) { return a.dedicated > b.dedicated; });
    return !adapters_.empty();
}

bool MetricsSampler::ReadArray(PDH_HCOUNTER counter, DWORD format, std::vector<BYTE>& buffer, DWORD& count) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        DWORD size = static_cast<DWORD>(buffer.size());
        count = 0;
        const PDH_STATUS status = PdhGetFormattedCounterArrayW(
            counter, format, &size, &count,
            buffer.empty() ? nullptr : reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data()));
        if (status == ERROR_SUCCESS) return true;
        if (status != PDH_MORE_DATA) return false;
        buffer.resize(size + size / 4 + 1024);  // headroom for new instances
    }
    return false;
}

bool MetricsSampler::ReadCpuFallback(double& value) {
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) return false;
    const ULARGE_INTEGER i{{idle.dwLowDateTime, idle.dwHighDateTime}};
    const ULARGE_INTEGER k{{kernel.dwLowDateTime, kernel.dwHighDateTime}};
    const ULARGE_INTEGER u{{user.dwLowDateTime, user.dwHighDateTime}};
    bool ok = false;
    if (haveCpuFallback_) {
        const uint64_t di = i.QuadPart - lastIdle_.QuadPart;
        const uint64_t total = (k.QuadPart - lastKernel_.QuadPart) + (u.QuadPart - lastUser_.QuadPart);
        if (total > 0) {
            value = 100.0 * static_cast<double>(total - std::min(di, total)) / static_cast<double>(total);
            ok = true;
        }
    }
    lastIdle_ = i;
    lastKernel_ = k;
    lastUser_ = u;
    haveCpuFallback_ = true;
    return ok;
}

void MetricsSampler::Collect(MetricsSnapshot& out) {
    out.valid = true;
    const bool collected = query_ && PdhCollectQueryData(query_) == ERROR_SUCCESS;

    // CPU
    bool cpuOk = false;
    if (collected && cpuCounter_) {
        PDH_FMT_COUNTERVALUE value{};
        if (PdhGetFormattedCounterValue(cpuCounter_, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &value) ==
                ERROR_SUCCESS &&
            IsValidStatus(value.CStatus)) {
            out.cpu = value.doubleValue;
            cpuOk = true;
        }
    }
    double fallback = 0.0;
    const bool fallbackOk = ReadCpuFallback(fallback);
    if (!cpuOk && fallbackOk) out.cpu = fallback;
    out.cpu = std::clamp(std::isfinite(out.cpu) ? out.cpu : 0.0, 0.0, 100.0);

    // RAM: "in use" = total - available, like Task Manager.
    MEMORYSTATUSEX mem{sizeof(mem)};
    if (GlobalMemoryStatusEx(&mem)) {
        out.ramTotal = mem.ullTotalPhys;
        out.ramUsed = mem.ullTotalPhys - mem.ullAvailPhys;
    }

    // GPU. Availability is latched once the counters produced data, so a transient failure (or a
    // burst of process churn) reuses the last values instead of making GPU/VRAM blink in and out.
    if (adapters_.empty() && GetTickCount64() >= nextAdapterSearch_) SelectAdapters();
    auto keepLast = [&] {
        out.gpuAvailable = gpuSeen_;
        out.gpu = lastGpu_.gpu;
        out.vramUsed = lastGpu_.vramUsed;
        out.vramTotal = lastGpu_.vramTotal;
    };
    if (adapters_.empty() || !collected) return keepLast();

    const Adapter& main = adapters_.front();
    const uint64_t vramTotal = main.dedicated >= kMinDedicatedVram ? main.dedicated : 0;
    bool sawAny = false, engineOk = false, memoryOk = false;
    double busiest = 0.0;
    uint64_t vramUsed = 0;

    DWORD count = 0;
    if (gpuEngineCounter_ && ReadArray(gpuEngineCounter_, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, engineBuffer_, count)) {
        engineOk = true;
        const auto* items = reinterpret_cast<const PDH_FMT_COUNTERVALUE_ITEM_W*>(engineBuffer_.data());
        for (const Adapter& adapter : adapters_) {
            const auto result = AggregateGpuEngines(items, count, adapter.tag, engines_);
            sawAny |= result.sawAdapter;
            busiest = std::max(busiest, result.busiest);
        }
    }
    count = 0;
    if (gpuMemoryCounter_ && ReadArray(gpuMemoryCounter_, PDH_FMT_LARGE, memoryBuffer_, count)) {
        memoryOk = true;
        bool saw = false;
        vramUsed = SumAdapterMemory(reinterpret_cast<const PDH_FMT_COUNTERVALUE_ITEM_W*>(memoryBuffer_.data()), count,
                                    main.tag, saw);
        sawAny |= saw;
    }

    if (!sawAny) {
        // The LUID changes when the driver restarts or updates: enumerate the adapters again.
        if (++adapterMisses_ >= kAdapterMissLimit) SelectAdapters();
        return keepLast();
    }
    adapterMisses_ = 0;
    if (!engineOk || !memoryOk) return keepLast();

    gpuSeen_ = true;
    out.gpuAvailable = true;
    out.gpu = busiest;
    out.vramTotal = vramTotal;
    out.vramUsed = std::min(vramUsed, vramTotal);
    lastGpu_ = out;
}
