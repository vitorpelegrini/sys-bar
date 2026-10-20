#include "test.h"

#include "hardware.h"

TEST(Disk_instance_names_are_parsed) {
    int number = -1;
    std::wstring letters;
    CHECK(hw::ParseDiskInstance(L"0 C: E:", number, letters));
    CHECK_EQ(number, 0);
    CHECK_EQ(letters, std::wstring(L"C: E:"));
    CHECK(hw::ParseDiskInstance(L"12 D:", number, letters));
    CHECK_EQ(number, 12);
    CHECK_EQ(letters, std::wstring(L"D:"));
    CHECK(hw::ParseDiskInstance(L"3", number, letters));  // disk without letters
    CHECK_EQ(number, 3);
    CHECK_EQ(letters, std::wstring(L""));
    CHECK(!hw::ParseDiskInstance(L"_Total", number, letters));
    CHECK(!hw::ParseDiskInstance(L"", number, letters));
    CHECK(!hw::ParseDiskInstance(nullptr, number, letters));
    CHECK(!hw::ParseDiskInstance(L"99999 X:", number, letters));  // absurd
}

TEST(Storage_devices_enumerate_without_admin) {
    // Every machine has at least one physical disk, and opening it for property queries needs no rights.
    hw::StorageDevices storage;
    storage.Refresh();
    CHECK(storage.Find(0) != nullptr);
    for (int n = 0; n < 32; ++n) {
        const hw::DiskDevice* disk = storage.Find(n);
        if (!disk) continue;
        storage.UpdateTemperature(n);  // must not crash or hang, whether supported or not
        std::printf("      disk %d: %-24s kind %-4s temperature %s\n", n, t::Narrow(disk->model).c_str(),
                    t::Narrow(hw::DiskKindName(disk->kind)).c_str(),
                    disk->temperature ? std::to_string(static_cast<int>(*disk->temperature)).c_str() : "n/a");
    }
}

TEST(Network_meter_turns_counters_into_rates) {
    hw::NetworkMeter meter;
    hw::NetInterface eth{1, L"Ethernet", 1'000'000, 500'000, 1'000'000'000ull};
    hw::NetInterface wifi{2, L"Wi-Fi", 0, 0, 300'000'000ull};
    hw::NetRates r = meter.Update({eth, wifi}, 10'000);
    CHECK(r.available);
    CHECK_EQ(r.downBps, uint64_t{0});  // first call: nothing to compare with
    CHECK_EQ(r.names, std::wstring(L"Ethernet + Wi-Fi"));
    CHECK_EQ(r.linkBps, uint64_t{1'000'000'000ull});

    eth.inOctets += 2'000'000;  // 2 MB down and 1 MB up in 2 s
    eth.outOctets += 1'000'000;
    wifi.inOctets += 200'000;
    r = meter.Update({eth, wifi}, 12'000);
    CHECK_EQ(r.downBps, uint64_t{1'100'000});
    CHECK_EQ(r.upBps, uint64_t{500'000});

    eth.inOctets = 10;  // counter reset (adapter restarted): no bogus spike
    r = meter.Update({eth}, 13'000);
    CHECK_EQ(r.downBps, uint64_t{0});
    CHECK_EQ(r.names, std::wstring(L"Ethernet"));

    CHECK(!meter.Update({}, 14'000).available);
}

TEST(Network_and_battery_read_without_admin) {
    const auto interfaces = hw::ReadNetInterfaces();
    for (const auto& i : interfaces) {
        std::printf("      network: %s, link %llu Mbps\n", t::Narrow(i.name).c_str(),
                    static_cast<unsigned long long>(i.linkBps / 1'000'000));
    }
    hw::BatteryMeter battery;
    const hw::BatteryReading b = battery.Read();
    std::printf("      battery: %s\n", b.present ? "present" : "none");
    if (b.present) CHECK(b.percent >= 0.0 && b.percent <= 100.0);
}

TEST(Disk_kind_names) {
    CHECK_EQ(std::wstring(hw::DiskKindName(hw::DiskKind::Nvme)), std::wstring(L"NVMe"));
    CHECK_EQ(std::wstring(hw::DiskKindName(hw::DiskKind::Ssd)), std::wstring(L"SSD"));
    CHECK_EQ(std::wstring(hw::DiskKindName(hw::DiskKind::Hdd)), std::wstring(L"HDD"));
    CHECK_EQ(std::wstring(hw::DiskKindName(hw::DiskKind::Usb)), std::wstring(L"USB"));
    CHECK_EQ(std::wstring(hw::DiskKindName(hw::DiskKind::Unknown)), std::wstring(L""));
}