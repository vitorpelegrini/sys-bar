#pragma once

#include "common.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// What a fan cools (sensors/SharedBlock.cs: Role). Unknown for unlabeled motherboard headers.
enum class FanRole { Unknown, Cpu, Gpu, Case, Pump, Psu };

// Native hardware sensors that need neither a kernel driver nor administrator rights.
namespace hw {

// ---- GPU (WDDM 2.7+, Windows 10 2004+): the same source as Task Manager's GPU temperature ----------
struct GpuReading {
    std::optional<double> temperature;  // °C
    std::optional<double> fanRpm;
};

// Keeps kernel-mode adapter handles open (cheap) and queries D3DKMT adapter performance data.
class GpuSensors {
public:
    GpuSensors() = default;
    ~GpuSensors() { Close(); }
    GpuSensors(const GpuSensors&) = delete;
    GpuSensors& operator=(const GpuSensors&) = delete;

    void Open(const std::vector<LUID>& adapters);
    void Close();
    GpuReading Read(size_t index);  // index into the list given to Open()

private:
    struct Adapter {
        UINT handle = 0;
        bool supported = true;
    };
    std::vector<Adapter> adapters_;
};

// ---- Physical disks ---------------------------------------------------------------------------
enum class DiskKind { Unknown, Nvme, Ssd, Hdd, Usb };

// Short, universal name of the kind ("NVMe", "SSD", "HDD", "USB"); empty when unknown.
const wchar_t* DiskKindName(DiskKind kind);
struct DiskDevice {
    int number = -1;     // \\.\PhysicalDriveN
    std::wstring model;  // "Samsung SSD 980 PRO 1TB"
    DiskKind kind = DiskKind::Unknown;
    bool temperatureSupported = true;
    std::optional<double> temperature;  // °C, last value read
};

// Model names and temperatures (IOCTL_STORAGE_QUERY_PROPERTY works without administrator rights
// for NVMe and many SSDs; drives that do not answer are skipped from then on).
class StorageDevices {
public:
    StorageDevices() = default;
    ~StorageDevices() { Close(); }
    StorageDevices(const StorageDevices&) = delete;
    StorageDevices& operator=(const StorageDevices&) = delete;

    void Refresh();  // re-enumerates \\.\PhysicalDrive0..N
    void Close();
    // Reads the temperature of disk `number`. Callers only do it while the disk is active, so a
    // sleeping hard drive is never woken up just to report its temperature.
    void UpdateTemperature(int number);
    const DiskDevice* Find(int number) const;

private:
    struct Handle {
        DiskDevice device;
        HANDLE file = INVALID_HANDLE_VALUE;
    };
    std::vector<Handle> disks_;
};

// "0 C: E:" -> number 0, letters "C: E:". Returns false for "_Total" and malformed names.
bool ParseDiskInstance(const wchar_t* name, int& number, std::wstring& letters);

// ---- Network ------------------------------------------------------------------------------------
struct NetInterface {
    uint64_t luid = 0;
    std::wstring name;  // "Ethernet", "Wi-Fi"
    uint64_t inOctets = 0, outOctets = 0;
    uint64_t linkBps = 0;  // bits per second
    bool wireless = false;
};

// Physical interfaces that are up: no loopback, tunnels, virtual switch ports or filter drivers, so
// traffic routed through a Hyper-V/WSL switch is counted once, on the real adapter.
std::vector<NetInterface> ReadNetInterfaces();

struct NetRates {
    bool available = false;  // at least one physical interface is up
    uint64_t downBps = 0, upBps = 0;
    uint64_t linkBps = 0;   // fastest link, bits per second
    std::wstring names;     // "Ethernet" / "Ethernet + Wi-Fi"
    bool wireless = false;  // every active adapter is Wi-Fi
};

// Turns the cumulative interface counters into rates between two calls.
class NetworkMeter {
public:
    NetRates Update(const std::vector<NetInterface>& now, ULONGLONG tickMs);

private:
    std::unordered_map<uint64_t, NetInterface> last_;
    ULONGLONG lastTick_ = 0;
};

// ---- Battery ------------------------------------------------------------------------------------
struct BatteryReading {
    bool present = false;
    double percent = 0.0;  // 0..100
    bool acOnline = false;
    bool charging = false;
    std::optional<int> secondsLeft;  // on battery, when Windows has an estimate
    std::optional<double> watts;     // > 0 charging, < 0 discharging
};

// What GetSystemPowerStatus says about the battery (no battery, unknown state and bogus values handled).
BatteryReading FromPowerStatus(const SYSTEM_POWER_STATUS& power);
// "percent,ac,charging[,watts[,secondsLeft]]" (e.g. "18,0,0,-9.2,3000"): a simulated battery, for
// testing the battery display on a desktop through the SYSBAR_FAKE_BATTERY environment variable.
bool ParseFakeBattery(const std::wstring& text, BatteryReading& out);

// Laptop battery: charge from GetSystemPowerStatus, power from the battery driver (no admin needed).
class BatteryMeter {
public:
    BatteryMeter() = default;
    ~BatteryMeter() { Close(); }
    BatteryMeter(const BatteryMeter&) = delete;
    BatteryMeter& operator=(const BatteryMeter&) = delete;

    BatteryReading Read();
    void Close();

private:
    struct Device {
        HANDLE file = INVALID_HANDLE_VALUE;
        ULONG tag = 0;
    };
    void Open();
    std::vector<Device> devices_;
    bool opened_ = false;
};

}  // namespace hw
