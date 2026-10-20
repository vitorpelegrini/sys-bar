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
    const hw::DiskDevice* first = storage.Find(0);
    CHECK(first != nullptr);
    if (first) {
        storage.UpdateTemperature(0);  // must not crash or hang, whether supported or not
        std::printf("      disk 0: %s%s, temperature %s\n", t::Narrow(first->model).c_str(),
                    first->nvme ? " (NVMe)" : "",
                    first->temperature ? std::to_string(static_cast<int>(*first->temperature)).c_str() : "n/a");
    }
}