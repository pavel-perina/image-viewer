#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace iv {

// What a disk does when several files are read from it at once.
enum class DiskKind {
    Solid,    // SSD, NVMe, RAM: reads in parallel at no cost
    Seeking,  // spinning disk, also USB sticks, SD cards and network shares, which do no better
    Unknown,  // treated as seeking, the safe side
};

enum class ReadPriority {
    Foreground,  // an image to show: goes before every background read waiting for the disk
    Background,  // thumbnails
};

// The mount point (Linux), volume (Windows) or other key for the disk a file is on, and its kind.
struct DiskInfo {
    std::string key;
    DiskKind kind = DiskKind::Unknown;
};

// Finds the disk of a path from the system, cached per folder, as files of one folder share it.
DiskInfo diskOf(const std::filesystem::path& path);

// Lets one file at a time be read from a disk that seeks, so a folder of thumbnails is read file
// by file rather than jumping between ten of them; disks without seeking are not limited. Only
// reading is gated: decoding goes on in parallel.
class DiskGate {
public:
    using Locator = std::function<DiskInfo(const std::filesystem::path&)>;

    explicit DiskGate(Locator locator = diskOf);

    static DiskGate& global();

    // Held while reading; the next reader goes when it is destroyed.
    class Pass {
    public:
        Pass() = default;
        Pass(Pass&& other) noexcept;
        Pass& operator=(Pass&& other) noexcept;
        Pass(const Pass&) = delete;
        Pass& operator=(const Pass&) = delete;
        ~Pass();

    private:
        friend class DiskGate;
        Pass(DiskGate* gate, std::string key) : m_gate(gate), m_key(std::move(key)) {}
        void release();
        DiskGate* m_gate = nullptr;
        std::string m_key;
    };

    // Waits for a turn on the disk of the path, or returns nothing if stopped while waiting.
    std::optional<Pass> enter(const std::filesystem::path& path, ReadPriority priority, std::stop_token stop = {});

private:
    struct Lane {
        int active = 0;
        int foregroundWaiting = 0;
    };

    void leave(const std::string& key);

    Locator m_locator;
    std::mutex m_mutex;
    std::condition_variable_any m_changed;
    std::map<std::string, Lane> m_lanes;
};

namespace detail {

struct MountEntry {
    std::string mountPoint;
    std::string fileSystem;
    std::string source;
    std::string device;  // "major:minor"
};

// The entry of /proc/self/mountinfo whose mount point holds the absolute path most closely.
std::optional<MountEntry> findMount(std::string_view mountinfo, const std::string& path);

// The kind of a mounted file system: by its type for network and memory ones, otherwise unknown,
// left to the block device.
std::optional<DiskKind> kindOfFileSystem(std::string_view fileSystem);

}  // namespace detail

}  // namespace iv
