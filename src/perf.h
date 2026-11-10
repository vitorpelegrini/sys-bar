#pragma once

#include "common.h"

#include <cstdint>
#include <string>
#include <vector>

// Windows performance counters read through the PerfLib V2 consumer API (advapi32), not PDH: PDH loads its
// counter-name tables for the first counter it is given (some 6 MB of private memory, more than the rest of
// sys-bar together) and builds a formatted array of every instance on each read. Only manifest-based ("V2")
// counter sets are reachable this way, which is all sys-bar needs: Processor Information, GPU Engine and
// GPU Adapter Memory.
namespace perf {

constexpr ULONG kNoCounter = 0xFFFFFFFF;

struct CounterRef {
    GUID set{};
    ULONG id = kNoCounter;
    ULONG baseId = kNoCounter;  // the counter it is divided by (fractions and averages), if any
};

// Looks a counter up by the English names of its counter set and of itself ("GPU Engine", "Utilization
// Percentage"). False when this system does not have it (yet: GPU sets appear once the driver is up).
// `searchAll` = false only tries the counter set's well-known id: for retries, which must stay cheap.
bool Find(const wchar_t* counterSet, const wchar_t* counter, CounterRef& out, bool searchAll = true);

// One instance of a counter as returned by Query::Collect().
struct Item {
    const wchar_t* name = L"";  // instance name; valid until the next Collect() or Close()
    uint64_t value = 0;         // raw value
    bool wide = true;           // a 64-bit counter (a 32-bit one wraps around at 2^32)
};

// How much a cumulative counter grew between two readings, across the wrap-around of a 32-bit one. Nothing
// when it went backwards (the counter was reset).
inline bool Delta(const Item& before, const Item& now, uint64_t& delta) {
    if (!now.wide) {
        delta = static_cast<uint32_t>(static_cast<uint32_t>(now.value) - static_cast<uint32_t>(before.value));
        return true;
    }
    if (now.value < before.value) return false;
    delta = now.value - before.value;
    return true;
}

// A set of counters read together, with a single system call per Collect().
class Query {
public:
    Query() = default;
    ~Query() { Close(); }
    Query(const Query&) = delete;
    Query& operator=(const Query&) = delete;

    // Adds counter `counterId` of `set` for `instance` ("*": every instance). Returns its handle for Items(),
    // or -1 when it could not be added.
    int Add(const GUID& set, ULONG counterId, const wchar_t* instance);
    // Reads every counter. False when the read failed: no counter has items then.
    bool Collect();
    // The instances the last Collect() returned for a counter added with Add().
    const std::vector<Item>& Items(int handle) const;
    // System time of the last Collect(), in 100 ns units: the time base of "100 ns timer" counters.
    int64_t Time100ns() const { return time100ns_; }
    void Close();

private:
    struct Counter {
        GUID set{};
        ULONG id = 0;
        std::wstring instance;
        ULONG index = 0;  // position of its data in the block
        std::vector<Item> items;
    };
    void MapIndexes();

    HANDLE query_ = nullptr;
    std::vector<Counter> counters_;
    std::vector<BYTE> block_;
    int64_t time100ns_ = 0;
};

// Fills `items` from one counter's data inside a block returned by PerfQueryCounterData; nothing outside
// `block` is ever read, whatever the block says. Exposed for the tests.
void ParseCounter(const BYTE* block, size_t size, ULONG index, std::vector<Item>& items);

}  // namespace perf
