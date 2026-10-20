#include "sensorfeed.h"

#include <atomic>
#include <cmath>
#include <cstring>

namespace feed {
namespace {

constexpr wchar_t kMappingName[] = L"Global\\SysBarSensors";
constexpr wchar_t kWantedEventName[] = L"Global\\SysBarSensorsWanted";
constexpr wchar_t kServiceName[] = L"SysBarSensors";
constexpr ULONGLONG kReopenMs = 10'000;  // the service is looked for again every 10 s

template <typename T>
T Get(const uint8_t* p, size_t offset) {
    T value;
    std::memcpy(&value, p + offset, sizeof(T));
    return value;
}

}  // namespace

bool Decode(const uint8_t* block, size_t size, int64_t nowFileTime, Decoded& out) {
    out = {};
    if (!block || size < kBlockSize) return false;
    if (Get<uint32_t>(block, 0) != kMagic || Get<uint32_t>(block, 4) != kVersion) return false;
    if (Get<uint32_t>(block, 8) & 1u) return false;  // being written
    const uint32_t count = Get<uint32_t>(block, 12);
    if (count > kMaxEntries) return false;
    const int64_t updated = Get<int64_t>(block, 16);
    if (updated <= 0 || nowFileTime - updated > kMaxAge100ns || updated - nowFileTime > kMaxAge100ns) return false;

    const float cpu = Get<float>(block, 24);
    if (std::isfinite(cpu) && cpu > 0.0f && cpu < 150.0f) out.cpuTemperature = cpu;

    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = block + kHeaderSize + static_cast<size_t>(i) * kEntrySize;
        const uint8_t kind = e[0];
        const uint8_t group = e[1];
        const float value = Get<float>(e, 4);
        if (kind > 1 || group > 3 || !std::isfinite(value)) continue;
        const bool fan = kind == 1;
        if (fan ? (value < 0.0f || value >= 20000.0f) : (value <= 0.0f || value >= 150.0f)) continue;

        std::wstring name;
        for (size_t c = 0; c < kNameChars; ++c) {
            const auto ch = Get<wchar_t>(e, 8 + c * sizeof(wchar_t));
            if (ch == L'\0') break;
            name += (ch < 0x20) ? L' ' : ch;  // no control characters from another process
        }
        if (name.empty()) continue;
        SensorReading reading;
        reading.kind = fan ? SensorKind::Fan : SensorKind::Temperature;
        reading.group = static_cast<SensorGroup>(group);
        reading.name = std::move(name);
        reading.value = value;
        out.readings.push_back(std::move(reading));
    }
    return true;
}

void SensorFeed::Open() {
    nextOpen_ = GetTickCount64() + kReopenMs;
    mapping_ = OpenFileMappingW(FILE_MAP_READ, FALSE, kMappingName);
    if (!mapping_) return;
    view_ = static_cast<const uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, kBlockSize));
    if (!view_) {
        Close();
        return;
    }
    wanted_ = OpenEventW(EVENT_MODIFY_STATE, FALSE, kWantedEventName);
}

void SensorFeed::Close() {
    if (view_) UnmapViewOfFile(view_);
    if (mapping_) CloseHandle(mapping_);
    if (wanted_) CloseHandle(wanted_);
    view_ = nullptr;
    mapping_ = nullptr;
    wanted_ = nullptr;
}

bool SensorFeed::Read(Decoded& out) {
    if (!view_ && GetTickCount64() >= nextOpen_) Open();
    if (!view_) return false;
    if (wanted_) SetEvent(wanted_);  // "someone is looking": keeps the service sampling

    // Sequence lock: copy the block and accept it only if the sequence was even and unchanged.
    uint8_t copy[kBlockSize];
    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto before = *reinterpret_cast<const volatile uint32_t*>(view_ + 8);
        std::atomic_thread_fence(std::memory_order_acquire);
        std::memcpy(copy, view_, kBlockSize);
        std::atomic_thread_fence(std::memory_order_acquire);
        const auto after = *reinterpret_cast<const volatile uint32_t*>(view_ + 8);
        if (before == after && !(before & 1u)) {
            FILETIME now;
            GetSystemTimeAsFileTime(&now);
            const int64_t now100ns = (static_cast<int64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
            // Stale data simply means the service is idle or starting: the request above wakes it up.
            return Decode(copy, sizeof(copy), now100ns, out);
        }
    }
    return false;  // still being written after several tries: use the next sample
}

bool ServiceInstalled() {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return false;
    SC_HANDLE service = OpenServiceW(manager, kServiceName, SERVICE_QUERY_STATUS);
    const bool installed = service != nullptr;
    if (service) CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return installed;
}

}  // namespace feed
