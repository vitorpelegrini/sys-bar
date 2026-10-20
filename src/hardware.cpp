#include "hardware.h"

#include "format.h"

// d3dkmthk.h needs NTSTATUS and the UNICODE_STRING family from winternl.h.
#include <winternl.h>
#include <d3dkmthk.h>
#include <winioctl.h>

#include <cwctype>

namespace hw {

// ---- GPU ----------------------------------------------------------------------------------------

void GpuSensors::Open(const std::vector<LUID>& adapters) {
    Close();
    for (const LUID& luid : adapters) {
        Adapter adapter;
        D3DKMT_OPENADAPTERFROMLUID open{};
        open.AdapterLuid = luid;
        if (D3DKMTOpenAdapterFromLuid(&open) == 0) {
            adapter.handle = open.hAdapter;
        } else {
            adapter.supported = false;
        }
        adapters_.push_back(adapter);
    }
}

void GpuSensors::Close() {
    for (const Adapter& adapter : adapters_) {
        if (adapter.handle) {
            D3DKMT_CLOSEADAPTER close{adapter.handle};
            D3DKMTCloseAdapter(&close);
        }
    }
    adapters_.clear();
}

GpuReading GpuSensors::Read(size_t index) {
    GpuReading reading;
    if (index >= adapters_.size()) return reading;
    Adapter& adapter = adapters_[index];
    if (!adapter.supported || !adapter.handle) return reading;

    D3DKMT_ADAPTER_PERFDATA perf{};
    perf.PhysicalAdapterIndex = 0;
    D3DKMT_QUERYADAPTERINFO query{};
    query.hAdapter = adapter.handle;
    query.Type = KMTQAITYPE_ADAPTERPERFDATA;
    query.pPrivateDriverData = &perf;
    query.PrivateDriverDataSize = sizeof(perf);
    if (D3DKMTQueryAdapterInfo(&query) != 0) {
        adapter.supported = false;  // older Windows or a driver without the data: stop asking
        return reading;
    }
    // Temperature is reported in deci-degrees Celsius; 0 means "not reported".
    if (perf.Temperature > 0 && perf.Temperature < 1500) reading.temperature = perf.Temperature / 10.0;
    if (perf.FanRPM > 0 && perf.FanRPM < 20000) reading.fanRpm = static_cast<double>(perf.FanRPM);
    return reading;
}

// ---- Disks --------------------------------------------------------------------------------------

namespace {

constexpr int kMaxPhysicalDrives = 32;

std::wstring ReadModel(HANDLE file, bool& nvme) {
    STORAGE_PROPERTY_QUERY query{StorageDeviceProperty, PropertyStandardQuery, {0}};
    alignas(8) BYTE buffer[1024] = {};
    DWORD bytes = 0;
    nvme = false;
    if (!DeviceIoControl(file, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer, sizeof(buffer), &bytes,
                         nullptr) ||
        bytes < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        return {};
    }
    const auto* descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer);
    nvme = descriptor->BusType == BusTypeNvme;
    if (!descriptor->ProductIdOffset || descriptor->ProductIdOffset >= bytes) return {};
    const char* product = reinterpret_cast<const char*>(buffer) + descriptor->ProductIdOffset;
    const size_t max = bytes - descriptor->ProductIdOffset;
    const size_t length = strnlen(product, max);
    std::wstring model(length, L' ');
    for (size_t i = 0; i < length; ++i) model[i] = static_cast<unsigned char>(product[i]);
    return text::Trim(model);
}

}  // namespace

void StorageDevices::Refresh() {
    Close();
    for (int n = 0; n < kMaxPhysicalDrives; ++n) {
        wchar_t path[32];
        swprintf_s(path, L"\\\\.\\PhysicalDrive%d", n);
        // No access rights requested: enough for property queries, no administrator needed.
        HANDLE file = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) continue;
        Handle h;
        h.file = file;
        h.device.number = n;
        h.device.model = ReadModel(file, h.device.nvme);
        disks_.push_back(std::move(h));
    }
}

void StorageDevices::Close() {
    for (Handle& h : disks_) {
        if (h.file != INVALID_HANDLE_VALUE) CloseHandle(h.file);
    }
    disks_.clear();
}

void StorageDevices::UpdateTemperature(int number) {
    for (Handle& h : disks_) {
        if (h.device.number != number || !h.device.temperatureSupported) continue;
        STORAGE_PROPERTY_QUERY query{StorageDeviceTemperatureProperty, PropertyStandardQuery, {0}};
        alignas(8) BYTE buffer[512] = {};
        DWORD bytes = 0;
        if (!DeviceIoControl(h.file, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer, sizeof(buffer),
                             &bytes, nullptr) ||
            bytes < sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR)) {
            h.device.temperatureSupported = false;  // e.g. SATA drives that need SMART (admin)
            h.device.temperature.reset();
            return;
        }
        const auto* data = reinterpret_cast<const STORAGE_TEMPERATURE_DATA_DESCRIPTOR*>(buffer);
        // The first entry is the composite/drive temperature.
        if (data->InfoCount > 0) {
            const SHORT celsius = data->TemperatureInfo[0].Temperature;
            if (celsius > -40 && celsius < 150) {
                h.device.temperature = static_cast<double>(celsius);
            } else {
                h.device.temperature.reset();
            }
        }
        return;
    }
}

const DiskDevice* StorageDevices::Find(int number) const {
    for (const Handle& h : disks_) {
        if (h.device.number == number) return &h.device;
    }
    return nullptr;
}

bool ParseDiskInstance(const wchar_t* name, int& number, std::wstring& letters) {
    if (!name || !std::iswdigit(*name)) return false;  // also rejects "_Total"
    number = 0;
    const wchar_t* p = name;
    while (std::iswdigit(*p)) {
        number = number * 10 + (*p - L'0');
        if (number > 9999) return false;
        ++p;
    }
    letters = text::Trim(p);
    return true;
}

}  // namespace hw
