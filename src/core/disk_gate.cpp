#include "core/disk_gate.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#elif defined(__linux__)
#include <sys/stat.h>
#include <sys/sysmacros.h>
#endif

namespace iv {

namespace fs = std::filesystem;

namespace detail {

namespace {

// Mount points escape spaces, tabs, newlines and backslashes as three octal digits.
std::string unescape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 4 <= text.size() &&
            std::all_of(text.begin() + static_cast<std::ptrdiff_t>(i) + 1, text.begin() + static_cast<std::ptrdiff_t>(i) + 4,
                        [](char c) { return c >= '0' && c <= '7'; })) {
            out += static_cast<char>((text[i + 1] - '0') * 64 + (text[i + 2] - '0') * 8 + (text[i + 3] - '0'));
            i += 3;
        } else {
            out += text[i];
        }
    }
    return out;
}

bool holds(const std::string& mountPoint, const std::string& path) {
    if (mountPoint == "/") {
        return !path.empty() && path.front() == '/';
    }
    return path == mountPoint || (path.starts_with(mountPoint) && path[mountPoint.size()] == '/');
}

}  // namespace

std::optional<MountEntry> findMount(std::string_view mountinfo, const std::string& path) {
    std::optional<MountEntry> best;
    std::istringstream lines{std::string(mountinfo)};
    std::string line;
    while (std::getline(lines, line)) {
        // id parent major:minor root mount-point options [optional fields] - type source options
        std::istringstream fields(line);
        std::vector<std::string> words;
        for (std::string word; fields >> word;) {
            words.push_back(word);
        }
        const auto separator = std::ranges::find(words, std::string("-"));
        if (words.size() < 5 || separator == words.end() || words.end() - separator < 3) {
            continue;
        }
        MountEntry entry{unescape(words[4]), *(separator + 1), unescape(*(separator + 2)), words[2]};
        // Later entries mount over earlier ones at the same place.
        if (holds(entry.mountPoint, path) && (!best || entry.mountPoint.size() >= best->mountPoint.size())) {
            best = std::move(entry);
        }
    }
    return best;
}

std::optional<std::pair<unsigned, unsigned>> parseDeviceNumbers(std::string_view device) {
    const auto colon = device.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const auto number = [](std::string_view text) -> std::optional<unsigned> {
        unsigned value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (text.empty() || error != std::errc() || end != text.data() + text.size()) {
            return std::nullopt;
        }
        return value;
    };
    const auto major = number(device.substr(0, colon));
    const auto minor = number(device.substr(colon + 1));
    if (!major || !minor) {
        return std::nullopt;
    }
    return std::pair{*major, *minor};
}

std::optional<DiskKind> kindOfFileSystem(std::string_view fileSystem) {
    static constexpr std::array<std::string_view, 12> kNetwork{"nfs",  "nfs4", "cifs", "smb3",      "smbfs",  "9p",
                                                               "afs",  "ceph", "glusterfs", "davfs", "fuse.sshfs", "sshfs"};
    static constexpr std::array<std::string_view, 3> kMemory{"tmpfs", "ramfs", "devtmpfs"};
    if (std::ranges::find(kNetwork, fileSystem) != kNetwork.end()) {
        return DiskKind::Seeking;
    }
    if (std::ranges::find(kMemory, fileSystem) != kMemory.end()) {
        return DiskKind::Solid;
    }
    return std::nullopt;
}

}  // namespace detail

namespace {

#if defined(__linux__)

std::string readText(const fs::path& path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

// From the block device's queue, the whole disk's for a partition. USB sticks often call
// themselves rotational, which suits them anyway.
DiskKind kindOfBlockDevice(unsigned major, unsigned minor) {
    std::error_code ec;
    fs::path device = fs::canonical(fs::path("/sys/dev/block") / (std::to_string(major) + ":" + std::to_string(minor)), ec);
    if (ec) {
        return DiskKind::Unknown;
    }
    if (fs::exists(device / "partition", ec)) {
        device = device.parent_path();
    }
    const std::string rotational = readText(device / "queue" / "rotational");
    if (rotational.starts_with('0')) {
        return DiskKind::Solid;
    }
    if (rotational.starts_with('1')) {
        return DiskKind::Seeking;
    }
    return DiskKind::Unknown;
}

DiskInfo locate(const fs::path& folder) {
    const auto mount = detail::findMount(readText("/proc/self/mountinfo"), folder.string());
    if (!mount) {
        return {};
    }
    DiskInfo info{mount->mountPoint, DiskKind::Unknown};
    if (const auto kind = detail::kindOfFileSystem(mount->fileSystem)) {
        info.kind = *kind;
        return info;
    }
    // The source device, as Btrfs gives its subvolumes device numbers of their own.
    struct stat source {};
    if (mount->source.starts_with("/dev/") && stat(mount->source.c_str(), &source) == 0 && S_ISBLK(source.st_mode)) {
        info.kind = kindOfBlockDevice(major(source.st_rdev), minor(source.st_rdev));
    } else if (const auto numbers = detail::parseDeviceNumbers(mount->device); numbers && numbers->first != 0) {
        info.kind = kindOfBlockDevice(numbers->first, numbers->second);
    }
    return info;
}

#elif defined(_WIN32)

DiskKind kindOfVolume(const std::wstring& root) {
    switch (GetDriveTypeW(root.c_str())) {
    case DRIVE_FIXED: break;
    case DRIVE_RAMDISK: return DiskKind::Solid;
    case DRIVE_REMOVABLE:
    case DRIVE_REMOTE:
    case DRIVE_CDROM: return DiskKind::Seeking;
    default: return DiskKind::Unknown;
    }
    std::array<wchar_t, MAX_PATH> name{};
    if (!GetVolumeNameForVolumeMountPointW(root.c_str(), name.data(), static_cast<DWORD>(name.size()))) {
        return DiskKind::Unknown;
    }
    std::wstring volume(name.data());
    if (volume.ends_with(L'\\')) {
        volume.pop_back();  // a trailing backslash would open the root folder, not the volume
    }
    // No access rights asked for: a query of the device's properties needs none, nor admin.
    const HANDLE handle = CreateFileW(volume.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return DiskKind::Unknown;
    }
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR penalty{};
    DWORD bytes = 0;
    const BOOL ok = DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &penalty, sizeof(penalty), &bytes, nullptr);
    CloseHandle(handle);
    if (!ok || bytes < sizeof(penalty)) {
        return DiskKind::Unknown;  // such as a volume spanning disks
    }
    return penalty.IncursSeekPenalty ? DiskKind::Seeking : DiskKind::Solid;
}

DiskInfo locate(const fs::path& folder) {
    std::array<wchar_t, MAX_PATH> root{};
    if (!GetVolumePathNameW(folder.c_str(), root.data(), static_cast<DWORD>(root.size()))) {
        return {};
    }
    const std::wstring volume(root.data());
    // Only needs to be unique; UTF-8 keeps a mount folder with any name intact.
    const std::u8string key = fs::path(volume).u8string();
    return {std::string(key.begin(), key.end()), kindOfVolume(volume)};
}

#else

// Not looked into elsewhere: one gate for everything, as for an unknown disk.
DiskInfo locate(const fs::path&) { return {}; }

#endif

}  // namespace

DiskInfo diskOf(const fs::path& path) {
    static std::mutex mutex;
    static std::unordered_map<std::string, DiskInfo> byFolder;
    std::error_code ec;
    fs::path folder = fs::weakly_canonical(fs::absolute(path, ec).parent_path(), ec);
    const std::string key = folder.string();
    {
        const std::scoped_lock lock(mutex);
        if (const auto found = byFolder.find(key); found != byFolder.end()) {
            return found->second;
        }
    }
    DiskInfo info = locate(folder);
    const std::scoped_lock lock(mutex);
    if (byFolder.size() > 4096) {
        byFolder.clear();  // folders come and go, and a disk mounted later may sit where one was
    }
    byFolder.emplace(key, info);
    return info;
}

DiskGate::DiskGate(Locator locator) : m_locator(std::move(locator)) {}

DiskGate& DiskGate::global() {
    static DiskGate gate;
    return gate;
}

std::optional<DiskGate::Pass> DiskGate::enter(const fs::path& path, ReadPriority priority, std::stop_token stop) {
    const DiskInfo disk = m_locator(path);
    if (disk.kind == DiskKind::Solid) {
        return Pass();
    }
    const bool foreground = priority == ReadPriority::Foreground;
    std::unique_lock lock(m_mutex);
    Lane& lane = m_lanes[disk.key];
    if (foreground) {
        ++lane.foregroundWaiting;
    }
    const bool turn = m_changed.wait(lock, stop, [&] { return lane.active == 0 && (foreground || lane.foregroundWaiting == 0); });
    if (foreground) {
        --lane.foregroundWaiting;
    }
    if (!turn) {
        m_changed.notify_all();  // the background readers may have waited for this one
        return std::nullopt;
    }
    ++lane.active;
    return Pass(this, disk.key);
}

void DiskGate::leave(const std::string& key) {
    {
        const std::scoped_lock lock(m_mutex);
        --m_lanes[key].active;
    }
    m_changed.notify_all();
}

DiskGate::Pass::Pass(Pass&& other) noexcept : m_gate(std::exchange(other.m_gate, nullptr)), m_key(std::move(other.m_key)) {}

DiskGate::Pass& DiskGate::Pass::operator=(Pass&& other) noexcept {
    if (this != &other) {
        release();
        m_gate = std::exchange(other.m_gate, nullptr);
        m_key = std::move(other.m_key);
    }
    return *this;
}

DiskGate::Pass::~Pass() { release(); }

void DiskGate::Pass::release() {
    if (m_gate) {
        std::exchange(m_gate, nullptr)->leave(m_key);
    }
}

}  // namespace iv
