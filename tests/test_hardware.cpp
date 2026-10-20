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

TEST(Disk_kind_names) {
    CHECK_EQ(std::wstring(hw::DiskKindName(hw::DiskKind::Nvme)), std::wstring(L"NVMe"));
    CHECK_EQ(std::wstring(hw::DiskKindName(hw::DiskKind::Ssd)), std::wstring(L"SSD"));
    CHECK_EQ(std::wstring(hw::DiskKindName(hw::DiskKind::Hdd)), std::wstring(L"HDD"));
    CHECK_EQ(std::wstring(hw::DiskKindName(hw::DiskKind::Usb)), std::wstring(L"USB"));
    CHECK_EQ(std::wstring(hw::DiskKindName(hw::DiskKind::Unknown)), std::wstring(L""));
}