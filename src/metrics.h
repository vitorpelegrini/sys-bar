#pragma once

#include "common.h"
#include "hardware.h"
#include "perf.h"

namespace feed {
class SensorFeed;
}

#include <atomic>
#include <memory>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

struct DiskSnapshot {
    int number = 0;
    std::wstring letters;  // "C: E:"
    std::wstring model;
    double active = 0.0;  // 0..100, Task Manager "active time"
    uint64_t readBps = 0;
    uint64_t writeBps = 0;
    std::optional<double> temperature;  // °C
    hw::DiskKind kind = hw::DiskKind::Unknown;
};

enum class SensorKind { Temperature, Fan };
enum class SensorGroup { Cpu, Gpu, Disk, Board };

// A reading for the card's "Sensors" section.
struct SensorReading {
    SensorKind kind = SensorKind::Temperature;
    SensorGroup group = SensorGroup::Cpu;
    std::wstring name;   // "CPU", GPU adapter name, "Fan #2", SATA drive model
    double value = 0.0;  // °C or RPM (a fan at 0 RPM is stopped)
    FanRole role = FanRole::Unknown;
    std::optional<double> duty;  // fans: how hard it is driven, 0..100 %
};

struct MetricsSnapshot {
    bool valid = false;

    double cpu = 0.0;  // 0..100, same metric as Task Manager (% Processor Utility)

    uint64_t ramUsed = 0;
    uint64_t ramTotal = 0;

    bool gpuAvailable = false;
    double gpu = 0.0;        // 0..100, busiest engine of any physical GPU (Task Manager semantics)
    uint64_t vramUsed = 0;   // dedicated memory of the main GPU
    uint64_t vramTotal = 0;  // 0 for integrated GPUs (no meaningful dedicated memory)

    bool diskAvailable = false;
    double diskActive = 0.0;   // busiest physical disk, 0..100
    uint64_t diskReadBps = 0;  // all physical disks
    uint64_t diskWriteBps = 0;
    std::vector<DiskSnapshot> disks;

    hw::NetRates network;  // physical adapters
    hw::BatteryReading battery;

    bool advancedSensors = false;          // the advanced sensors service is publishing
    std::optional<double> cpuTemperature;  // °C (advanced sensors)
    std::optional<double> gpuTemperature;  // °C, main GPU
    std::optional<double> gpuFanRpm;
    std::optional<double> gpuFanPercent;
    std::vector<SensorReading> sensors;  // CPU, GPUs, fans and service-only drives (disks have their own)
};

struct SystemInfo {
    std::wstring cpuName;
    unsigned logicalCpus = 0;
    std::wstring gpuName;  // main GPU first, then any other physical GPUs ("A + B")
};

struct GpuEngineResult {
    double busiest = 0.0;     // 0..100
    bool sawAdapter = false;  // at least one instance belonged to the adapter
};

// One instance of a GPU counter, ready to be aggregated.
struct CounterItem {
    const wchar_t* name = nullptr;  // "pid_1234_luid_0x00000000_0x0005E13D_phys_0_eng_3_engtype_3D"
    double value = 0.0;             // engines: utilization since the previous sample, percent
    uint64_t large = 0;             // memory: bytes
    bool valid = true;              // false: no value this round (a new instance has no previous reading)
};

// Aggregation of the "GPU Engine" counters (exposed for the tests).
GpuEngineResult AggregateGpuEngines(const CounterItem* items, size_t count, const std::wstring& luidTag,
                                    std::unordered_map<std::wstring_view, double>& scratch);
uint64_t SumAdapterMemory(const CounterItem* items, size_t count, const std::wstring& luidTag, bool& sawAdapter);

// Minimum dedicated memory for a GPU to be treated as discrete (below that, VRAM is hidden).
constexpr uint64_t kMinDedicatedVram = 256ull * 1024 * 1024;

// Samples the metrics on its own thread and notifies the UI with a (coalesced) PostMessage.
// The UI thread never waits for a counter or a device: our window is a child of the Explorer taskbar and both
// threads share the input queue, so any stall on the UI thread would freeze the taskbar too.
class MetricsSampler {
public:
    MetricsSampler();
    ~MetricsSampler();
    MetricsSampler(const MetricsSampler&) = delete;
    MetricsSampler& operator=(const MetricsSampler&) = delete;

    // `message` is posted for each sample; `sensorsMessage` when only the advanced sensors were read again.
    bool Start(HWND notify, UINT message, DWORD intervalMs, UINT sensorsMessage = 0);
    void Stop();
    void SetInterval(DWORD intervalMs);
    // Paused while the display is off or the session is locked: no sampling, no wake-ups.
    void SetPaused(bool paused);

    // Returns the latest snapshot and re-arms the notification.
    MetricsSnapshot Latest();
    // The latest snapshot, without re-arming anything (for `sensorsMessage`).
    MetricsSnapshot Peek() const;
    // Reads the advanced sensors again now, outside the sampling cadence (the card is opening and the service
    // was just asked for fresh fans); `sensorsMessage` follows.
    void RefreshSensors();
    // A storage device arrived or left (WM_DEVICECHANGE): the next sample lists the drives again.
    void DisksChanged() { disksChanged_ = true; }
    SystemInfo Info() const;

private:
    struct Adapter {
        std::wstring tag;  // "luid_0x00000000_0x0005e13d"
        std::wstring name;
        uint64_t dedicated = 0;
        LUID luid{};
    };

    void Run();
    void OpenCounters();
    void CloseCounters();
    void Prime();
    bool SelectAdapters();
    void Collect(MetricsSnapshot& out);
    void CollectGpu(MetricsSnapshot& out, bool collected);
    void CollectDisks(MetricsSnapshot& out);
    void CollectSensors(MetricsSnapshot& out);
    bool ReadUtility(bool collected, double& percent);
    bool ReadEngines(bool collected);
    bool ReadCpuFallback(double& value);
    void AddGpuCounters();

    HWND notify_ = nullptr;
    UINT message_ = 0;
    UINT sensorsMessage_ = 0;
    std::atomic<bool> sensorsWanted_{false};
    std::atomic<bool> disksChanged_{false};
    std::atomic<DWORD> intervalMs_{1000};
    std::atomic<bool> paused_{false};
    std::atomic<bool> notifyPending_{false};
    HANDLE stopEvent_ = nullptr;
    HANDLE wakeEvent_ = nullptr;
    std::thread thread_;

    mutable std::mutex mutex_;
    MetricsSnapshot latest_;
    SystemInfo info_;

    // Owned by the sampling thread.
    perf::Query perf_;  // CPU utility, GPU engines and GPU memory, read with one call per sample
    int cpuUtility_ = -1, cpuUtilityBase_ = -1, gpuEngine_ = -1, gpuMemory_ = -1;  // handles in perf_
    perf::Item lastUtility_, lastUtilityBase_;
    bool haveUtility_ = false;
    bool gpuCountersSearched_ = false;  // the costly search for the GPU counter sets is only made once
    // Running time of every GPU engine instance at the previous sample: utilization is its growth.
    struct EngineTotal {
        std::wstring name;
        perf::Item total;
    };
    std::vector<EngineTotal> engineTotals_;
    int64_t engineTime_ = 0;  // when engineTotals_ was read (100 ns units); 0 = no baseline
    std::unordered_map<std::wstring_view, size_t> engineIndex_;  // by name, when the instances changed order
    std::vector<CounterItem> engineItems_;
    std::vector<CounterItem> memoryItems_;
    hw::DiskCounters diskCounters_;
    int diskMisses_ = 0;             // samples in a row without any drive answering
    std::vector<Adapter> adapters_;  // physical GPUs, main (largest dedicated memory) first
    ULONGLONG nextAdapterSearch_ = 0;
    ULONGLONG nextCounterRetry_ = 0;
    MetricsSnapshot lastDisks_;  // last good disk values, reused on a transient read failure
    int adapterMisses_ = 0;
    bool gpuSeen_ = false;     // latched: GPU counters have produced data at least once
    MetricsSnapshot lastGpu_;  // last good GPU values, reused on a transient read failure
    hw::GpuSensors gpuSensors_;
    hw::StorageDevices storage_;
    hw::NetworkMeter network_;
    hw::NetInterfaceReader netReader_;
    hw::BatteryMeter battery_;
    std::unique_ptr<feed::SensorFeed> feed_;                  // advanced sensors service (optional)
    ULONGLONG nextServiceUpdate_ = 0;                         // next time an outdated service may be asked
    std::unordered_map<int, ULONGLONG> lastTemperatureRead_;  // disk number -> tick
    std::wstring diskInstanceKey_;  // the drives and their letters; a change reads the models again
    std::unordered_map<std::wstring_view, double> engines_;
    ULARGE_INTEGER lastIdle_{}, lastKernel_{}, lastUser_{};
    bool haveCpuFallback_ = false;
};
