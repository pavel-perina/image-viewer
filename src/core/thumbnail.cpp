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
constexpr int kMaxOversampledEdge = 4096;
constexpr std::uint64_t kMaxDirectBytes = 512ull * 1024 * 1024;

// The smallest level with enough pixels, among those that can be had without generating levels,
// which would cost more than the kernel does when it reads the source directly.
int chooseThumbnailLevel(const SubimageInfo& subimage, int maxEdge) {
    int chosen = 0;
    for (int i = 0; i < std::ssize(subimage.levels); ++i) {
        const LevelInfo& level = subimage.levels[static_cast<std::size_t>(i)];
        if (level.generated || std::max(level.size.width, level.size.height) < maxEdge) {
            break;
        }
        chosen = i;
    }
    // A level this large cannot be held at once, while generated ones are built a tile at a time.
    const Size size = subimage.levels[static_cast<std::size_t>(chosen)].size;
    if (static_cast<std::uint64_t>(size.width) * size.height * bytesPerPixel(subimage.format) > kMaxDirectBytes) {
        for (int i = chosen + 1; i < std::ssize(subimage.levels); ++i) {
            const Size smaller = subimage.levels[static_cast<std::size_t>(i)].size;
            if (std::max(smaller.width, smaller.height) < maxEdge) {
                break;
            }
            chosen = i;
        }
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

// 8-bit pixels for the screen, or for sRGB, from pixels of the subimage: through the range
// it is shown with by default, and its profile. With alpha, the colours are converted alone
// and the alpha put back.
ImageBuffer toEightBit(const ImageBuffer& pixels, const SubimageInfo& subimage, const ColorTarget& target) {
    const std::array samples{pixels};
    const DisplayRange range = usesAutoRangeByDefault(pixels.format()) ? autoRange(samples) : fullRange(pixels.format());
    const auto transform = displayTransform(subimage, target);
    if (!pixels.hasAlpha()) {
        return toDisplay(pixels, range, transform.get());
    }
    // Written into a new buffer, as 8-bit pixels without a transform come back as they are.
    const ImageBuffer shown = toDisplay(pixels.withAlpha(false), range, transform.get());
    const ImageBuffer result = ImageBuffer::create(pixels.size(), PixelFormat::Bgrx8).withAlpha(true);
    for (int y = 0; y < pixels.height(); ++y) {
        auto* out = reinterpret_cast<std::uint8_t*>(result.row(y));
        std::memcpy(out, shown.row(y), static_cast<std::size_t>(pixels.width()) * 4);
        for (int x = 0; x < pixels.width(); ++x) {
            switch (pixels.format()) {
            case PixelFormat::Bgrx8:
                out[x * 4 + 3] = reinterpret_cast<const std::uint8_t*>(pixels.row(y))[x * 4 + 3];
                break;
            case PixelFormat::Rgba16:
                out[x * 4 + 3] = static_cast<std::uint8_t>((reinterpret_cast<const std::uint16_t*>(pixels.row(y))[x * 4 + 3] + 128) / 257);
                break;
            default:
                out[x * 4 + 3] = static_cast<std::uint8_t>(std::lround(
                    std::clamp(reinterpret_cast<const float*>(pixels.row(y))[x * 4 + 3], 0.0F, 1.0F) * 255.0F));
                break;
            }
        }
    }
    return result;
}

}  // namespace

Result<ImageBuffer> makeThumbnail(const std::filesystem::path& path, int maxEdge, std::stop_token stop, const ColorTarget& target) {
    const auto source = openImageSource(path, ReadPriority::Background, stop);
    if (!source) {
        return std::unexpected(source.error());
    }
    const SubimageInfo& subimage = (*source)->subimages()[0];
    // Twice the pixels gives the kernel enough to work with, as levels that come from a file
    // can hold false detail near their own resolution.
    const int levelIndex = chooseThumbnailLevel(subimage, maxEdge > kMaxOversampledEdge ? maxEdge : maxEdge * 2);
    if (tooExpensive(subimage, levelIndex)) {
        return std::unexpected("image has no reduced versions and is too large for a thumbnail");
    }

    try {
        const auto level = readLevel(**source, 0, levelIndex, stop);
        if (!level) {
            return level;
        }
        const double scale = std::min(1.0, static_cast<double>(maxEdge) / std::max(level->width(), level->height()));
        const Size size{std::max(1, static_cast<int>(std::lround(level->width() * scale))),
                        std::max(1, static_cast<int>(std::lround(level->height() * scale)))};
        // A thumbnail shows transparency as mid gray, so that its cache keeps opaque pictures.
        const ImageBuffer small = resize(flattened(*level), size, stop);
        if (stop.stop_requested()) {
            return std::unexpected("cancelled");
        }
        return applyOrientation(toEightBit(small, subimage, target), subimage.orientation);
    } catch (const std::bad_alloc&) {
        return std::unexpected("not enough memory for thumbnail");
    }
}

Result<ImageBuffer> makeSrgbCopy(const std::filesystem::path& path, Size size, std::stop_token stop) {
    const auto source = openImageSource(path, ReadPriority::Background, stop);
    if (!source) {
        return std::unexpected(source.error());
    }
    const SubimageInfo& subimage = (*source)->subimages()[0];
    const Size stored = displaySize({std::max(1, size.width), std::max(1, size.height)}, subimage.orientation);
    const int edge = std::max(stored.width, stored.height);
    const int levelIndex = chooseThumbnailLevel(subimage, edge > kMaxOversampledEdge ? edge : edge * 2);
    if (tooExpensive(subimage, levelIndex)) {
        return std::unexpected("image has no reduced versions and is too large to read whole");
    }
    try {
        const auto level = readLevel(**source, 0, levelIndex, stop);
        if (!level) {
            return level;
        }
        const ImageBuffer resized = level->size() == stored ? *level : resize(*level, stored, stop);
        if (stop.stop_requested()) {
            return std::unexpected("cancelled");
        }
        return applyOrientation(toEightBit(resized, subimage, ColorTarget{}), subimage.orientation);
    } catch (const std::bad_alloc&) {
        return std::unexpected("not enough memory to read the image");
    }
}

}  // namespace iv
