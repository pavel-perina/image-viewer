#include "core/thumbnail.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <cstring>
#include <new>

#include "core/image_ops.h"

namespace iv {

namespace {

// Building a thumbnail for a flat image reads the whole file. Past this size that would hold
// a worker for too long, so such files simply have no thumbnail.
constexpr std::uint64_t kMaxScannedPixels = 256ull * 1024 * 1024;

int chooseThumbnailLevel(const SubimageInfo& subimage, int maxEdge) {
    int chosen = 0;
    for (int i = 0; i < std::ssize(subimage.levels); ++i) {
        const Size size = subimage.levels[static_cast<std::size_t>(i)].size;
        if (std::max(size.width, size.height) < maxEdge) {
            break;
        }
        chosen = i;
    }
    return chosen;
}

bool tooExpensive(const SubimageInfo& subimage, int level) {
    if (!subimage.levels[static_cast<std::size_t>(level)].generated) {
        return false;
    }
    // Generated levels start after the real ones, and are built from the smallest real one.
    const auto real = std::ranges::find_if(subimage.levels, [](const LevelInfo& l) { return l.generated; });
    const Size scanned = std::prev(real)->size;
    return static_cast<std::uint64_t>(scanned.width) * scanned.height > kMaxScannedPixels;
}

Result<ImageBuffer> readLevel(ImageSource& source, int levelIndex, const std::stop_token& stop) {
    const SubimageInfo& subimage = source.subimages()[0];
    const LevelInfo& level = subimage.levels[static_cast<std::size_t>(levelIndex)];
    const int tileWidth = level.tileSize.width;
    const int tileHeight = level.tileSize.height;
    const int cols = (level.size.width + tileWidth - 1) / tileWidth;
    const int rows = (level.size.height + tileHeight - 1) / tileHeight;

    if (cols == 1 && rows == 1) {
        return source.readTile({0, levelIndex, 0, 0}, stop);
    }

    const auto pixelBytes = static_cast<std::size_t>(bytesPerPixel(subimage.format));
    const ImageBuffer whole = ImageBuffer::create(level.size, subimage.format);
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const auto tile = source.readTile({0, levelIndex, col, row}, stop);
            if (!tile) {
                return tile;
            }
            for (int y = 0; y < tile->height(); ++y) {
                std::memcpy(whole.row(row * tileHeight + y) + static_cast<std::size_t>(col * tileWidth) * pixelBytes,
                            tile->row(y), static_cast<std::size_t>(tile->width()) * pixelBytes);
            }
        }
    }
    return whole;
}

ImageBuffer applyOrientation(const ImageBuffer& source, Orientation orientation) {
    if (orientation == Orientation::Normal) {
        return source;
    }
    const ImageBuffer target = ImageBuffer::create(displaySize(source.size(), orientation), source.format());
    const Affine transform = displayFromStored(source.size(), orientation);
    const auto pixelBytes = static_cast<std::size_t>(bytesPerPixel(source.format()));
    for (int y = 0; y < source.height(); ++y) {
        const std::byte* in = source.row(y);
        for (int x = 0; x < source.width(); ++x, in += pixelBytes) {
            double outX, outY;
            transform.apply(x + 0.5, y + 0.5, outX, outY);
            std::memcpy(target.row(static_cast<int>(outY)) + static_cast<std::size_t>(outX) * pixelBytes, in, pixelBytes);
        }
    }
    return target;
}

}  // namespace

Result<ImageBuffer> makeThumbnail(const std::filesystem::path& path, int maxEdge, std::stop_token stop, const ColorTarget& target) {
    const auto source = openImageSource(path);
    if (!source) {
        return std::unexpected(source.error());
    }
    const SubimageInfo& subimage = (*source)->subimages()[0];
    const int levelIndex = chooseThumbnailLevel(subimage, maxEdge);
    if (tooExpensive(subimage, levelIndex)) {
        return std::unexpected("image has no reduced versions and is too large for a thumbnail");
    }

    try {
        const auto level = readLevel(**source, levelIndex, stop);
        if (!level) {
            return level;
        }
        const double scale = std::min(1.0, static_cast<double>(maxEdge) / std::max(level->width(), level->height()));
        const Size size{std::max(1, static_cast<int>(std::lround(level->width() * scale))),
                        std::max(1, static_cast<int>(std::lround(level->height() * scale)))};
        const ImageBuffer small = shrink(*level, size);
        const std::array samples{small};
        const DisplayRange range = usesAutoRangeByDefault(small.format()) ? autoRange(samples) : fullRange(small.format());
        const auto transform = displayTransform(subimage, target);
        return applyOrientation(toDisplay(small, range, transform.get()), subimage.orientation);
    } catch (const std::bad_alloc&) {
        return std::unexpected("not enough memory for thumbnail");
    }
}

}  // namespace iv
