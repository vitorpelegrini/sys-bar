#include "sensorfeed.h"

#include <atomic>
#include <cmath>
#include <cstring>

namespace feed {
namespace {

constexpr wchar_t kMappingName[] = L"Global\\SysBarSensors";
constexpr wchar_t kWantedEventName[] = L"Global\\SysBarSensorsWanted";
constexpr wchar_t kUpdateEventName[] = L"Global\\SysBarSensorsUpdate";
constexpr wchar_t kDetailEventName[] = L"Global\\SysBarSensorsDetail";
constexpr wchar_t kOnDemandEventName[] = L"Global\\SysBarSensorsOnDemand";
constexpr wchar_t kServiceName[] = L"SysBarSensors";
constexpr ULONGLONG kReopenMs = 10'000;       // the service is looked for again every 10 s
constexpr ULONGLONG kStaleReopenMs = 30'000;  // no valid data for this long: drop the mapping, open it anew

// Trust: a "Global\\" file mapping can only be created by services and administrators
// (SeCreateGlobalPrivilege), so a standard user cannot plant a fake block; every field is validated
// anyway. The events carry no data: one created by someone else only makes our signal go nowhere.

// Characters a name from another process may not carry into our UI: C0/C1 controls, DEL, bidi overrides.
bool Displayable(wchar_t c) {
    return c >= 0x20 && !(c >= 0x7F && c <= 0x9F) && !(c >= 0x202A && c <= 0x202E) && !(c >= 0x2066 && c <= 0x2069);
}

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

    out.serviceVersion = Get<uint32_t>(block, 28);
    const float cpu = Get<float>(block, 24);
    if (std::isfinite(cpu) && cpu > 0.0f && cpu < 150.0f) out.cpuTemperature = cpu;

    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = block + kHeaderSize + static_cast<size_t>(i) * kEntrySize;
        const uint8_t kind = e[0];
        const uint8_t group = e[1];
        const uint8_t role = e[2];
        const uint8_t duty = e[3];  // percent + 1; 0 = unknown (and what older services wrote)
        const float value = Get<float>(e, 4);
        if (kind > 1 || group > 3 || !std::isfinite(value)) continue;
        const bool fan = kind == 1;
        if (fan ? (value < 0.0f || value >= 20000.0f) : (value <= 0.0f || value >= 150.0f)) continue;

        std::wstring name;
        for (size_t c = 0; c < kNameChars; ++c) {
            const auto ch = Get<wchar_t>(e, 8 + c * sizeof(wchar_t));
            if (ch == L'\0') break;
            name += Displayable(ch) ? ch : L' ';
        }
        if (name.empty()) continue;
        SensorReading reading;
        reading.kind = fan ? SensorKind::Fan : SensorKind::Temperature;
        reading.group = static_cast<SensorGroup>(group);
        reading.name = std::move(name);
        reading.value = value;
        reading.role =
            fan && role <= static_cast<uint8_t>(FanRole::Psu) ? static_cast<FanRole>(role) : FanRole::Unknown;
        if (fan && duty >= 1 && duty <= 101) reading.duty = duty - 1.0;
        out.readings.push_back(std::move(reading));
    }
    return true;
}

void SensorFeed::Open() {
    nextOpen_ = GetTickCount64() + kReopenMs;
    mapping_ = OpenFileMappingW(FILE_MAP_READ, FALSE, kMappingName);
    if (!mapping_) return;
    lastGood_ = GetTickCount64();
    view_ = static_cast<const uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, kBlockSize));
    if (!view_) {
        Close();
        return;
    }
    OpenWanted();
}

void SensorFeed::OpenWanted() {
    // The service may create its event a moment after the mapping: looked for again until found.
    wanted_ = OpenEventW(EVENT_MODIFY_STATE, FALSE, kWantedEventName);
}

void SensorFeed::Close() {
    if (view_) UnmapViewOfFile(view_);
    if (mapping_) CloseHandle(mapping_);
    if (wanted_) CloseHandle(wanted_);
    if (update_) CloseHandle(update_);
    if (onDemand_) CloseHandle(onDemand_);
    view_ = nullptr;
    mapping_ = nullptr;
    wanted_ = nullptr;
    update_ = nullptr;
    onDemand_ = nullptr;
}

DetailSignal::~DetailSignal() {
    if (event_) CloseHandle(event_);
}

void DetailSignal::Raise() {
    // An older service has no such event: looked for again now and then, in case the service was updated.
    if (!event_ && GetTickCount64() >= nextOpen_) {
        event_ = OpenEventW(EVENT_MODIFY_STATE, FALSE, kDetailEventName);
        if (!event_) nextOpen_ = GetTickCount64() + kReopenMs;
    }
    if (event_) SetEvent(event_);
}

void SensorFeed::RequestUpdate() {
    if (!update_) update_ = OpenEventW(EVENT_MODIFY_STATE, FALSE, kUpdateEventName);
    if (update_) SetEvent(update_);
}

bool SensorFeed::Read(Decoded& out) {
    if (!view_ && GetTickCount64() >= nextOpen_) Open();
    if (!view_) return false;
    if (!wanted_) OpenWanted();
    if (wanted_) SetEvent(wanted_);  // "someone is looking": keeps the service sampling
    // "Fans only when asked": we signal the detail event while they are on screen (see DetailSignal).
    if (!onDemand_) onDemand_ = OpenEventW(EVENT_MODIFY_STATE, FALSE, kOnDemandEventName);
    if (onDemand_) SetEvent(onDemand_);

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
            if (Decode(copy, sizeof(copy), now100ns, out)) {
                lastGood_ = GetTickCount64();
                return true;
            }
            // A different layout (the service updated itself) or a service gone for good: let go of this
            // mapping so the service can create its new one, and look again later.
            if (Get<uint32_t>(copy, 0) != kMagic || Get<uint32_t>(copy, 4) != kVersion ||
                GetTickCount64() - lastGood_ > kStaleReopenMs) {
                Close();
                nextOpen_ = GetTickCount64() + kReopenMs;
            }
            return false;
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
