#include "test.h"

#include "sensorfeed.h"

#include <cstring>
#include <limits>

namespace {

constexpr int64_t kNow = 133'000'000'000'000'000LL;  // some FILETIME

// Builds a block exactly like sensors/SharedBlock.cs does.
struct Block {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(feed::kBlockSize, 0);

    Block() {
        Put<uint32_t>(0, feed::kMagic);
        Put<uint32_t>(4, feed::kVersion);
        Put<uint32_t>(8, 2);  // even: consistent
        Put<int64_t>(16, kNow);
        Put<float>(24, std::numeric_limits<float>::quiet_NaN());
    }
    template <typename T>
    void Put(size_t at, T v) {
        std::memcpy(bytes.data() + at, &v, sizeof(T));
    }
    void Add(uint8_t kind, uint8_t group, float value, const std::wstring& name, uint8_t role = 0) {
        uint32_t count;
        std::memcpy(&count, bytes.data() + 12, 4);
        const size_t at = feed::kHeaderSize + count * feed::kEntrySize;
        bytes[at] = kind;
        bytes[at + 1] = group;
        bytes[at + 2] = role;
        Put<float>(at + 4, value);
        for (size_t c = 0; c < name.size() && c < feed::kNameChars; ++c) Put<wchar_t>(at + 8 + c * 2, name[c]);
        Put<uint32_t>(12, count + 1);
    }
    bool Decode(feed::Decoded& out, int64_t now = kNow) const {
        return feed::Decode(bytes.data(), bytes.size(), now, out);
    }
};

}  // namespace

TEST(Feed_decodes_cpu_fans_and_drives) {
    Block b;
    b.Put<float>(24, 47.5f);
    b.Add(0, 0, 47.5f, L"CPU");
    b.Add(1, 3, 1200.0f, L"Fan #1");
    b.Add(0, 2, 38.0f, L"Samsung SSD 870 EVO 1TB");
    feed::Decoded d;
    CHECK(b.Decode(d));
    CHECK(d.cpuTemperature.has_value());
    if (d.cpuTemperature) CHECK_NEAR(*d.cpuTemperature, 47.5, 1e-6);
    CHECK_EQ(d.readings.size(), size_t{3});
    if (d.readings.size() == 3) {
        CHECK(d.readings[1].kind == SensorKind::Fan);
        CHECK(d.readings[1].group == SensorGroup::Board);
        CHECK_EQ(d.readings[1].name, std::wstring(L"Fan #1"));
        CHECK_EQ(d.readings[2].name, std::wstring(L"Samsung SSD 870 EVO 1TB"));
    }
}

TEST(Feed_rejects_bad_headers_torn_writes_and_stale_data) {
    feed::Decoded d;
    {
        Block b;
        b.Put<uint32_t>(0, 0xDEADBEEF);
        CHECK(!b.Decode(d));
    }
    {
        Block b;
        b.Put<uint32_t>(4, 2);  // future layout
        CHECK(!b.Decode(d));
    }
    {
        Block b;
        b.Put<uint32_t>(8, 3);  // odd sequence: write in progress
        CHECK(!b.Decode(d));
    }
    {
        Block b;
        CHECK(!b.Decode(d, kNow + 16LL * 10'000'000));  // 16 s old
        CHECK(b.Decode(d, kNow + 5LL * 10'000'000));    // 5 s old is fine
    }
    {
        Block b;
        b.Put<uint32_t>(12, 65);  // more entries than the block holds
        CHECK(!b.Decode(d));
    }
    CHECK(!feed::Decode(nullptr, 0, kNow, d));
    Block truncated;  // (not "small": that is a macro in the Windows headers)
    CHECK(!feed::Decode(truncated.bytes.data(), feed::kBlockSize - 1, kNow, d));
}

TEST(Feed_drops_implausible_values) {
    Block b;
    b.Put<float>(24, 400.0f);  // CPU temperature out of range
    b.Add(0, 2, -5.0f, L"cold");
    b.Add(0, 2, std::numeric_limits<float>::infinity(), L"inf");
    b.Add(1, 3, 50000.0f, L"jet engine");
    b.Add(9, 3, 100.0f, L"unknown kind");
    b.Add(0, 7, 40.0f, L"unknown group");
    b.Add(0, 2, 40.0f, L"");  // nameless
    b.Add(0, 2, 41.0f, L"ok");
    feed::Decoded d;
    CHECK(b.Decode(d));
    CHECK(!d.cpuTemperature);
    CHECK_EQ(d.readings.size(), size_t{1});
    if (!d.readings.empty()) CHECK_EQ(d.readings[0].name, std::wstring(L"ok"));
}

TEST(Feed_strips_control_characters_and_bounds_names) {
    Block b;
    b.Add(0, 2, 40.0f, std::wstring(L"evil\nname\t") + std::wstring(80, L'x'));
    feed::Decoded d;
    CHECK(b.Decode(d));
    CHECK_EQ(d.readings.size(), size_t{1});
    if (!d.readings.empty()) {
        CHECK(d.readings[0].name.find(L'\n') == std::wstring::npos);
        CHECK(d.readings[0].name.size() <= feed::kNameChars);
    }
}

TEST(Feed_absent_service_is_harmless) {
    // Whether or not the service runs on this machine, reading must never crash or block.
    feed::SensorFeed reader;
    feed::Decoded d;
    const bool fresh = reader.Read(d);
    std::printf("      advanced sensors service: %s (installed: %s)\n", fresh ? "publishing" : "not publishing",
                feed::ServiceInstalled() ? "yes" : "no");
}

TEST(Feed_reads_fan_roles_and_stopped_fans) {
    Block b;
    b.Add(1, 3, 2591.0f, L"Fan #2", 1);      // CPU
    b.Add(1, 3, 1205.0f, L"Fan #1", 3);      // case
    b.Add(1, 1, 0.0f, L"AMD Radeon RX", 2);  // GPU fan stopped (zero-RPM mode)
    b.Add(1, 3, 900.0f, L"Fan #3", 42);      // unknown role from a future service
    b.Add(0, 2, 40.0f, L"Some SSD", 1);      // roles only apply to fans
    feed::Decoded d;
    CHECK(b.Decode(d));
    CHECK_EQ(d.readings.size(), size_t{5});
    if (d.readings.size() == 5) {
        CHECK(d.readings[0].role == FanRole::Cpu);
        CHECK(d.readings[1].role == FanRole::Case);
        CHECK(d.readings[2].role == FanRole::Gpu);
        CHECK_EQ(d.readings[2].value, 0.0);
        CHECK(d.readings[3].role == FanRole::Unknown);
        CHECK(d.readings[4].role == FanRole::Unknown);
    }
}

TEST(Feed_reads_the_service_version) {
    Block b;
    b.Put<uint32_t>(28, feed::EncodeVersion(1, 2, 3));
    feed::Decoded d;
    CHECK(b.Decode(d));
    CHECK_EQ(d.serviceVersion, feed::EncodeVersion(1, 2, 3));
    // Ordering works numerically across fields, so "older than the app" is a plain comparison.
    CHECK(feed::EncodeVersion(1, 9, 9) < feed::EncodeVersion(1, 10, 0));
    CHECK(feed::EncodeVersion(1, 10, 0) < feed::EncodeVersion(2, 0, 0));
    CHECK(feed::EncodeVersion(1, 0, 1) > feed::EncodeVersion(1, 0, 0));
}