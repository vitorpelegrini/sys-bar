#include "perf.h"

#include <perflib.h>

#include <algorithm>
#include <cstring>
#include <cwchar>

namespace perf {
namespace {

// Counter sets sys-bar reads, by their GUID in every Windows 10 and 11 so far: tried first, so the lookup
// does not have to ask every registered counter set for its name (tens of milliseconds). Each is still
// checked by name, and an unknown system falls back to the full search.
struct KnownSet {
    const wchar_t* name;
    GUID id;
};
constexpr KnownSet kKnownSets[] = {
    {L"Processor Information", {0xB4FC721A, 0x0378, 0x476F, {0x89, 0xBA, 0xA5, 0xA7, 0x9F, 0x81, 0x0B, 0x36}}},
    {L"GPU Engine", {0x978C167D, 0x4764, 0x4D9C, {0x98, 0x24, 0x14, 0x74, 0x73, 0x51, 0xDC, 0x81}}},
    {L"GPU Adapter Memory", {0xBE2139C7, 0xAB81, 0x424D, {0xB1, 0x07, 0xD8, 0x7F, 0x7C, 0x93, 0x22, 0xAC}}},
};

// Registration data of a counter set; empty when it is not there.
std::vector<BYTE> RegistrationInfo(const GUID& set, PerfRegInfoType type) {
    std::vector<BYTE> buffer(4096);
    for (int attempt = 0; attempt < 3; ++attempt) {
        DWORD needed = 0;
        const ULONG status = PerfQueryCounterSetRegistrationInfo(nullptr, &set, type, 0, buffer.data(),
                                                                 static_cast<DWORD>(buffer.size()), &needed);
        if (status == ERROR_SUCCESS) {
            buffer.resize(std::min<size_t>(buffer.size(), needed));
            return buffer;
        }
        if (status != ERROR_NOT_ENOUGH_MEMORY || needed <= buffer.size()) break;
        buffer.resize(needed);
    }
    return {};
}

// The text at `offset` of a registration buffer, when it is a terminated string inside it.
const wchar_t* TextAt(const std::vector<BYTE>& buffer, size_t offset) {
    if (offset >= buffer.size() || offset % alignof(wchar_t) != 0) return nullptr;
    const auto* text = reinterpret_cast<const wchar_t*>(buffer.data() + offset);
    const size_t max = (buffer.size() - offset) / sizeof(wchar_t);
    return wcsnlen(text, max) < max ? text : nullptr;
}

bool HasName(const GUID& set, const wchar_t* name) {
    const std::vector<BYTE> info = RegistrationInfo(set, PERF_REG_COUNTERSET_ENGLISH_NAME);
    const wchar_t* text = TextAt(info, 0);
    return text && _wcsicmp(text, name) == 0;
}

bool FindSet(const wchar_t* name, GUID& set, bool searchAll) {
    for (const KnownSet& known : kKnownSets) {
        if (_wcsicmp(known.name, name) == 0 && HasName(known.id, name)) {
            set = known.id;
            return true;
        }
    }
    if (!searchAll) return false;
    DWORD count = 0;
    PerfEnumerateCounterSet(nullptr, nullptr, 0, &count);
    if (count == 0 || count > 100'000) return false;
    std::vector<GUID> ids(count);
    if (PerfEnumerateCounterSet(nullptr, ids.data(), count, &count) != ERROR_SUCCESS) return false;
    ids.resize(std::min<size_t>(ids.size(), count));
    for (const GUID& id : ids) {
        if (HasName(id, name)) {
            set = id;
            return true;
        }
    }
    return false;
}

template <typename T>
const T* At(const BYTE* block, size_t size, size_t offset) {
    return offset <= size && size - offset >= sizeof(T) ? reinterpret_cast<const T*>(block + offset) : nullptr;
}

// The value after a PERF_COUNTER_DATA header at `offset` (4 or 8 bytes, zero-extended).
bool ReadValue(const BYTE* block, size_t size, size_t offset, Item& item, size_t& next) {
    const auto* data = At<PERF_COUNTER_DATA>(block, size, offset);
    if (!data || data->dwSize < sizeof(PERF_COUNTER_DATA) || data->dwSize > size - offset ||
        data->dwDataSize > data->dwSize - sizeof(PERF_COUNTER_DATA)) {
        return false;
    }
    item.value = 0;
    item.wide = data->dwDataSize >= sizeof(uint64_t);
    std::memcpy(&item.value, block + offset + sizeof(PERF_COUNTER_DATA),
                std::min<size_t>(data->dwDataSize, sizeof(item.value)));
    next = offset + data->dwSize;
    return true;
}

}  // namespace

bool Find(const wchar_t* counterSet, const wchar_t* counter, CounterRef& out, bool searchAll) {
    out = {};
    if (!FindSet(counterSet, out.set, searchAll)) return false;

    // Counter ids by English name.
    const std::vector<BYTE> names = RegistrationInfo(out.set, PERF_REG_COUNTER_ENGLISH_NAMES);
    const auto* header = At<PERF_STRING_BUFFER_HEADER>(names.data(), names.size(), 0);
    if (!header) return false;
    for (DWORD i = 0; i < header->dwCounters; ++i) {
        const auto* entry = At<PERF_STRING_COUNTER_HEADER>(
            names.data(), names.size(), sizeof(PERF_STRING_BUFFER_HEADER) + i * sizeof(PERF_STRING_COUNTER_HEADER));
        if (!entry) break;
        const wchar_t* text = TextAt(names, entry->dwOffset);
        if (text && _wcsicmp(text, counter) == 0) {
            out.id = entry->dwCounterId;
            break;
        }
    }
    if (out.id == kNoCounter) return false;

    // Its base counter (the denominator of fractions and averages), which has no name of its own.
    const std::vector<BYTE> info = RegistrationInfo(out.set, PERF_REG_COUNTERSET_STRUCT);
    if (const auto* set = At<PERF_COUNTERSET_REG_INFO>(info.data(), info.size(), 0)) {
        for (ULONG i = 0; i < set->NumCounters; ++i) {
            const auto* c = At<PERF_COUNTER_REG_INFO>(
                info.data(), info.size(), sizeof(PERF_COUNTERSET_REG_INFO) + i * sizeof(PERF_COUNTER_REG_INFO));
            if (!c) break;
            if (c->CounterId == out.id) out.baseId = c->BaseCounterId;
        }
    }
    return true;
}

void ParseCounter(const BYTE* block, size_t size, ULONG index, std::vector<Item>& items) {
    items.clear();
    const auto* header = At<PERF_DATA_HEADER>(block, size, 0);
    if (!header || index >= header->dwNumCounters) return;
    size = std::min<size_t>(size, header->dwTotalSize);

    // Walk to the counter's data: each one says how long it is.
    size_t offset = sizeof(PERF_DATA_HEADER);
    const PERF_COUNTER_HEADER* counter = nullptr;
    for (ULONG i = 0;; ++i) {
        counter = At<PERF_COUNTER_HEADER>(block, size, offset);
        if (!counter || counter->dwSize < sizeof(PERF_COUNTER_HEADER) || counter->dwSize > size - offset) return;
        if (i == index) break;
        offset += counter->dwSize;
    }
    const size_t end = offset + counter->dwSize;
    offset += sizeof(PERF_COUNTER_HEADER);
    if (counter->dwStatus != ERROR_SUCCESS) return;

    if (counter->dwType == PERF_SINGLE_COUNTER) {
        Item item;
        size_t next = 0;
        if (ReadValue(block, end, offset, item, next)) items.push_back(item);
        return;
    }
    if (counter->dwType != PERF_MULTIPLE_INSTANCES) return;
    const auto* instances = At<PERF_MULTI_INSTANCES>(block, end, offset);
    if (!instances) return;
    offset += sizeof(PERF_MULTI_INSTANCES);
    items.reserve(std::min<size_t>(instances->dwInstances, 16384));
    for (ULONG i = 0; i < instances->dwInstances; ++i) {
        const auto* instance = At<PERF_INSTANCE_HEADER>(block, end, offset);
        if (!instance || instance->Size < sizeof(PERF_INSTANCE_HEADER) || instance->Size > end - offset) return;
        // The name follows the header, inside its size, and must be terminated there.
        Item item;
        const size_t nameBytes = instance->Size - sizeof(PERF_INSTANCE_HEADER);
        const auto* name = reinterpret_cast<const wchar_t*>(block + offset + sizeof(PERF_INSTANCE_HEADER));
        const size_t maxChars = nameBytes / sizeof(wchar_t);
        const bool terminated = maxChars > 0 && wcsnlen(name, maxChars) < maxChars;
        size_t next = 0;
        if (!ReadValue(block, end, offset + instance->Size, item, next)) return;
        if (terminated) {
            item.name = name;
            items.push_back(item);
        }
        offset = next;
    }
}

int Query::Add(const GUID& set, ULONG counterId, const wchar_t* instance) {
    if (!query_ && PerfOpenQueryHandle(nullptr, &query_) != ERROR_SUCCESS) {
        query_ = nullptr;
        return -1;
    }
    // The identifier is followed by the instance name, padded to a multiple of 8 bytes.
    const size_t nameBytes = (wcslen(instance) + 1) * sizeof(wchar_t);
    std::vector<BYTE> buffer((sizeof(PERF_COUNTER_IDENTIFIER) + nameBytes + 7) & ~static_cast<size_t>(7));
    auto* id = reinterpret_cast<PERF_COUNTER_IDENTIFIER*>(buffer.data());
    id->CounterSetGuid = set;
    id->Size = static_cast<ULONG>(buffer.size());
    id->CounterId = counterId;
    id->InstanceId = 0xFFFFFFFF;  // any instance id: instances are chosen by name
    std::memcpy(buffer.data() + sizeof(PERF_COUNTER_IDENTIFIER), instance, nameBytes);
    if (PerfAddCounters(query_, id, id->Size) != ERROR_SUCCESS || id->Status != ERROR_SUCCESS) return -1;

    Counter counter;
    counter.set = set;
    counter.id = counterId;
    counter.instance = instance;
    counter.index = static_cast<ULONG>(counters_.size());  // in the order added, unless Windows says otherwise
    counters_.push_back(std::move(counter));
    MapIndexes();
    return static_cast<int>(counters_.size()) - 1;
}

// Where each counter's data sits in the block, as Windows reports it.
void Query::MapIndexes() {
    std::vector<BYTE> buffer(1024);
    DWORD needed = 0;
    ULONG status = PerfQueryCounterInfo(query_, reinterpret_cast<PERF_COUNTER_IDENTIFIER*>(buffer.data()),
                                        static_cast<DWORD>(buffer.size()), &needed);
    if (status == ERROR_NOT_ENOUGH_MEMORY && needed > buffer.size()) {
        buffer.resize(needed);
        status = PerfQueryCounterInfo(query_, reinterpret_cast<PERF_COUNTER_IDENTIFIER*>(buffer.data()),
                                      static_cast<DWORD>(buffer.size()), &needed);
    }
    if (status != ERROR_SUCCESS) return;
    const size_t size = std::min<size_t>(buffer.size(), needed);
    for (size_t offset = 0;;) {
        const auto* id = At<PERF_COUNTER_IDENTIFIER>(buffer.data(), size, offset);
        if (!id || id->Size < sizeof(PERF_COUNTER_IDENTIFIER) || id->Size > size - offset) break;
        const auto* name = reinterpret_cast<const wchar_t*>(buffer.data() + offset + sizeof(PERF_COUNTER_IDENTIFIER));
        const size_t maxChars = (id->Size - sizeof(PERF_COUNTER_IDENTIFIER)) / sizeof(wchar_t);
        if (maxChars > 0 && wcsnlen(name, maxChars) < maxChars) {
            for (Counter& counter : counters_) {
                if (IsEqualGUID(counter.set, id->CounterSetGuid) && counter.id == id->CounterId &&
                    _wcsicmp(counter.instance.c_str(), name) == 0) {
                    counter.index = id->Index;
                }
            }
        }
        offset += id->Size;
    }
}

bool Query::Collect() {
    for (Counter& counter : counters_) counter.items.clear();
    if (!query_ || counters_.empty()) return false;
    if (block_.size() < 4096) block_.resize(4096);
    for (int attempt = 0; attempt < 3; ++attempt) {
        DWORD needed = 0;
        const ULONG status = PerfQueryCounterData(query_, reinterpret_cast<PERF_DATA_HEADER*>(block_.data()),
                                                  static_cast<DWORD>(block_.size()), &needed);
        if (status == ERROR_SUCCESS) {
            const size_t size = std::min<size_t>(block_.size(), needed);
            const auto* header = At<PERF_DATA_HEADER>(block_.data(), size, 0);
            if (!header) return false;
            time100ns_ = header->PerfTime100NSec;
            for (Counter& counter : counters_) ParseCounter(block_.data(), size, counter.index, counter.items);
            return true;
        }
        if (status != ERROR_NOT_ENOUGH_MEMORY || needed <= block_.size()) return false;
        block_.resize(needed + needed / 4 + 4096);  // headroom: new processes add instances
    }
    return false;
}

const std::vector<Item>& Query::Items(int handle) const {
    static const std::vector<Item> none;
    return handle >= 0 && static_cast<size_t>(handle) < counters_.size() ? counters_[static_cast<size_t>(handle)].items
                                                                         : none;
}

void Query::Close() {
    if (query_) PerfCloseQueryHandle(query_);
    query_ = nullptr;
    counters_.clear();
    block_.clear();
    block_.shrink_to_fit();
    time100ns_ = 0;
}

}  // namespace perf
