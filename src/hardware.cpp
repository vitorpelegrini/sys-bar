#include "hardware.h"

#include "format.h"

// d3dkmthk.h needs NTSTATUS and the UNICODE_STRING family from winternl.h.
#include <winternl.h>
#include <d3dkmthk.h>
#include <winioctl.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>
#include <batclass.h>

#include <algorithm>

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

// A drive "incurs a seek penalty" when it has moving heads: that is how Windows itself tells HDD from SSD.
std::optional<bool> SeekPenalty(HANDLE file) {
    STORAGE_PROPERTY_QUERY query{StorageDeviceSeekPenaltyProperty, PropertyStandardQuery, {0}};
    DEVICE_SEEK_PENALTY_DESCRIPTOR descriptor{};
    DWORD bytes = 0;
    if (!DeviceIoControl(file, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &descriptor, sizeof(descriptor),
                         &bytes, nullptr) ||
        bytes < sizeof(descriptor)) {
        return std::nullopt;
    }
    return descriptor.IncursSeekPenalty != FALSE;
}

std::wstring ReadModel(HANDLE file, DiskKind& kind) {
    STORAGE_PROPERTY_QUERY query{StorageDeviceProperty, PropertyStandardQuery, {0}};
    alignas(8) BYTE buffer[1024] = {};
    DWORD bytes = 0;
    kind = DiskKind::Unknown;
    if (!DeviceIoControl(file, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer, sizeof(buffer), &bytes,
                         nullptr) ||
        bytes < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        return {};
    }
    const auto* descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer);
    if (descriptor->BusType == BusTypeNvme) {
        kind = DiskKind::Nvme;
    } else if (descriptor->BusType == BusTypeUsb) {
        kind = DiskKind::Usb;
    } else if (const auto penalty = SeekPenalty(file)) {
        kind = *penalty ? DiskKind::Hdd : DiskKind::Ssd;
    }
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
        h.device.model = ReadModel(file, h.device.kind);
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

const wchar_t* DiskKindName(DiskKind kind) {
    switch (kind) {
        case DiskKind::Nvme: return L"NVMe";
        case DiskKind::Ssd: return L"SSD";
        case DiskKind::Hdd: return L"HDD";
        case DiskKind::Usb: return L"USB";
        default: return L"";
    }
}

// ---- Network --------------------------------------------------------------------------------------

std::vector<NetInterface> ReadNetInterfaces() {
    std::vector<NetInterface> out;
    MIB_IF_TABLE2* table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || !table) return out;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2& row = table->Table[i];
        if (!row.InterfaceAndOperStatusFlags.HardwareInterface || row.InterfaceAndOperStatusFlags.FilterInterface ||
            row.OperStatus != IfOperStatusUp || row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.Type == IF_TYPE_TUNNEL) {
            continue;
        }
        NetInterface item;
        item.luid = row.InterfaceLuid.Value;
        item.name = row.Alias;
        item.inOctets = row.InOctets;
        item.outOctets = row.OutOctets;
        item.linkBps = std::max(row.ReceiveLinkSpeed, row.TransmitLinkSpeed);
        out.push_back(std::move(item));
    }
    FreeMibTable(table);
    return out;
}

NetRates NetworkMeter::Update(const std::vector<NetInterface>& now, ULONGLONG tickMs) {
    NetRates rates;
    rates.available = !now.empty();
    const double seconds = lastTick_ && tickMs > lastTick_ ? static_cast<double>(tickMs - lastTick_) / 1000.0 : 0.0;
    double down = 0.0, up = 0.0;
    std::unordered_map<uint64_t, NetInterface> next;
    for (const NetInterface& item : now) {
        if (const auto it = last_.find(item.luid); it != last_.end() && seconds > 0.0) {
            // A counter that went backwards was reset (adapter restarted): no rate for this round.
            if (item.inOctets >= it->second.inOctets) down += static_cast<double>(item.inOctets - it->second.inOctets);
            if (item.outOctets >= it->second.outOctets)
                up += static_cast<double>(item.outOctets - it->second.outOctets);
        }
        rates.linkBps = std::max(rates.linkBps, item.linkBps);
        if (rates.names.find(item.name) == std::wstring::npos) {
            rates.names += (rates.names.empty() ? L"" : L" + ") + item.name;
        }
        next.emplace(item.luid, item);
    }
    if (seconds > 0.0) {
        rates.downBps = static_cast<uint64_t>(down / seconds);
        rates.upBps = static_cast<uint64_t>(up / seconds);
    }
    last_ = std::move(next);
    lastTick_ = tickMs;
    return rates;
}

// ---- Battery --------------------------------------------------------------------------------------

void BatteryMeter::Open() {
    opened_ = true;
    HDEVINFO info =
        SetupDiGetClassDevsW(&GUID_DEVCLASS_BATTERY, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (info == INVALID_HANDLE_VALUE) return;
    for (DWORD index = 0; index < 8; ++index) {
        SP_DEVICE_INTERFACE_DATA data{sizeof(data)};
        if (!SetupDiEnumDeviceInterfaces(info, nullptr, &GUID_DEVCLASS_BATTERY, index, &data)) break;
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(info, &data, nullptr, 0, &required, nullptr);
        if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
        std::vector<BYTE> buffer(required);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(info, &data, detail, required, nullptr, nullptr)) continue;
        Device device;
        device.file = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (device.file == INVALID_HANDLE_VALUE) continue;
        DWORD wait = 0, bytes = 0;
        if (DeviceIoControl(device.file, IOCTL_BATTERY_QUERY_TAG, &wait, sizeof(wait), &device.tag, sizeof(device.tag),
                            &bytes, nullptr) &&
            device.tag != 0) {
            devices_.push_back(device);
        } else {
            CloseHandle(device.file);
        }
    }
    SetupDiDestroyDeviceInfoList(info);
}

void BatteryMeter::Close() {
    for (const Device& device : devices_) CloseHandle(device.file);
    devices_.clear();
    opened_ = false;
}

BatteryReading BatteryMeter::Read() {
    BatteryReading reading;
    SYSTEM_POWER_STATUS power{};
    // BatteryFlag 128 = no system battery, 255 = unknown status.
    if (!GetSystemPowerStatus(&power) || power.BatteryFlag == 128 || power.BatteryFlag == 255 ||
        power.BatteryLifePercent > 100) {
        if (opened_) Close();
        return reading;
    }
    reading.present = true;
    reading.percent = power.BatteryLifePercent;
    reading.acOnline = power.ACLineStatus == 1;
    reading.charging = (power.BatteryFlag & 8) != 0;
    if (!reading.acOnline && power.BatteryLifeTime != static_cast<DWORD>(-1)) {
        reading.secondsLeft = static_cast<int>(power.BatteryLifeTime);
    }

    if (!opened_) Open();
    double milliwatts = 0.0;
    bool known = false;
    for (const Device& device : devices_) {
        BATTERY_WAIT_STATUS wait{};
        wait.BatteryTag = device.tag;
        BATTERY_STATUS status{};
        DWORD bytes = 0;
        if (!DeviceIoControl(device.file, IOCTL_BATTERY_QUERY_STATUS, &wait, sizeof(wait), &status, sizeof(status),
                             &bytes, nullptr)) {
            Close();  // the battery went away or its tag changed: enumerate again next time
            break;
        }
        if (status.Rate == static_cast<LONG>(BATTERY_UNKNOWN_RATE) || status.Rate == 0) continue;
        milliwatts += status.Rate;
        known = true;
    }
    if (known && std::abs(milliwatts) < 500'000.0) reading.watts = milliwatts / 1000.0;
    return reading;
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
