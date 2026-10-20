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

// Layout v1 - must match sensors/SharedBlock.cs.
constexpr uint32_t kMagic = 0x31534253;  // "SBS1"
constexpr uint32_t kVersion = 1;
constexpr size_t kHeaderSize = 32;
constexpr size_t kEntrySize = 128;
constexpr size_t kMaxEntries = 64;
constexpr size_t kNameChars = 60;
constexpr size_t kBlockSize = kHeaderSize + kMaxEntries * kEntrySize;
constexpr int64_t kMaxAge100ns = 15LL * 10'000'000;  // readings older than 15 s are stale

struct Decoded {
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
    void Close();

private:
    void Open();

    HANDLE mapping_ = nullptr;
    const uint8_t* view_ = nullptr;
    HANDLE wanted_ = nullptr;
    ULONGLONG nextOpen_ = 0;
};

// Whether the service is registered (any user may query this).
bool ServiceInstalled();

}  // namespace feed
