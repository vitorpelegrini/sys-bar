#include "test.h"

#include "hardware.h"

#include <algorithm>

TEST(Disk_activity_comes_from_the_growth_of_the_drive_counters) {
    hw::DiskTotals before, now;
    before.queryTime = 1'000'000'000;
    before.idleTime = 500'000'000;
    before.bytesRead = 10'000;
    before.bytesWritten = 99'000;
    now.queryTime = before.queryTime + 10'000'000;  // one second later
    now.idleTime = before.idleTime + 7'500'000;     // idle for 75% of it
    now.bytesRead = before.bytesRead + 4096;
    now.bytesWritten = before.bytesWritten + 2'000'000;
    hw::DiskActivity activity;
    hw::ActivityBetween(before, now, activity);
    CHECK_NEAR(activity.active, 25.0, 1e-9);
    CHECK_EQ(activity.readBps, 4096ull);
    CHECK_EQ(activity.writeBps, 2'000'000ull);

    // Half a second: the same bytes are twice the rate.
    now.queryTime = before.queryTime + 5'000'000;
    now.idleTime = before.idleTime;  // busy all the time
    hw::ActivityBetween(before, now, activity);
    CHECK_NEAR(activity.active, 100.0, 1e-9);
    CHECK_EQ(activity.readBps, 8192ull);

    // More idle time than time (rounding in the driver) is still 0% active, never negative.
    now.idleTime = before.idleTime + 6'000'000;
    hw::ActivityBetween(before, now, activity);
    CHECK_NEAR(activity.active, 0.0, 1e-9);

    // No time passed, or counters that went backwards (the drive was replugged): nothing, not garbage.
    hw::ActivityBetween(before, before, activity);
    CHECK_EQ(activity.active, 0.0);
    CHECK_EQ(activity.readBps, 0ull);
    now = before;
    now.queryTime += 10'000'000;
    now.idleTime += 10'000'000;
    now.bytesRead = 5;
    now.bytesWritten = 7;
    hw::ActivityBetween(before, now, activity);
    CHECK_EQ(activity.readBps, 0ull);
    CHECK_EQ(activity.writeBps, 0ull);
}

TEST(Disk_counters_list_the_system_drive_and_measure_it) {
    hw::DiskCounters disks;
    const ULONGLONG t0 = GetTickCount64();
    const auto first = disks.Read(t0);
    CHECK(!first.empty());  // every machine has a physical drive
    wchar_t windows[MAX_PATH] = {};
    GetWindowsDirectoryW(windows, MAX_PATH);
    const std::wstring systemLetter = {windows[0], L':'};
    bool systemDrive = false;
    for (const auto& d : first) {
        CHECK_EQ(d.active, 0.0);  // no previous reading yet
        systemDrive |= d.letters.find(systemLetter) != std::wstring::npos;
    }
    CHECK(systemDrive);

    Sleep(300);
    const auto second = disks.Read(t0 + 300);
    CHECK_EQ(second.size(), first.size());
    for (const auto& d : second) {
        CHECK(d.active >= 0.0 && d.active <= 100.0);
        std::printf("      disk %d [%s]: %.1f%% active, read %llu B/s, write %llu B/s\n", d.number,
                    t::Narrow(d.letters).c_str(), d.active, static_cast<unsigned long long>(d.readBps),
                    static_cast<unsigned long long>(d.writeBps));
    }

    // After a pause the next reading covers the time since Prime(), not the pause.
    disks.Prime(t0 + 400);
    disks.Invalidate();  // and a rescan keeps the drives and their baselines
    Sleep(200);
    const auto third = disks.Read(t0 + 600);
    CHECK_EQ(third.size(), first.size());
    for (const auto& d : third) CHECK(d.active >= 0.0 && d.active <= 100.0);
}

TEST(Storage_devices_enumerate_without_admin) {
    // Every machine has at least one physical disk, and opening it for property queries needs no rights.
    hw::StorageDevices storage;
    storage.Refresh({0, 1, 2, 3, 4, 5, 6, 7});
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

TEST(Battery_from_power_status) {
    SYSTEM_POWER_STATUS p{};
    p.BatteryFlag = 128;  // desktop: no system battery
    CHECK(!hw::FromPowerStatus(p).present);
    p.BatteryFlag = 255;  // unknown
    CHECK(!hw::FromPowerStatus(p).present);
    p.BatteryFlag = 0;
    p.BatteryLifePercent = 255;  // unknown percent
    CHECK(!hw::FromPowerStatus(p).present);

    p.BatteryLifePercent = 18;
    p.ACLineStatus = 0;
    p.BatteryLifeTime = 3000;
    hw::BatteryReading b = hw::FromPowerStatus(p);
    CHECK(b.present && !b.acOnline && !b.charging);
    CHECK_EQ(b.percent, 18.0);
    CHECK_EQ(b.secondsLeft.value_or(-1), 3000);

    p.BatteryLifeTime = static_cast<DWORD>(-1);  // no estimate yet
    CHECK(!hw::FromPowerStatus(p).secondsLeft);
    p.BatteryLifeTime = 30u * 24 * 3600;  // nonsense
    CHECK(!hw::FromPowerStatus(p).secondsLeft);

    p.ACLineStatus = 1;
    p.BatteryFlag = 8;  // charging
    p.BatteryLifeTime = 5000;
    b = hw::FromPowerStatus(p);
    CHECK(b.acOnline && b.charging);
    CHECK(!b.secondsLeft);  // plugged in: "time left" makes no sense

    p.ACLineStatus = 255;  // unknown AC state
    CHECK(!hw::FromPowerStatus(p).acOnline);
}

TEST(Fake_battery_for_manual_testing) {
    hw::BatteryReading b;
    CHECK(hw::ParseFakeBattery(L"18,0,0,-9.2,3000", b));
    CHECK(b.present && !b.acOnline && !b.charging);
    CHECK_NEAR(b.watts.value_or(0.0), -9.2, 1e-9);
    CHECK_EQ(b.secondsLeft.value_or(-1), 3000);
    CHECK(hw::ParseFakeBattery(L"80,1,1", b));
    CHECK(b.charging && !b.watts && !b.secondsLeft);
    CHECK(!hw::ParseFakeBattery(L"", b));
    CHECK(!hw::ParseFakeBattery(L"150,0,0", b));
    CHECK(!hw::ParseFakeBattery(L"50,x,0", b));
    CHECK(!hw::ParseFakeBattery(L"50,0", b));
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

TEST(Network_reader_matches_the_full_table_and_rarely_reads_it) {
    auto sameSet = [](std::vector<hw::NetInterface> a, std::vector<hw::NetInterface> b) {
        auto byLuid = [](const hw::NetInterface& x, const hw::NetInterface& y) { return x.luid < y.luid; };
        std::sort(a.begin(), a.end(), byLuid);
        std::sort(b.begin(), b.end(), byLuid);
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i].luid != b[i].luid || a[i].name != b[i].name || a[i].wireless != b[i].wireless) return false;
        }
        return true;
    };
    hw::NetInterfaceReader reader;
    const ULONGLONG t0 = GetTickCount64();
    const auto first = reader.Read(t0);
    CHECK(sameSet(first, hw::ReadNetInterfaces()));
    CHECK_EQ(reader.scans(), 1);

    // Within the rescan period only the known interfaces are queried, and their counters keep moving.
    for (int i = 1; i <= 4; ++i) {
        const auto again = reader.Read(t0 + 1000 * i);
        CHECK(sameSet(again, first));
        for (const auto& now : again) {
            const auto before =
                std::find_if(first.begin(), first.end(), [&](const auto& x) { return x.luid == now.luid; });
            if (before != first.end()) CHECK(now.inOctets >= before->inOctets && now.outOctets >= before->outOctets);
        }
    }
    CHECK(reader.scans() <= 2);  // 1, or 2 when Windows reported an interface change meanwhile

    const int scans = reader.scans();
    reader.Read(t0 + 6000);  // the period elapsed
    CHECK_EQ(reader.scans(), scans + 1);
    reader.Invalidate();  // an interface changed
    reader.Read(t0 + 6500);
    CHECK_EQ(reader.scans(), scans + 2);
    reader.Read(t0 + 7000, true);  // explicit rescan (resuming from a pause)
    CHECK_EQ(reader.scans(), scans + 3);
    reader.Close();
}

TEST(Disk_kind_names) {
    CHECK_EQ(std::wstring(hw::DiskKindName(hw::DiskKind::Nvme)), std::wstring(L"NVMe"));
    CHECK_EQ(std::wstring(hw::DiskKindName(hw::DiskKind::Ssd)), std::wstring(L"SSD"));
    CHECK_EQ(std::wstring(hw::DiskKindName(hw::DiskKind::Hdd)), std::wstring(L"HDD"));
    CHECK_EQ(std::wstring(hw::DiskKindName(hw::DiskKind::Usb)), std::wstring(L"USB"));
    CHECK_EQ(std::wstring(hw::DiskKindName(hw::DiskKind::Unknown)), std::wstring(L""));
}