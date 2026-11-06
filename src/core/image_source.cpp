#include "core/image_source.h"

#include <array>
#include <fstream>
#include <new>

#include "core/jpeg_source.h"
#include "core/overview_source.h"
#include "core/tiff_source.h"

namespace iv {

namespace {

Result<std::vector<std::uint8_t>> readFile(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
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

}  // namespace

Result<std::shared_ptr<ImageSource>> openImageSource(const std::filesystem::path& path) {
    // Only the header: a TIFF can be far larger than memory.
    std::array<std::uint8_t, 16> header{};
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return std::unexpected("cannot open file");
        }
        file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    }

    Result<std::shared_ptr<ImageSource>> source = std::unexpected("unsupported image format");
    if (isTiff(header)) {
        source = openTiffSource(path);
    } else if (isJpeg(header)) {
        auto bytes = readFile(path);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        source = openJpegSource(std::move(*bytes));
    }
    if (!source) {
        return source;
    }
    return withOverviews(std::move(*source));
}

std::span<const std::string_view> supportedExtensions() {
    static constexpr std::array<std::string_view, 6> kExtensions{"jpg", "jpeg", "jpe", "jfif", "tif", "tiff"};
    return kExtensions;
}

}  // namespace iv
