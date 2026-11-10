#include "metrics.h"

#include "format.h"
#include "sensorfeed.h"
#include "version.h"

#include <dxgi.h>

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
constexpr int kDiskMissLimit = 2;              // rounds the last disks are kept when no drive answers

wchar_t LowerAscii(wchar_t c) {
    return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
}

std::wstring LowerAscii(const std::wstring& s) {
    std::wstring out = s;
    for (wchar_t& c : out) c = LowerAscii(c);
    return out;
}

// Finds `needle` (lowercase ASCII) in `haystack` ignoring ASCII case: instance names spell the adapter LUID
// in upper- or lowercase hex depending on the Windows version.
const wchar_t* FindNoCase(const wchar_t* haystack, const std::wstring& needle) {
    if (needle.empty()) return haystack;
    for (const wchar_t* p = haystack; *p; ++p) {
        if (LowerAscii(*p) != needle[0]) continue;
        size_t i = 1;
        while (i < needle.size() && p[i] && LowerAscii(p[i]) == needle[i]) ++i;
        if (i == needle.size()) return p;
    }
    return nullptr;
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

GpuEngineResult AggregateGpuEngines(const CounterItem* items, size_t count, const std::wstring& luidTag,
                                    std::unordered_map<std::wstring_view, double>& scratch) {
    // Instances look like: pid_1234_luid_0x..._0x..._phys_0_eng_3_engtype_3D
    // Sum per engine (over all processes) and report the busiest engine, like Task Manager.
    GpuEngineResult result;
    scratch.clear();
    if (luidTag.empty()) return result;
    const std::wstring tag = LowerAscii(luidTag);
    for (size_t i = 0; i < count; ++i) {
        const wchar_t* name = items[i].name;
        if (!name || !FindNoCase(name, tag)) continue;
        result.sawAdapter = true;
        if (!items[i].valid) continue;
        const wchar_t* phys = wcsstr(name, L"_phys_");
        const wchar_t* type = wcsstr(name, L"_engtype_");
        if (!phys || !type || type < phys) continue;
        const double v = items[i].value;
        if (v > 0.0 && std::isfinite(v)) scratch[std::wstring_view(phys, static_cast<size_t>(type - phys))] += v;
    }
    for (const auto& [key, value] : scratch) result.busiest = std::max(result.busiest, value);
    result.busiest = std::clamp(result.busiest, 0.0, 100.0);
    return result;
}

uint64_t SumAdapterMemory(const CounterItem* items, size_t count, const std::wstring& luidTag, bool& sawAdapter) {
    uint64_t used = 0;
    sawAdapter = false;
    if (luidTag.empty()) return 0;
    const std::wstring tag = LowerAscii(luidTag);
    for (size_t i = 0; i < count; ++i) {
        if (!items[i].name || !FindNoCase(items[i].name, tag)) continue;
        sawAdapter = true;
        if (items[i].valid) used += items[i].large;
    }
    return used;
}

MetricsSampler::MetricsSampler() : feed_(std::make_unique<feed::SensorFeed>()) {}

MetricsSampler::~MetricsSampler() {
    Stop();
}

bool MetricsSampler::Start(HWND notify, UINT message, DWORD intervalMs, UINT sensorsMessage) {
    if (thread_.joinable()) return true;
    notify_ = notify;
    message_ = message;
    sensorsMessage_ = sensorsMessage;
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

MetricsSnapshot MetricsSampler::Peek() const {
    std::lock_guard lock(mutex_);
    return latest_;
}

void MetricsSampler::RefreshSensors() {
    if (!sensorsMessage_ || !wakeEvent_) return;
    sensorsWanted_ = true;
    SetEvent(wakeEvent_);
}

SystemInfo MetricsSampler::Info() const {
    std::lock_guard lock(mutex_);
    return info_;
}

void MetricsSampler::Run() {
    UseEfficiencyMode();
    OpenCounters();
    SelectAdapters();
    Prime();  // rates need two readings

    const HANDLE handles[2] = {stopEvent_, wakeEvent_};
    ULONGLONG nextDue = GetTickCount64() + kFirstSampleDelayMs;

    for (;;) {
        if (paused_) {
            if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0) break;
            if (!paused_) {
                // Resuming: start a fresh measurement window instead of averaging over the pause.
                Prime();
                network_.Update(netReader_.Read(GetTickCount64(), true), GetTickCount64());
                nextDue = GetTickCount64() + kFirstSampleDelayMs;
            }
            continue;
        }

        const ULONGLONG now = GetTickCount64();
        const DWORD wait = nextDue > now ? static_cast<DWORD>(nextDue - now) : 0;
        const DWORD result = WaitForMultipleObjects(2, handles, FALSE, wait);
        if (result == WAIT_OBJECT_0) break;
        if (result == WAIT_OBJECT_0 + 1) {
            if (sensorsWanted_.exchange(false)) {
                // Only the advanced sensors, into the latest snapshot: the sampling cadence is left alone.
                MetricsSnapshot snapshot = Peek();
                if (snapshot.valid) {
                    snapshot.sensors.clear();
                    snapshot.cpuTemperature.reset();
                    snapshot.advancedSensors = false;
                    CollectSensors(snapshot);
                    {
                        std::lock_guard lock(mutex_);
                        latest_.sensors = std::move(snapshot.sensors);
                        latest_.cpuTemperature = snapshot.cpuTemperature;
                        latest_.advancedSensors = snapshot.advancedSensors;
                    }
                    PostMessageW(notify_, sensorsMessage_, 0, 0);
                }
                continue;
            }
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

    CloseCounters();
}

void MetricsSampler::OpenCounters() {
    // CPU: "% Processor Utility", the metric Task Manager shows. It is the growth of the counter over the
    // growth of its base, and goes above 100 with turbo: clamped where it is used.
    perf::CounterRef utility;
    if (perf::Find(L"Processor Information", L"% Processor Utility", utility) && utility.baseId != perf::kNoCounter) {
        cpuUtility_ = perf_.Add(utility.set, utility.id, L"_Total");
        cpuUtilityBase_ = perf_.Add(utility.set, utility.baseId, L"_Total");
    }
    AddGpuCounters();
}

void MetricsSampler::AddGpuCounters() {
    // The GPU counter sets can be missing early at logon (driver still loading): added later if so.
    nextCounterRetry_ = GetTickCount64() + kCounterRetryMs;
    const bool searchAll = !gpuCountersSearched_;
    gpuCountersSearched_ = true;
    perf::CounterRef counter;
    if (gpuEngine_ < 0 && perf::Find(L"GPU Engine", L"Utilization Percentage", counter, searchAll)) {
        gpuEngine_ = perf_.Add(counter.set, counter.id, L"*");
    }
    if (gpuMemory_ < 0 && perf::Find(L"GPU Adapter Memory", L"Dedicated Usage", counter, searchAll)) {
        gpuMemory_ = perf_.Add(counter.set, counter.id, L"*");
    }
}

void MetricsSampler::CloseCounters() {
    perf_.Close();
    cpuUtility_ = cpuUtilityBase_ = gpuEngine_ = gpuMemory_ = -1;
    haveUtility_ = false;
    gpuCountersSearched_ = false;
    engineTotals_.clear();
    engineTime_ = 0;
    diskCounters_.Close();
    gpuSensors_.Close();
    storage_.Close();
    netReader_.Close();
    diskInstanceKey_.clear();  // a later Start() enumerates the drives again
    lastTemperatureRead_.clear();
}

// A fresh measurement window: the next sample's rates and percentages cover the time from now on.
void MetricsSampler::Prime() {
    const bool collected = perf_.Collect();
    double ignored = 0.0;
    ReadUtility(collected, ignored);
    ReadEngines(collected);
    ReadCpuFallback(ignored);
    diskCounters_.Prime(GetTickCount64());
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

// CPU utility since the previous call, in percent (not capped at 100). False when there is no previous
// reading yet or the counter is not available.
bool MetricsSampler::ReadUtility(bool collected, double& percent) {
    const std::vector<perf::Item>* utility = collected ? &perf_.Items(cpuUtility_) : nullptr;
    const std::vector<perf::Item>* base = collected ? &perf_.Items(cpuUtilityBase_) : nullptr;
    if (!utility || utility->empty() || base->empty()) {
        haveUtility_ = false;
        return false;
    }
    uint64_t grew = 0, baseGrew = 0;
    const bool ok = haveUtility_ && perf::Delta(lastUtility_, utility->front(), grew) &&
                    perf::Delta(lastUtilityBase_, base->front(), baseGrew) && baseGrew > 0;
    if (ok) percent = static_cast<double>(grew) / static_cast<double>(baseGrew);
    lastUtility_ = utility->front();
    lastUtilityBase_ = base->front();
    lastUtility_.name = lastUtilityBase_.name = L"";  // the block they point into is reused
    haveUtility_ = true;
    return ok;
}

// Turns the running time of every GPU engine instance into its utilization since the previous call
// (engineItems_). False when there is no previous reading to compare with.
bool MetricsSampler::ReadEngines(bool collected) {
    engineItems_.clear();
    if (!collected || gpuEngine_ < 0) {
        engineTime_ = 0;  // the next reading starts over
        return false;
    }
    const std::vector<perf::Item>& now = perf_.Items(gpuEngine_);
    const int64_t time = perf_.Time100ns();
    const double elapsed = engineTime_ ? static_cast<double>(time - engineTime_) : 0.0;
    const bool rates = elapsed > 0.0;

    // Instances keep their order from one sample to the next unless a process came or went: compared in
    // place first, and looked up by name only for those that moved.
    engineIndex_.clear();
    bool indexed = false;
    engineItems_.reserve(now.size());
    for (size_t i = 0; i < now.size(); ++i) {
        CounterItem item;
        item.name = now[i].name;
        item.valid = false;
        if (rates) {
            const EngineTotal* before = nullptr;
            if (i < engineTotals_.size() && engineTotals_[i].name == now[i].name) {
                before = &engineTotals_[i];
            } else {
                if (!indexed) {
                    for (size_t k = 0; k < engineTotals_.size(); ++k) engineIndex_.emplace(engineTotals_[k].name, k);
                    indexed = true;
                }
                if (const auto found = engineIndex_.find(now[i].name); found != engineIndex_.end()) {
                    before = &engineTotals_[found->second];
                }
            }
            uint64_t grew = 0;
            if (before && perf::Delta(before->total, now[i], grew)) {
                item.value = 100.0 * static_cast<double>(grew) / elapsed;
                item.valid = true;
            }
        }
        engineItems_.push_back(item);
    }
    engineIndex_.clear();  // it points into the names replaced below

    // Remember this reading. Names are only copied when they changed: no allocation on a quiet system.
    engineTotals_.resize(now.size());
    for (size_t i = 0; i < now.size(); ++i) {
        if (engineTotals_[i].name != now[i].name) engineTotals_[i].name = now[i].name;
        engineTotals_[i].total = now[i];
        engineTotals_[i].total.name = L"";  // the block it points into is reused
    }
    engineTime_ = time;
    return rates;
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
    const bool collected = perf_.Collect();

    // CPU: utility when the counter is there, else plain processor time.
    double utility = 0.0;
    const bool cpuOk = ReadUtility(collected, utility);
    if (cpuOk) out.cpu = utility;
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
    CollectDisks(out);
    CollectSensors(out);
    const ULONGLONG tick = GetTickCount64();
    out.network = network_.Update(netReader_.Read(tick), tick);
    out.battery = battery_.Read();
}

void MetricsSampler::CollectGpu(MetricsSnapshot& out, bool collected) {
    // Availability is latched once the counters produced data, so a transient failure (or a burst of
    // process churn) reuses the last values instead of making GPU/VRAM blink in and out.
    if (adapters_.empty() && GetTickCount64() >= nextAdapterSearch_) SelectAdapters();
    if ((gpuEngine_ < 0 || gpuMemory_ < 0) && GetTickCount64() >= nextCounterRetry_) AddGpuCounters();
    const bool engineRates = ReadEngines(collected);  // every sample, so each covers exactly one interval
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

    if (engineRates) {
        engineOk = true;
        for (const Adapter& adapter : adapters_) {
            const auto result = AggregateGpuEngines(engineItems_.data(), engineItems_.size(), adapter.tag, engines_);
            sawAny |= result.sawAdapter;
            busiest = std::max(busiest, result.busiest);
        }
    }
    if (gpuMemory_ >= 0) {
        memoryOk = true;
        memoryItems_.clear();
        for (const perf::Item& item : perf_.Items(gpuMemory_))
            memoryItems_.push_back({item.name, 0.0, item.value, true});
        bool saw = false;
        vramUsed = SumAdapterMemory(memoryItems_.data(), memoryItems_.size(), main.tag, saw);
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

void MetricsSampler::CollectDisks(MetricsSnapshot& out) {
    const ULONGLONG now = GetTickCount64();
    if (disksChanged_.exchange(false)) diskCounters_.Invalidate();
    const std::vector<hw::DiskActivity> activity = diskCounters_.Read(now);
    if (activity.empty()) {
        // No drive answered (they are being listed again): keep the last disks for a moment instead of
        // making the section blink.
        if (lastDisks_.diskAvailable && ++diskMisses_ <= kDiskMissLimit) {
            out.disks = lastDisks_.disks;
            out.diskActive = lastDisks_.diskActive;
            out.diskReadBps = lastDisks_.diskReadBps;
            out.diskWriteBps = lastDisks_.diskWriteBps;
            out.diskAvailable = lastDisks_.diskAvailable;
        } else {
            lastDisks_ = {};
        }
        return;
    }
    diskMisses_ = 0;

    std::wstring key;
    for (const hw::DiskActivity& drive : activity) {
        DiskSnapshot disk;
        disk.number = drive.number;
        disk.letters = drive.letters;
        disk.active = drive.active;
        disk.readBps = drive.readBps;
        disk.writeBps = drive.writeBps;
        key += std::to_wstring(drive.number) + L' ' + drive.letters + L'|';
        out.disks.push_back(std::move(disk));
    }
    if (key != diskInstanceKey_) {  // a disk was added or removed: read the models again
        diskInstanceKey_ = key;
        std::vector<int> numbers;
        for (const DiskSnapshot& disk : out.disks) numbers.push_back(disk.number);
        storage_.Refresh(numbers);
    }

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