#include "test.h"

#include "metrics.h"

#include <pdhmsg.h>

#include <deque>
#include <limits>

namespace {

const std::wstring kTag = L"luid_0x00000000_0x0005e13d";

// Builds an array in the format PDH returns.
struct Items {
    std::deque<std::wstring> names;  // deque: stable addresses
    std::vector<PDH_FMT_COUNTERVALUE_ITEM_W> items;

    void Engine(int pid, const std::wstring& luid, int eng, const wchar_t* type, double value,
                DWORD status = PDH_CSTATUS_VALID_DATA) {
        Raw(L"pid_" + std::to_wstring(pid) + L"_" + luid + L"_phys_0_eng_" + std::to_wstring(eng) + L"_engtype_" + type,
            value, status);
    }
    void Raw(const std::wstring& name, double value, DWORD status = PDH_CSTATUS_VALID_DATA) {
        names.push_back(name);
        PDH_FMT_COUNTERVALUE_ITEM_W item{};
        item.szName = names.back().data();
        item.FmtValue.CStatus = status;
        item.FmtValue.doubleValue = value;
        items.push_back(item);
    }
    void Memory(const std::wstring& name, LONGLONG bytes, DWORD status = PDH_CSTATUS_VALID_DATA) {
        names.push_back(name);
        PDH_FMT_COUNTERVALUE_ITEM_W item{};
        item.szName = names.back().data();
        item.FmtValue.CStatus = status;
        item.FmtValue.largeValue = bytes;
        items.push_back(item);
    }
    GpuEngineResult Aggregate(const std::wstring& tag = kTag) {
        std::unordered_map<std::wstring_view, double> scratch;
        return AggregateGpuEngines(items.data(), static_cast<DWORD>(items.size()), tag, scratch);
    }
};

}  // namespace

TEST(Gpu_sums_processes_per_engine_and_reports_the_busiest) {
    Items in;
    in.Engine(100, kTag, 0, L"3D", 30.0);
    in.Engine(200, kTag, 0, L"3D", 25.0);  // same engine: 30 + 25 = 55
    in.Engine(100, kTag, 1, L"Copy", 40.0);
    in.Engine(300, kTag, 4, L"VideoDecode", 12.0);
    const auto r = in.Aggregate();
    CHECK(r.sawAdapter);
    CHECK_NEAR(r.busiest, 55.0, 1e-9);
}

TEST(Gpu_ignores_other_adapters) {
    Items in;
    in.Engine(100, L"luid_0x00000000_0x00064563", 0, L"3D", 90.0);
    in.Engine(100, kTag, 0, L"3D", 10.0);
    CHECK_NEAR(in.Aggregate().busiest, 10.0, 1e-9);
}

TEST(Gpu_matches_the_luid_case_insensitively) {
    Items in;
    in.Engine(100, L"LUID_0x00000000_0x0005E13D", 0, L"3D", 33.0);
    const auto r = in.Aggregate();
    CHECK(r.sawAdapter);
    CHECK_NEAR(r.busiest, 33.0, 1e-9);
}

TEST(Gpu_is_capped_at_100) {
    Items in;
    in.Engine(1, kTag, 0, L"3D", 70.0);
    in.Engine(2, kTag, 0, L"3D", 60.0);
    CHECK_NEAR(in.Aggregate().busiest, 100.0, 1e-9);
}

TEST(Gpu_discards_invalid_data) {
    Items in;
    in.Engine(1, kTag, 0, L"3D", 80.0, PDH_CSTATUS_INVALID_DATA);
    in.Engine(2, kTag, 0, L"3D", std::numeric_limits<double>::quiet_NaN());
    in.Engine(3, kTag, 0, L"3D", -5.0);
    in.Engine(4, kTag, 0, L"3D", 7.0);
    in.Raw(L"pid_5_" + kTag + L"_unexpected_format", 99.0);
    const auto r = in.Aggregate();
    CHECK(r.sawAdapter);
    CHECK_NEAR(r.busiest, 7.0, 1e-9);
}

TEST(Gpu_without_instances_of_the_adapter) {
    Items in;
    in.Engine(1, L"luid_0x00000000_0x00000001", 0, L"3D", 50.0);
    const auto r = in.Aggregate();
    CHECK(!r.sawAdapter);
    CHECK_EQ(r.busiest, 0.0);
    CHECK(!Items{}.Aggregate().sawAdapter);
}

TEST(Gpu_empty_tag_matches_nothing) {
    Items in;
    in.Engine(1, kTag, 0, L"3D", 50.0);
    const auto r = in.Aggregate(L"");
    CHECK(!r.sawAdapter);
    CHECK_EQ(r.busiest, 0.0);
}

TEST(Vram_sums_only_the_chosen_adapter) {
    Items in;
    in.Memory(kTag + L"_phys_0", 2'000'000'000);
    in.Memory(kTag + L"_phys_1", 500'000'000);
    in.Memory(L"luid_0x00000000_0x00064563_phys_0", 7'000'000'000);
    in.Memory(kTag + L"_phys_2", 999, PDH_CSTATUS_INVALID_DATA);
    bool saw = false;
    const uint64_t used = SumAdapterMemory(in.items.data(), static_cast<DWORD>(in.items.size()), kTag, saw);
    CHECK(saw);
    CHECK_EQ(used, 2'500'000'000ull);
}
