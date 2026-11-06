#include "core/thumbnail.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace iv {

namespace {

constexpr int kBytesPerPixel = 4;

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

Result<ImageBuffer> readLevel(ImageSource& source, int levelIndex, const std::stop_token& stop) {
    const LevelInfo& level = source.subimages()[0].levels[static_cast<std::size_t>(levelIndex)];
    const int tileWidth = level.tileSize.width;
    const int tileHeight = level.tileSize.height;
    const int cols = (level.size.width + tileWidth - 1) / tileWidth;
    const int rows = (level.size.height + tileHeight - 1) / tileHeight;

    if (cols == 1 && rows == 1) {
        return source.readTile({0, levelIndex, 0, 0}, stop);
    }

    const ImageBuffer whole = ImageBuffer::create(level.size, source.subimages()[0].format);
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const auto tile = source.readTile({0, levelIndex, col, row}, stop);
            if (!tile) {
                return tile;
            }
            for (int y = 0; y < tile->height(); ++y) {
                std::memcpy(whole.row(row * tileHeight + y) + static_cast<std::size_t>(col * tileWidth) * kBytesPerPixel,
                            tile->row(y), static_cast<std::size_t>(tile->width()) * kBytesPerPixel);
            }
        }
    }
    return whole;
}

// Averages the source pixels covered by each target pixel.
ImageBuffer shrink(const ImageBuffer& source, Size size) {
    if (source.size() == size) {
        return source;
    }
    const ImageBuffer target = ImageBuffer::create(size, PixelFormat::Bgrx8);
    for (int y = 0; y < size.height; ++y) {
        const int top = static_cast<int>(static_cast<std::int64_t>(y) * source.height() / size.height);
        const int bottom = std::max(top + 1, static_cast<int>(static_cast<std::int64_t>(y + 1) * source.height() / size.height));
        auto* out = reinterpret_cast<std::uint8_t*>(target.row(y));
        for (int x = 0; x < size.width; ++x, out += kBytesPerPixel) {
            const int left = static_cast<int>(static_cast<std::int64_t>(x) * source.width() / size.width);
            const int right = std::max(left + 1, static_cast<int>(static_cast<std::int64_t>(x + 1) * source.width() / size.width));
            std::uint32_t blue = 0, green = 0, red = 0;
            for (int sy = top; sy < bottom; ++sy) {
                const auto* in = reinterpret_cast<const std::uint8_t*>(source.row(sy)) + left * kBytesPerPixel;
                for (int sx = left; sx < right; ++sx, in += kBytesPerPixel) {
                    blue += in[0];
                    green += in[1];
                    red += in[2];
                }
            }
            const auto count = static_cast<std::uint32_t>((bottom - top) * (right - left));
            out[0] = static_cast<std::uint8_t>(blue / count);
            out[1] = static_cast<std::uint8_t>(green / count);
            out[2] = static_cast<std::uint8_t>(red / count);
            out[3] = 255;
        }
    }
    return target;
}

ImageBuffer applyOrientation(const ImageBuffer& source, Orientation orientation) {
    if (orientation == Orientation::Normal) {
        return source;
    }
    const ImageBuffer target = ImageBuffer::create(displaySize(source.size(), orientation), PixelFormat::Bgrx8);
    const Affine transform = displayFromStored(source.size(), orientation);
    for (int y = 0; y < source.height(); ++y) {
        const auto* in = reinterpret_cast<const std::uint8_t*>(source.row(y));
        for (int x = 0; x < source.width(); ++x, in += kBytesPerPixel) {
            double outX, outY;
            transform.apply(x + 0.5, y + 0.5, outX, outY);
            std::memcpy(target.row(static_cast<int>(outY)) + static_cast<std::size_t>(outX) * kBytesPerPixel, in,
                        kBytesPerPixel);
        }
    }
    return target;
}

}  // namespace

Result<ImageBuffer> makeThumbnail(const std::filesystem::path& path, int maxEdge, std::stop_token stop) {
    const auto source = openImageSource(path);
    if (!source) {
        return std::unexpected(source.error());
    }
    const SubimageInfo& subimage = (*source)->subimages()[0];
    if (subimage.format != PixelFormat::Bgrx8) {
        return std::unexpected("this pixel format has no thumbnail support yet");
    }

    try {
        const auto level = readLevel(**source, chooseThumbnailLevel(subimage, maxEdge), stop);
        if (!level) {
            return level;
        }
        const double scale = std::min(1.0, static_cast<double>(maxEdge) / std::max(level->width(), level->height()));
        const Size size{std::max(1, static_cast<int>(std::lround(level->width() * scale))),
                        std::max(1, static_cast<int>(std::lround(level->height() * scale)))};
        return applyOrientation(shrink(*level, size), subimage.orientation);
    } catch (const std::bad_alloc&) {
        return std::unexpected("not enough memory for thumbnail");
    }
}

}  // namespace iv
