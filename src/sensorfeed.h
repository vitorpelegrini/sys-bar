#pragma once

#include "common.h"
#include "metrics.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Reads the optional "advanced sensors" service (sensors/, LibreHardwareMonitorLib + PawnIO), which
// publishes CPU temperature, fans and SATA drive temperatures in a read-only shared-memory block.
namespace feed {

// Layout v1 - must match sensors/SharedBlock.cs. Entry byte 2 is the fan role and byte 3 the fan's duty
// (percent + 1); 0 means unknown in both, which is what older services wrote.
constexpr uint32_t kMagic = 0x31534253;  // "SBS1"
constexpr uint32_t kVersion = 1;
constexpr size_t kHeaderSize = 32;
constexpr size_t kEntrySize = 128;
constexpr size_t kMaxEntries = 64;
constexpr size_t kNameChars = 60;
constexpr size_t kBlockSize = kHeaderSize + kMaxEntries * kEntrySize;
constexpr int64_t kMaxAge100ns = 15LL * 10'000'000;  // readings older than 15 s are stale

struct Decoded {
    uint32_t serviceVersion = 0;  // major << 20 | minor << 10 | patch (0 = unknown, older service)
    std::optional<double> cpuTemperature;
    std::vector<SensorReading> readings;
};

// Decodes a consistent copy of the block. Fails on a bad magic/version, an odd (torn) sequence,
// stale data or out-of-range values.
bool Decode(const uint8_t* block, size_t size, int64_t nowFileTime, Decoded& out);

class SensorFeed {
public:
    SensorFeed() = default;
    ~SensorFeed() { Close(); }
    SensorFeed(const SensorFeed&) = delete;
    SensorFeed& operator=(const SensorFeed&) = delete;

    // Asks the service for data and returns the latest readings; false when the service is absent,
    // stopped or not publishing.
    bool Read(Decoded& out);
    // Asks the service to update itself from the official release (it receives no data from us).
    void RequestUpdate();
    void Close();

private:
    void Open();
    void OpenWanted();

    HANDLE mapping_ = nullptr;
    const uint8_t* view_ = nullptr;
    HANDLE wanted_ = nullptr;
    HANDLE update_ = nullptr;
    HANDLE onDemand_ = nullptr;
    ULONGLONG nextOpen_ = 0;
    ULONGLONG lastGood_ = 0;
};

constexpr uint32_t EncodeVersion(uint32_t major, uint32_t minor, uint32_t patch) {
    return (major << 20) | (minor << 10) | patch;
}

// Tells the service that fans are on screen (sys-bar's card is open or opening): it then refreshes them every
// second instead of every few seconds, and right away when the card opens. Never blocks.
class DetailSignal {
public:
    DetailSignal() = default;
    ~DetailSignal();
    DetailSignal(const DetailSignal&) = delete;
    DetailSignal& operator=(const DetailSignal&) = delete;

    void Raise();

private:
    HANDLE event_ = nullptr;
    ULONGLONG nextOpen_ = 0;
};

// Whether the service is registered (any user may query this).
bool ServiceInstalled();

}  // namespace feed
