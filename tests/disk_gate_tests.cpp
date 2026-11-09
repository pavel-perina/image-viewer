#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <latch>
#include <mutex>
#include <thread>
#include <vector>

#include "core/disk_gate.h"

using namespace iv;
using namespace std::chrono_literals;

namespace {

constexpr std::string_view kMountinfo =
    "22 1 0:21 / / rw,relatime shared:1 - btrfs /dev/nvme0n1p2 rw,subvol=/@\n"
    "30 22 0:33 / /home rw,relatime shared:2 - ext4 /dev/sda1 rw\n"
    "41 30 8:33 / /run/media/me/My\\040Card rw,nosuid - vfat /dev/sdc1 rw\n"
    "50 22 0:40 / /mnt/share rw - cifs //server/photos rw\n";

DiskGate::Locator fixed(DiskKind kind) {
    return [kind](const std::filesystem::path& path) { return DiskInfo{path.parent_path().string(), kind}; };
}

}  // namespace

TEST(DiskGate, FindsTheClosestMountPoint) {
    auto mount = detail::findMount(kMountinfo, "/home/me/photos/a.jpg");
    ASSERT_TRUE(mount);
    EXPECT_EQ(mount->mountPoint, "/home");
    EXPECT_EQ(mount->source, "/dev/sda1");
    EXPECT_EQ(mount->device, "0:33");

    mount = detail::findMount(kMountinfo, "/homework/a.jpg");
    ASSERT_TRUE(mount);
    EXPECT_EQ(mount->mountPoint, "/") << "a name that only starts like a mount point is not in it";
    EXPECT_EQ(mount->fileSystem, "btrfs");

    mount = detail::findMount(kMountinfo, "/run/media/me/My Card/DCIM/a.jpg");
    ASSERT_TRUE(mount);
    EXPECT_EQ(mount->mountPoint, "/run/media/me/My Card") << "spaces come escaped";

    EXPECT_FALSE(detail::findMount(kMountinfo, "relative/a.jpg"));
}

TEST(DiskGate, KnowsNetworkAndMemoryFileSystems) {
    EXPECT_EQ(detail::kindOfFileSystem("cifs"), DiskKind::Seeking);
    EXPECT_EQ(detail::kindOfFileSystem("nfs4"), DiskKind::Seeking);
    EXPECT_EQ(detail::kindOfFileSystem("tmpfs"), DiskKind::Solid);
    EXPECT_EQ(detail::kindOfFileSystem("ext4"), std::nullopt);
}

TEST(DiskGate, LetsOneReaderAtATimeOntoASeekingDisk) {
    DiskGate gate(fixed(DiskKind::Seeking));
    std::atomic<int> inside = 0;
    std::atomic<int> most = 0;
    std::vector<std::jthread> readers;
    for (int i = 0; i < 6; ++i) {
        readers.emplace_back([&] {
            auto pass = gate.enter("/disk/file.jpg", ReadPriority::Background);
            ASSERT_TRUE(pass);
            const int now = ++inside;
            most = std::max(most.load(), now);
            std::this_thread::sleep_for(5ms);
            --inside;
        });
    }
    readers.clear();
    EXPECT_EQ(most, 1);
}

TEST(DiskGate, SolidDisksAndOtherDisksDoNotWait) {
    DiskGate solid(fixed(DiskKind::Solid));
    auto first = solid.enter("/ssd/a.jpg", ReadPriority::Background);
    auto second = solid.enter("/ssd/b.jpg", ReadPriority::Background);
    EXPECT_TRUE(first && second);

    DiskGate seeking(fixed(DiskKind::Seeking));
    auto one = seeking.enter("/one/a.jpg", ReadPriority::Background);
    auto other = seeking.enter("/other/a.jpg", ReadPriority::Background);
    EXPECT_TRUE(one && other) << "each disk has its own turn";
}

TEST(DiskGate, AnImageToShowGoesBeforeWaitingThumbnails) {
    DiskGate gate(fixed(DiskKind::Seeking));
    auto held = gate.enter("/disk/held.jpg", ReadPriority::Background);
    ASSERT_TRUE(held);

    std::mutex mutex;
    std::vector<char> order;
    std::jthread thumbnail([&] {
        auto pass = gate.enter("/disk/t.jpg", ReadPriority::Background);
        const std::scoped_lock lock(mutex);
        order.push_back('t');
    });
    std::this_thread::sleep_for(20ms);  // the thumbnail waits first
    std::jthread image([&] {
        auto pass = gate.enter("/disk/i.jpg", ReadPriority::Foreground);
        const std::scoped_lock lock(mutex);
        order.push_back('i');
    });
    std::this_thread::sleep_for(20ms);
    held.reset();
    thumbnail.join();
    image.join();
    EXPECT_EQ(order, (std::vector<char>{'i', 't'}));
}

TEST(DiskGate, AStopWhileWaitingGivesNoPass) {
    DiskGate gate(fixed(DiskKind::Unknown));
    auto held = gate.enter("/disk/held.jpg", ReadPriority::Foreground);
    ASSERT_TRUE(held);
    std::stop_source stop;
    std::latch waiting(1);
    std::optional<DiskGate::Pass> result;
    std::jthread reader([&] {
        waiting.count_down();
        result = gate.enter("/disk/t.jpg", ReadPriority::Background, stop.get_token());
    });
    waiting.wait();
    std::this_thread::sleep_for(10ms);
    stop.request_stop();
    reader.join();
    EXPECT_FALSE(result);
    held.reset();
    EXPECT_TRUE(gate.enter("/disk/t.jpg", ReadPriority::Background)) << "the disk is free again";
}

#ifdef __linux__
TEST(DiskGate, FindsTheDiskOfARealFolder) {
    const auto folder = std::filesystem::temp_directory_path();
    const DiskInfo disk = diskOf(folder / "x.jpg");
    ASSERT_FALSE(disk.key.empty());
    EXPECT_TRUE(std::filesystem::weakly_canonical(folder).string().starts_with(disk.key));
    EXPECT_EQ(diskOf(folder / "y.jpg").key, disk.key);
}
#endif
