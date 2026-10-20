#pragma once

#include "common.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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
struct DiskDevice {
    int number = -1;     // \\.\PhysicalDriveN
    std::wstring model;  // "Samsung SSD 980 PRO 1TB"
    bool nvme = false;
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

}  // namespace hw
