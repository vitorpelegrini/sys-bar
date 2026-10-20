#include "metrics.h"

#include "format.h"
#include "sensorfeed.h"
#include "version.h"

#include <dxgi.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>

namespace {

constexpr DWORD kFirstSampleDelayMs = 400;                   // rate counters need two samples
constexpr ULONGLONG kAdapterRetryMs = 30'000;                // when no GPU is found, look again every 30 s
constexpr int kAdapterMissLimit = 3;                         // samples without our LUID before re-enumerating
constexpr ULONGLONG kDiskTemperatureMs = 5'000;              // drive temperatures change slowly
constexpr ULONGLONG kServiceUpdateRetryMs = 30 * 60 * 1000;  // ask an outdated sensors service every 30 min
constexpr UINT kMicrosoftVendor = 0x1414;                    // "Microsoft Basic Render Driver"
constexpr UINT kBasicRenderDevice = 0x8c;
constexpr DWORD kStopGraceMs = 1500;
constexpr ULONGLONG kCounterRetryMs = 30'000;  // GPU counters missing at logon are added later

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

MetricsSampler::MetricsSampler() : feed_(std::make_unique<feed::SensorFeed>()) {}

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
    if (!stopEvent_ || !wakeEvent_) {
        if (stopEvent_) CloseHandle(stopEvent_), stopEvent_ = nullptr;
        if (wakeEvent_) CloseHandle(wakeEvent_), wakeEvent_ = nullptr;
        return false;
    }

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
        // This runs on the UI thread, which shares its input queue with the taskbar: a sample stuck in a
        // device query (a hung USB drive) is cancelled rather than waited for.
        const HANDLE thread = thread_.native_handle();
        if (WaitForSingleObject(thread, kStopGraceMs) == WAIT_TIMEOUT) CancelSynchronousIo(thread);
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
                network_.Update(hw::ReadNetInterfaces(), GetTickCount64());
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
        if (!notifyPending_.exchange(true) && !PostMessageW(notify_, message_, 0, 0)) notifyPending_ = false;

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
    AddGpuCounters();
    // Physical disks: Task Manager's "active time" is 100 - % Idle Time.
    if (PdhAddEnglishCounterW(query_, L"\\PhysicalDisk(*)\\% Idle Time", 0, &diskIdleCounter_) != ERROR_SUCCESS ||
        PdhAddEnglishCounterW(query_, L"\\PhysicalDisk(*)\\Disk Read Bytes/sec", 0, &diskReadCounter_) !=
            ERROR_SUCCESS ||
        PdhAddEnglishCounterW(query_, L"\\PhysicalDisk(*)\\Disk Write Bytes/sec", 0, &diskWriteCounter_) !=
            ERROR_SUCCESS) {
        diskIdleCounter_ = diskReadCounter_ = diskWriteCounter_ = nullptr;
    }
    return true;
}

void MetricsSampler::AddGpuCounters() {
    // The GPU counter sets can be missing early at logon (driver still loading): added later if so.
    nextCounterRetry_ = GetTickCount64() + kCounterRetryMs;
    if (!query_) return;
    if (!gpuEngineCounter_ && PdhAddEnglishCounterW(query_, L"\\GPU Engine(*)\\Utilization Percentage", 0,
                                                    &gpuEngineCounter_) != ERROR_SUCCESS) {
        gpuEngineCounter_ = nullptr;
    }
    if (!gpuMemoryCounter_ && PdhAddEnglishCounterW(query_, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0,
                                                    &gpuMemoryCounter_) != ERROR_SUCCESS) {
        gpuMemoryCounter_ = nullptr;
    }
}

void MetricsSampler::CloseQuery() {
    if (query_) PdhCloseQuery(query_);
    query_ = nullptr;
    cpuCounter_ = gpuEngineCounter_ = gpuMemoryCounter_ = nullptr;
    diskIdleCounter_ = diskReadCounter_ = diskWriteCounter_ = nullptr;
    gpuSensors_.Close();
    storage_.Close();
    diskInstanceKey_.clear();  // a later Start() enumerates the drives again
    lastTemperatureRead_.clear();
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
                adapters_.push_back({tag, text::Trim(desc.Description), desc.DedicatedVideoMemory, desc.AdapterLuid});
            }
        }
        adapter.Reset();
    }
    std::stable_sort(adapters_.begin(), adapters_.end(),
                     [](const Adapter& a, const Adapter& b) { return a.dedicated > b.dedicated; });
    std::vector<LUID> luids;
    for (const Adapter& a : adapters_) luids.push_back(a.luid);
    gpuSensors_.Open(luids);
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

    CollectGpu(out, collected);
    CollectDisks(out, collected);
    CollectSensors(out);
    out.network = network_.Update(hw::ReadNetInterfaces(), GetTickCount64());
    out.battery = battery_.Read();
}

void MetricsSampler::CollectGpu(MetricsSnapshot& out, bool collected) {
    // Availability is latched once the counters produced data, so a transient failure (or a burst of
    // process churn) reuses the last values instead of making GPU/VRAM blink in and out.
    if (adapters_.empty() && GetTickCount64() >= nextAdapterSearch_) SelectAdapters();
    if ((!gpuEngineCounter_ || !gpuMemoryCounter_) && GetTickCount64() >= nextCounterRetry_) AddGpuCounters();
    auto keepLast = [&] {
        out.gpuAvailable = gpuSeen_;
        out.gpu = lastGpu_.gpu;
        out.vramUsed = lastGpu_.vramUsed;
        out.vramTotal = lastGpu_.vramTotal;
    };

    // Temperature and fan come from the kernel graphics driver, independently of the counters.
    if (!adapters_.empty()) {
        const hw::GpuReading main = gpuSensors_.Read(0);
        out.gpuTemperature = main.temperature;
        out.gpuFanRpm = main.fanRpm;
        out.gpuFanPercent = main.fanPercent;
    }

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

    if (!engineOk && !memoryOk) return keepLast();  // nothing read: not a sign that the adapter changed
    if (!sawAny) {
        // The LUID changes when the driver restarts or updates: enumerate the adapters again.
        if (++adapterMisses_ >= kAdapterMissLimit) SelectAdapters();
        return keepLast();
    }
    adapterMisses_ = 0;
    if (!engineOk) return keepLast();

    gpuSeen_ = true;
    out.gpuAvailable = true;
    out.gpu = busiest;
    // Without the memory counter the load is still shown; VRAM is hidden rather than wrong.
    out.vramTotal = memoryOk ? vramTotal : 0;
    out.vramUsed = memoryOk ? std::min(vramUsed, vramTotal) : 0;
    lastGpu_ = out;
}

void MetricsSampler::CollectDisks(MetricsSnapshot& out, bool collected) {
    if (!collected || !diskIdleCounter_ || !diskReadCounter_ || !diskWriteCounter_) return;
    const PDH_HCOUNTER counters[3] = {diskIdleCounter_, diskReadCounter_, diskWriteCounter_};
    DWORD counts[3] = {};
    for (int i = 0; i < 3; ++i) {
        if (!ReadArray(counters[i], PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, diskBuffers_[i], counts[i])) {
            // A transient read failure: keep the last disks instead of making the section blink.
            out.disks = lastDisks_.disks;
            out.diskActive = lastDisks_.diskActive;
            out.diskReadBps = lastDisks_.diskReadBps;
            out.diskWriteBps = lastDisks_.diskWriteBps;
            out.diskAvailable = lastDisks_.diskAvailable;
            return;
        }
    }
    const auto* idle = reinterpret_cast<const PDH_FMT_COUNTERVALUE_ITEM_W*>(diskBuffers_[0].data());
    const auto* read = reinterpret_cast<const PDH_FMT_COUNTERVALUE_ITEM_W*>(diskBuffers_[1].data());
    const auto* write = reinterpret_cast<const PDH_FMT_COUNTERVALUE_ITEM_W*>(diskBuffers_[2].data());
    // The three arrays list the same instances, but the order is not guaranteed: match by name.
    auto valueOf = [](const PDH_FMT_COUNTERVALUE_ITEM_W* items, DWORD count, const wchar_t* name) {
        for (DWORD i = 0; i < count; ++i) {
            if (items[i].szName && wcscmp(items[i].szName, name) == 0 && IsValidStatus(items[i].FmtValue.CStatus) &&
                std::isfinite(items[i].FmtValue.doubleValue)) {
                return std::max(0.0, items[i].FmtValue.doubleValue);
            }
        }
        return 0.0;
    };

    std::wstring key;
    for (DWORD i = 0; i < counts[0]; ++i) {
        DiskSnapshot disk;
        if (!hw::ParseDiskInstance(idle[i].szName, disk.number, disk.letters)) continue;
        key += idle[i].szName;
        key += L'|';
        const double idlePercent = IsValidStatus(idle[i].FmtValue.CStatus) ? idle[i].FmtValue.doubleValue : 100.0;
        disk.active = std::clamp(100.0 - (std::isfinite(idlePercent) ? idlePercent : 100.0), 0.0, 100.0);
        disk.readBps = static_cast<uint64_t>(valueOf(read, counts[1], idle[i].szName));
        disk.writeBps = static_cast<uint64_t>(valueOf(write, counts[2], idle[i].szName));
        out.disks.push_back(std::move(disk));
    }
    if (key != diskInstanceKey_) {  // a disk was added or removed: read the models again
        diskInstanceKey_ = key;
        std::vector<int> numbers;
        for (const DiskSnapshot& disk : out.disks) numbers.push_back(disk.number);
        storage_.Refresh(numbers);
    }

    const ULONGLONG now = GetTickCount64();
    for (DiskSnapshot& disk : out.disks) {
        // Temperatures are only read while a disk is busy anyway, never waking a sleeping drive.
        const bool busy = disk.readBps > 0 || disk.writeBps > 0 || disk.active >= 1.0;  // not idle-time noise
        ULONGLONG& last = lastTemperatureRead_[disk.number];
        if (busy && now - last >= kDiskTemperatureMs) {
            storage_.UpdateTemperature(disk.number);
            last = now;
        }
        if (const hw::DiskDevice* device = storage_.Find(disk.number)) {
            disk.model = device->model;
            disk.kind = device->kind;
            disk.temperature = device->temperature;
        }
        out.diskActive = std::max(out.diskActive, disk.active);
        out.diskReadBps += disk.readBps;
        out.diskWriteBps += disk.writeBps;
    }
    out.diskAvailable = !out.disks.empty();
    lastDisks_.disks = out.disks;
    lastDisks_.diskActive = out.diskActive;
    lastDisks_.diskReadBps = out.diskReadBps;
    lastDisks_.diskWriteBps = out.diskWriteBps;
    lastDisks_.diskAvailable = out.diskAvailable;
}

namespace {

std::wstring Normalized(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        if (c != L' ' && c != L'-' && c != L'_') out += static_cast<wchar_t>(std::towlower(c));
    }
    return out;
}

// Same device under two names: "Samsung SSD 870 EVO 1TB" (LibreHardwareMonitor) vs the storage IOCTL
// model, or "AMD Radeon RX 6700 XT" (LibreHardwareMonitor) vs the DXGI adapter description.
bool SameDrive(const std::wstring& a, const std::wstring& b) {
    const std::wstring x = Normalized(a), y = Normalized(b);
    return !x.empty() && !y.empty() && (x.find(y) != std::wstring::npos || y.find(x) != std::wstring::npos);
}

}  // namespace

void MetricsSampler::CollectSensors(MetricsSnapshot& out) {
    // Advanced sensors (optional service): CPU temperature, fans, SATA temperatures.
    feed::Decoded advanced;
    std::vector<SensorReading> fans, gpuFans, extraDrives;
    if (feed_ && feed_->Read(advanced)) {
        out.advancedSensors = true;
        // sys-bar updates itself without admin rights; the service (in Program Files) catches up on its own
        // when asked. Version 0 = a service from before the version field: nothing to compare.
        constexpr uint32_t kAppVersion =
            feed::EncodeVersion(SYSBAR_VERSION_MAJOR, SYSBAR_VERSION_MINOR, SYSBAR_VERSION_PATCH);
        const ULONGLONG now = GetTickCount64();
        if (advanced.serviceVersion != 0 && advanced.serviceVersion < kAppVersion && now >= nextServiceUpdate_) {
            feed_->RequestUpdate();
            nextServiceUpdate_ = now + kServiceUpdateRetryMs;
        }
        out.cpuTemperature = advanced.cpuTemperature;
        std::vector<bool> consumed(out.disks.size(), false);
        for (SensorReading& r : advanced.readings) {
            if (r.group == SensorGroup::Cpu) continue;  // already in cpuTemperature
            if (r.kind == SensorKind::Fan) {
                (r.group == SensorGroup::Gpu ? gpuFans : fans).push_back(std::move(r));
                continue;
            }
            if (r.group == SensorGroup::Disk) {
                // Complete the drives the native query could not read (SATA needs SMART/admin). One reading per
                // drive, in order: two drives of the same model each get their own temperature.
                bool matched = false;
                for (size_t i = 0; i < out.disks.size() && !matched; ++i) {
                    if (consumed[i] || !SameDrive(out.disks[i].model, r.name)) continue;
                    consumed[i] = true;
                    matched = true;
                    if (!out.disks[i].temperature) out.disks[i].temperature = r.value;
                }
                if (!matched) extraDrives.push_back(std::move(r));
            }
        }
    }

    if (out.cpuTemperature) {
        out.sensors.push_back({SensorKind::Temperature, SensorGroup::Cpu, L"CPU", *out.cpuTemperature});
    }
    for (size_t i = 0; i < adapters_.size(); ++i) {
        const hw::GpuReading reading =
            i == 0 ? hw::GpuReading{out.gpuTemperature, out.gpuFanRpm, out.gpuFanPercent} : gpuSensors_.Read(i);
        if (reading.temperature) {
            out.sensors.push_back({SensorKind::Temperature, SensorGroup::Gpu, adapters_[i].name, *reading.temperature});
        }
        // The service tells a stopped fan (0 RPM) from a missing one, which D3DKMT cannot: prefer it.
        std::optional<double> fan = reading.fanRpm, duty = reading.fanPercent;
        for (const SensorReading& f : gpuFans) {
            if (SameDrive(f.name, adapters_[i].name) || (adapters_.size() == 1 && gpuFans.size() == 1)) {
                fan = f.value;
                if (f.duty) duty = f.duty;
            }
        }
        if (fan) {
            out.sensors.push_back({SensorKind::Fan, SensorGroup::Gpu, adapters_[i].name, *fan, FanRole::Gpu, duty});
        }
    }
    for (SensorReading& f : fans) out.sensors.push_back(std::move(f));
    for (SensorReading& drive : extraDrives) out.sensors.push_back(std::move(drive));
}