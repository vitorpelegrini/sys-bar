#include "test.h"

#include "perf.h"

#include <pdh.h>
#include <pdhmsg.h>
#include <perflib.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Builds a block the way PerfQueryCounterData lays it out.
struct Block {
    std::vector<BYTE> bytes;

    template <typename T>
    size_t Put(const T& value) {
        const size_t at = bytes.size();
        bytes.resize(at + sizeof(T));
        std::memcpy(bytes.data() + at, &value, sizeof(T));
        return at;
    }
    template <typename T>
    T* At(size_t offset) {
        return reinterpret_cast<T*>(bytes.data() + offset);
    }
    size_t Header(ULONG counters) {
        PERF_DATA_HEADER header{};
        header.dwNumCounters = counters;
        header.PerfTime100NSec = 123456789;
        return Put(header);
    }
    void Finish() { At<PERF_DATA_HEADER>(0)->dwTotalSize = static_cast<ULONG>(bytes.size()); }

    void Value(uint64_t value, ULONG dataSize = 8) {
        PERF_COUNTER_DATA data{};
        data.dwDataSize = dataSize;
        data.dwSize = sizeof(PERF_COUNTER_DATA) + 8;
        Put(data);
        Put(value);
    }
    void Single(uint64_t value, ULONG dataSize = 8) {
        PERF_COUNTER_HEADER counter{};
        counter.dwType = PERF_SINGLE_COUNTER;
        const size_t at = Put(counter);
        Value(value, dataSize);
        At<PERF_COUNTER_HEADER>(at)->dwSize = static_cast<ULONG>(bytes.size() - at);
    }
    // Instance names are padded to 8 bytes, like the real thing.
    void Instance(const std::wstring& name, uint64_t value, bool terminated = true) {
        PERF_INSTANCE_HEADER instance{};
        const size_t at = Put(instance);
        for (wchar_t c : name) Put(c);
        if (terminated) Put(L'\0');
        while ((bytes.size() - at) % 8) bytes.push_back(terminated ? BYTE{0} : BYTE{0x41});
        At<PERF_INSTANCE_HEADER>(at)->Size = static_cast<ULONG>(bytes.size() - at);
        Value(value);
    }
    template <typename Fill>
    void Multi(ULONG instances, Fill&& fill) {
        PERF_COUNTER_HEADER counter{};
        counter.dwType = PERF_MULTIPLE_INSTANCES;
        const size_t at = Put(counter);
        PERF_MULTI_INSTANCES multi{};
        multi.dwInstances = instances;
        const size_t multiAt = Put(multi);
        fill();
        At<PERF_MULTI_INSTANCES>(multiAt)->dwTotalSize = static_cast<ULONG>(bytes.size() - multiAt);
        At<PERF_COUNTER_HEADER>(at)->dwSize = static_cast<ULONG>(bytes.size() - at);
    }
};

std::vector<perf::Item> Parse(const Block& block, ULONG index) {
    std::vector<perf::Item> items;
    perf::ParseCounter(block.bytes.data(), block.bytes.size(), index, items);
    return items;
}

}  // namespace

TEST(Perf_block_with_single_and_multiple_instances_is_parsed) {
    Block block;
    block.Header(3);
    block.Single(0x1122334455667788ull);
    block.Multi(3, [&] {
        block.Instance(L"pid_1_luid_0x00000000_0x0005E13D_phys_0_eng_0_engtype_3D", 1000);
        block.Instance(L"a", 2000);
        block.Instance(L"_Total", 0xFFFFFFFFFFull);
    });
    block.Single(0xAABBCCDDull, 4);  // a 32-bit counter
    block.Finish();

    const auto single = Parse(block, 0);
    CHECK_EQ(single.size(), size_t{1});
    if (!single.empty()) {
        CHECK_EQ(single[0].value, 0x1122334455667788ull);
        CHECK(single[0].wide);
    }

    const auto multi = Parse(block, 1);
    CHECK_EQ(multi.size(), size_t{3});
    if (multi.size() == 3) {
        CHECK_EQ(std::wstring(multi[0].name),
                 std::wstring(L"pid_1_luid_0x00000000_0x0005E13D_phys_0_eng_0_engtype_3D"));
        CHECK_EQ(multi[0].value, 1000ull);
        CHECK_EQ(std::wstring(multi[1].name), std::wstring(L"a"));
        CHECK_EQ(multi[1].value, 2000ull);
        CHECK_EQ(std::wstring(multi[2].name), std::wstring(L"_Total"));
        CHECK_EQ(multi[2].value, 0xFFFFFFFFFFull);
    }

    const auto narrow = Parse(block, 2);
    CHECK_EQ(narrow.size(), size_t{1});
    if (!narrow.empty()) {
        CHECK_EQ(narrow[0].value, 0xAABBCCDDull);
        CHECK(!narrow[0].wide);
    }
    CHECK(Parse(block, 3).empty());  // no such counter
}

TEST(Perf_counter_growth_handles_32_bit_wrap_and_resets) {
    perf::Item before, now;
    uint64_t grew = 0;
    before.value = 100;
    now.value = 350;
    CHECK(perf::Delta(before, now, grew));
    CHECK_EQ(grew, 250ull);
    now.value = 50;  // a 64-bit counter never wraps in practice: it was reset
    CHECK(!perf::Delta(before, now, grew));

    before.wide = now.wide = false;
    before.value = 0xFFFFFF00u;
    now.value = 0x00000010u;  // wrapped around
    CHECK(perf::Delta(before, now, grew));
    CHECK_EQ(grew, 0x110ull);
}

TEST(Perf_malformed_blocks_never_read_outside_the_block) {
    // Every prefix of a valid block, and the valid block with each size field corrupted in turn: the parser
    // must stay inside the buffer (the page-heap/ASan runs make an overrun fail loudly) and never hang.
    Block good;
    good.Header(2);
    good.Multi(2, [&] {
        good.Instance(L"first", 1);
        good.Instance(L"second", 2);
    });
    good.Single(7);
    good.Finish();

    for (size_t length = 0; length <= good.bytes.size(); ++length) {
        // An exact-size copy: reading past `length` is reading past the allocation.
        std::vector<BYTE> cut(good.bytes.begin(), good.bytes.begin() + static_cast<ptrdiff_t>(length));
        std::vector<perf::Item> items;
        perf::ParseCounter(cut.data(), cut.size(), 0, items);
        perf::ParseCounter(cut.data(), cut.size(), 1, items);
    }
    for (size_t at = 0; at + sizeof(ULONG) <= good.bytes.size(); at += sizeof(ULONG)) {
        for (ULONG poison : {0u, 1u, 7u, 0x7FFFFFFFu, 0xFFFFFFFFu}) {
            std::vector<BYTE> bad = good.bytes;
            std::memcpy(bad.data() + at, &poison, sizeof(poison));
            std::vector<perf::Item> items;
            perf::ParseCounter(bad.data(), bad.size(), 0, items);
            for (const perf::Item& item : items) {
                // Whatever came back points inside the block and is terminated there.
                const auto* name = reinterpret_cast<const BYTE*>(item.name);
                const bool inside = name >= bad.data() && name < bad.data() + bad.size();
                CHECK(inside || item.name[0] == L'\0');
                if (inside) CHECK(wcsnlen(item.name, (bad.data() + bad.size() - name) / sizeof(wchar_t)) < 4096);
            }
            perf::ParseCounter(bad.data(), bad.size(), 1, items);
        }
    }

    // A name that is not terminated inside its instance is dropped, the rest is kept.
    Block unterminated;
    unterminated.Header(1);
    unterminated.Multi(2, [&] {
        unterminated.Instance(L"AAAAAAAA", 1, false);
        unterminated.Instance(L"ok", 2);
    });
    unterminated.Finish();
    const auto items = Parse(unterminated, 0);
    CHECK_EQ(items.size(), size_t{1});
    if (!items.empty()) CHECK_EQ(std::wstring(items[0].name), std::wstring(L"ok"));
}

TEST(Perf_cpu_utility_matches_PDH) {
    // The value sys-bar shows is the growth of "% Processor Utility" over the growth of its base counter:
    // checked against what PDH computes for the same counter over (nearly) the same second.
    perf::CounterRef utility;
    CHECK(perf::Find(L"Processor Information", L"% Processor Utility", utility));
    CHECK(utility.baseId != perf::kNoCounter);
    perf::CounterRef missing;
    CHECK(!perf::Find(L"Processor Information", L"No such counter", missing));
    CHECK(!perf::Find(L"No such counter set", L"% Processor Utility", missing));
    if (utility.id == perf::kNoCounter || utility.baseId == perf::kNoCounter) return;

    perf::Query query;
    const int value = query.Add(utility.set, utility.id, L"_Total");
    const int base = query.Add(utility.set, utility.baseId, L"_Total");
    CHECK(value >= 0 && base >= 0);
    CHECK(query.Items(99).empty());  // an unknown handle is simply empty

    PDH_HQUERY pdh = nullptr;
    PDH_HCOUNTER counter = nullptr;
    const bool reference = PdhOpenQueryW(nullptr, 0, &pdh) == ERROR_SUCCESS &&
                           PdhAddEnglishCounterW(pdh, L"\\Processor Information(_Total)\\% Processor Utility", 0,
                                                 &counter) == ERROR_SUCCESS;

    // Both read back to back at each end of the window, so they cover the same time within a millisecond.
    CHECK(query.Collect());
    if (reference) PdhCollectQueryData(pdh);
    CHECK_EQ(query.Items(value).size(), size_t{1});
    CHECK_EQ(query.Items(base).size(), size_t{1});
    if (query.Items(value).size() != 1 || query.Items(base).size() != 1) return;
    perf::Item value0 = query.Items(value)[0], base0 = query.Items(base)[0];
    const int64_t time0 = query.Time100ns();

    int compared = 0;
    for (int round = 0; round < 3; ++round) {
        Sleep(700);
        CHECK(query.Collect());
        if (reference) PdhCollectQueryData(pdh);
        if (query.Items(value).size() != 1 || query.Items(base).size() != 1) return;
        const perf::Item value1 = query.Items(value)[0], base1 = query.Items(base)[0];
        uint64_t grew = 0, baseGrew = 0;
        CHECK(perf::Delta(value0, value1, grew));
        CHECK(perf::Delta(base0, base1, baseGrew));
        CHECK(baseGrew > 0);
        CHECK(query.Time100ns() > time0);
        if (baseGrew == 0) return;
        const double ours = static_cast<double>(grew) / static_cast<double>(baseGrew);
        CHECK(ours >= 0.0 && ours <= 400.0);  // percent; above 100 with turbo

        PDH_FMT_COUNTERVALUE formatted{};
        if (reference &&
            PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &formatted) ==
                ERROR_SUCCESS &&
            (formatted.CStatus == PDH_CSTATUS_VALID_DATA || formatted.CStatus == PDH_CSTATUS_NEW_DATA)) {
            std::printf("      cpu utility: ours %.3f%%, PDH %.3f%%\n", ours, formatted.doubleValue);
            // The two reads are a fraction of a millisecond apart: on a busy machine that is a few hundredths
            // of a point; the margin is wide so a context switch between them never fails the test.
            CHECK(std::fabs(ours - formatted.doubleValue) < 2.0 + 0.05 * formatted.doubleValue);
            ++compared;
        }
        value0 = value1;
        base0 = base1;
    }
    if (reference) CHECK(compared > 0);
    if (pdh) PdhCloseQuery(pdh);
}

TEST(Perf_gpu_engines_are_read_when_the_system_has_them) {
    perf::CounterRef engine;
    if (!perf::Find(L"GPU Engine", L"Utilization Percentage", engine)) {
        std::printf("      no GPU Engine counter set here (virtual machine?)\n");
        return;
    }
    perf::Query query;
    const int handle = query.Add(engine.set, engine.id, L"*");
    CHECK(handle >= 0);
    CHECK(query.Collect());
    const size_t first = query.Items(handle).size();
    // The running time of an engine only grows: read twice and compare by name.
    std::vector<std::pair<std::wstring, perf::Item>> before;
    for (const perf::Item& item : query.Items(handle)) before.emplace_back(item.name, item);
    Sleep(200);
    CHECK(query.Collect());
    size_t matched = 0, backwards = 0;
    for (const perf::Item& item : query.Items(handle)) {
        for (const auto& [name, old] : before) {
            if (name != item.name) continue;
            ++matched;
            uint64_t grew = 0;
            if (!perf::Delta(old, item, grew)) ++backwards;
            break;
        }
    }
    std::printf("      %zu GPU engine instances, %zu matched across two reads\n", first, matched);
    CHECK_EQ(backwards, size_t{0});
    if (first > 0) CHECK(matched > 0);
}
