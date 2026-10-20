#pragma once

#include "common.h"
#include "hardware.h"

namespace feed {
class SensorFeed;
}

#include <pdh.h>

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
    bool nvme = false;
};

enum class SensorKind { Temperature, Fan };
enum class SensorGroup { Cpu, Gpu, Disk, Board };

// A reading for the card's "Sensors" section.
struct SensorReading {
    SensorKind kind = SensorKind::Temperature;
    SensorGroup group = SensorGroup::Cpu;
    std::wstring name;   // "GPU", "NVMe · GVR1TB", "CPU fan"
    double value = 0.0;  // °C or RPM
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

    bool advancedSensors = false;          // the advanced sensors service is publishing
    std::optional<double> cpuTemperature;  // °C (advanced sensors)
    std::optional<double> gpuTemperature;  // °C, main GPU
    std::optional<double> gpuFanRpm;
    std::vector<SensorReading> sensors;  // everything known, for the card
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

// Aggregation of the "GPU Engine" counters (exposed for the tests).
GpuEngineResult AggregateGpuEngines(const PDH_FMT_COUNTERVALUE_ITEM_W* items, DWORD count, const std::wstring& luidTag,
                                    std::unordered_map<std::wstring_view, double>& scratch);
uint64_t SumAdapterMemory(const PDH_FMT_COUNTERVALUE_ITEM_W* items, DWORD count, const std::wstring& luidTag,
                          bool& sawAdapter);

// Minimum dedicated memory for a GPU to be treated as discrete (below that, VRAM is hidden).
constexpr uint64_t kMinDedicatedVram = 256ull * 1024 * 1024;

// Samples the metrics on its own thread and notifies the UI with a (coalesced) PostMessage.
// The UI thread never waits for PDH: our window is a child of the Explorer taskbar and both
// threads share the input queue, so any stall on the UI thread would freeze the taskbar too.
class MetricsSampler {
public:
    MetricsSampler();
    ~MetricsSampler();
    MetricsSampler(const MetricsSampler&) = delete;
    MetricsSampler& operator=(const MetricsSampler&) = delete;

    bool Start(HWND notify, UINT message, DWORD intervalMs);
    void Stop();
    void SetInterval(DWORD intervalMs);
    // Paused while the display is off or the session is locked: no sampling, no wake-ups.
    void SetPaused(bool paused);

    // Returns the latest snapshot and re-arms the notification.
    MetricsSnapshot Latest();
    SystemInfo Info() const;

private:
    struct Adapter {
        std::wstring tag;  // "luid_0x00000000_0x0005e13d"
        std::wstring name;
        uint64_t dedicated = 0;
        LUID luid{};
    };

    void Run();
    bool OpenQuery();
    void CloseQuery();
    bool SelectAdapters();
    void Collect(MetricsSnapshot& out);
    void CollectGpu(MetricsSnapshot& out, bool collected);
    void CollectDisks(MetricsSnapshot& out, bool collected);
    void CollectSensors(MetricsSnapshot& out);
    bool ReadCpuFallback(double& value);
    bool ReadArray(PDH_HCOUNTER counter, DWORD format, std::vector<BYTE>& buffer, DWORD& count);

    HWND notify_ = nullptr;
    UINT message_ = 0;
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
    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER cpuCounter_ = nullptr;
    PDH_HCOUNTER gpuEngineCounter_ = nullptr;
    PDH_HCOUNTER gpuMemoryCounter_ = nullptr;
    PDH_HCOUNTER diskIdleCounter_ = nullptr;
    PDH_HCOUNTER diskReadCounter_ = nullptr;
    PDH_HCOUNTER diskWriteCounter_ = nullptr;
    std::vector<Adapter> adapters_;  // physical GPUs, main (largest dedicated memory) first
    ULONGLONG nextAdapterSearch_ = 0;
    int adapterMisses_ = 0;
    bool gpuSeen_ = false;     // latched: GPU counters have produced data at least once
    MetricsSnapshot lastGpu_;  // last good GPU values, reused on a transient read failure
    std::vector<BYTE> engineBuffer_;
    std::vector<BYTE> memoryBuffer_;
    std::vector<BYTE> diskBuffers_[3];
    hw::GpuSensors gpuSensors_;
    hw::StorageDevices storage_;
    std::unique_ptr<feed::SensorFeed> feed_;                  // advanced sensors service (optional)
    std::unordered_map<int, ULONGLONG> lastTemperatureRead_;  // disk number -> tick
    std::wstring diskInstanceKey_;  // set of PDH disk instances; a change triggers re-enumeration
    std::unordered_map<std::wstring_view, double> engines_;
    ULARGE_INTEGER lastIdle_{}, lastKernel_{}, lastUser_{};
    bool haveCpuFallback_ = false;
};
