#include "core/image_source.h"

#include <array>
#include <cstring>
#include <fstream>
#include <new>

#ifdef IV_HAVE_AVIF
#include "core/avif_source.h"
#endif
#include "core/file_io.h"
#include "core/jpeg_source.h"
#ifdef IV_HAVE_JXL
#include "core/jxl_source.h"
#endif
#include "core/overview_source.h"
#ifdef IV_HAVE_PNG
#include "core/png_source.h"
#endif
#include "core/tiff_source.h"
#ifdef IV_HAVE_WEBP
#include "core/webp_source.h"
#endif

namespace iv {

Result<std::shared_ptr<ImageSource>> openImageSource(const std::filesystem::path& path, ReadPriority priority,
                                                     std::stop_token stop) {
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
    } else {
        // These formats need their whole file in memory anyway, to decode it.
        using Opener = Result<std::shared_ptr<ImageSource>> (*)(std::vector<std::uint8_t>);
        Opener opener = nullptr;
        if (isJpeg(header)) {
            opener = openJpegSource;
        }
#ifdef IV_HAVE_PNG
        if (isPng(header)) {
            opener = openPngSource;
        }
#endif
#ifdef IV_HAVE_WEBP
        if (isWebp(header)) {
            opener = openWebpSource;
        }
#endif
#ifdef IV_HAVE_AVIF
        if (isAvif(header)) {
            opener = openAvifSource;
        }
#endif
#ifdef IV_HAVE_JXL
        if (isJxl(header)) {
            opener = openJxlSource;
        }
#endif
        if (opener) {
            auto pass = DiskGate::global().enter(path, priority, stop);
            if (!pass) {
                return std::unexpected("cancelled");
            }
            auto bytes = readFile(path);
            pass.reset();  // decoding needs the disk no more
            if (!bytes) {
                return std::unexpected(bytes.error());
            }
            source = opener(std::move(*bytes));
        }
    }
    if (!source) {
        return source;
    }
    return withOverviews(std::move(*source));
}

std::span<const std::string_view> supportedExtensions() {
    static const std::vector<std::string_view> kExtensions{
        "jpg", "jpeg", "jpe", "jfif", "tif", "tiff",
#ifdef IV_HAVE_PNG
        "png",
#endif
#ifdef IV_HAVE_WEBP
        "webp",
#endif
#ifdef IV_HAVE_AVIF
        "avif",
#endif
#ifdef IV_HAVE_JXL
        "jxl",
#endif
    };
    return kExtensions;
}

Result<ImageBuffer> readLevel(ImageSource& source, int subimage, int levelIndex, const std::stop_token& stop) {
    const SubimageInfo& info = source.subimages()[static_cast<std::size_t>(subimage)];
    const LevelInfo& level = info.levels[static_cast<std::size_t>(levelIndex)];
    const int tileWidth = level.tileSize.width;
    const int tileHeight = level.tileSize.height;
    const int cols = (level.size.width + tileWidth - 1) / tileWidth;
    const int rows = (level.size.height + tileHeight - 1) / tileHeight;

    if (cols == 1 && rows == 1) {
        return source.readTile({subimage, levelIndex, 0, 0}, stop);
    }

    const auto pixelBytes = static_cast<std::size_t>(bytesPerPixel(info.format));
    ImageBuffer whole = ImageBuffer::create(level.size, info.format);
    bool alpha = false;
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const auto tile = source.readTile({subimage, levelIndex, col, row}, stop);
            if (!tile) {
                return tile;
            }
            alpha = alpha || tile->hasAlpha();
            for (int y = 0; y < tile->height(); ++y) {
                std::memcpy(whole.row(row * tileHeight + y) + static_cast<std::size_t>(col * tileWidth) * pixelBytes,
                            tile->row(y), static_cast<std::size_t>(tile->width()) * pixelBytes);
            }
        }
    }
    return whole.withAlpha(alpha);
}

}  // namespace iv
