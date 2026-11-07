#include "core/file_io.h"

#include <fstream>
#include <new>

namespace iv {

namespace fs = std::filesystem;

Result<std::vector<std::uint8_t>> readFile(const fs::path& path) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) {
        return std::unexpected(ec.message());
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected("cannot open file");
    }
    try {
        std::vector<std::uint8_t> bytes(size);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        if (static_cast<std::uintmax_t>(file.gcount()) != size) {
            return std::unexpected("cannot read file");
        }
        return bytes;
    } catch (const std::bad_alloc&) {
        return std::unexpected("not enough memory to read file");
    }
}

Result<void> writeFileSafely(const fs::path& path, std::span<const std::uint8_t> bytes, bool replace) {
    std::error_code ec;
    const fs::file_status target = fs::status(path, ec);
    if (!replace && fs::exists(target)) {
        return std::unexpected(path.filename().string() + " already exists");
    }
    // Hidden, and not an image extension, so a folder view does not pick it up meanwhile.
    fs::path temporary = path;
    temporary.replace_filename("." + path.filename().string() + ".part");
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return std::unexpected("cannot write to the folder");
        }
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.close();
        if (!file) {
            fs::remove(temporary, ec);
            return std::unexpected("cannot write the file, the disk may be full");
        }
    }
    if (fs::exists(target)) {
        fs::permissions(temporary, target.permissions(), ec);
    }
    // Checked again just before, which leaves only a tiny window for another program.
    if (!replace && fs::exists(path)) {
        fs::remove(temporary, ec);
        return std::unexpected(path.filename().string() + " already exists");
    }
    fs::rename(temporary, path, ec);
    if (ec) {
        const std::string message = ec.message();
        fs::remove(temporary, ec);
        return std::unexpected("cannot replace the file: " + message);
    }
    return {};
}

}  // namespace iv
