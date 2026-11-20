#include "core/pixel_probe.h"

#include <algorithm>
#include <format>
#include <new>

#include "core/color.h"
#include "core/image_ops.h"
#include "core/oklab.h"

namespace iv {

namespace {

bool isFloat(PixelFormat format) {
    return format == PixelFormat::Gray32F || format == PixelFormat::Rgba32F;
}

double maximumOf(PixelFormat format) {
    switch (format) {
    case PixelFormat::Bgrx8:
    case PixelFormat::Gray8: return 255;
    case PixelFormat::Gray16:
    case PixelFormat::Rgba16: return 65535;
    case PixelFormat::Gray32F:
    case PixelFormat::Rgba32F: return 1;
    }
    return 1;
}

std::array<std::uint8_t, 3> rgbOf(const ImageBuffer& bgrx) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(bgrx.data());
    return {p[2], p[1], p[0]};
}

template <typename Format>
std::string joined(const PixelProbe& probe, Format format) {
    std::string text = format(probe.values[0]);
    for (int c = 1; c < probe.channels; ++c) {
        text += ", " + format(probe.values[static_cast<std::size_t>(c)]);
    }
    return text;
}

}  // namespace

Result<PixelProbe> probePixel(ImageSource& source, int subimage, int x, int y, const std::stop_token& stop) {
    const auto subimages = source.subimages();
    if (subimage < 0 || subimage >= static_cast<int>(subimages.size())) {
        return std::unexpected("there is no such page");
    }
    const SubimageInfo& info = subimages[static_cast<std::size_t>(subimage)];
    const LevelInfo& level = info.levels.front();
    if (x < 0 || y < 0 || x >= level.size.width || y >= level.size.height) {
        return std::unexpected("outside the image");
    }
    const int col = x / level.tileSize.width;
    const int row = y / level.tileSize.height;
    const auto tile = source.readTile({subimage, 0, col, row}, stop);
    if (!tile) {
        return std::unexpected(tile.error());
    }
    // Flattened, to read what is shown, until transparency is.
    const ImageBuffer pixel = flattened(tile->view(x - col * level.tileSize.width, y - row * level.tileSize.height, 1, 1));

    PixelProbe probe;
    probe.format = pixel.format();
    const std::byte* p = pixel.data();
    switch (pixel.format()) {
    case PixelFormat::Bgrx8: {
        const auto* b = reinterpret_cast<const std::uint8_t*>(p);
        probe.values = {double(b[2]), double(b[1]), double(b[0])};
        // 8-bit gray is decoded into all three channels.
        probe.channels = info.stored.colorModel.starts_with("Grayscale") ? 1 : 3;
        break;
    }
    case PixelFormat::Gray8: {
        const double v = *reinterpret_cast<const std::uint8_t*>(p);
        probe.values = {v, v, v};
        probe.channels = 1;
        break;
    }
    case PixelFormat::Gray16: {
        const double v = *reinterpret_cast<const std::uint16_t*>(p);
        probe.values = {v, v, v};
        probe.channels = 1;
        break;
    }
    case PixelFormat::Rgba16: {
        const auto* s = reinterpret_cast<const std::uint16_t*>(p);
        probe.values = {double(s[0]), double(s[1]), double(s[2])};
        break;
    }
    case PixelFormat::Gray32F: {
        const double v = *reinterpret_cast<const float*>(p);
        probe.values = {v, v, v};
        probe.channels = 1;
        break;
    }
    case PixelFormat::Rgba32F: {
        const auto* f = reinterpret_cast<const float*>(p);
        probe.values = {double(f[0]), double(f[1]), double(f[2])};
        break;
    }
    }

    try {
        const DisplayRange range = fullRange(pixel.format());
        probe.eightBit = rgbOf(toDisplay(pixel, range));
        // Colour management on and an sRGB display: what the file's profile makes of it.
        if (const auto toSrgb = displayTransform(info, ColorTarget{true, nullptr})) {
            probe.srgb = rgbOf(toDisplay(pixel, range, toSrgb.get()));
        }
    } catch (const std::bad_alloc&) {
        return std::unexpected("not enough memory");
    }
    return probe;
}

namespace {

// A patch says nothing about the light when less than this much of it shows, its luminance is
// below this in linear light (about 6 of 255 as stored), or more than this part of it is clipped.
constexpr double kLeastShowing = 0.5;
constexpr double kDarkest = 0.002;
constexpr double kMostClipped = 0.25;
// A channel this near the top of an integer format is taken as clipped.
constexpr double kClipped = 254.0 / 255.0;

struct PatchSums {
    std::array<double, 3> rgb{};
    double weight = 0;   // of the pixels kept
    double showing = 0;  // of all, by alpha
    double clipped = 0;
    double pixels = 0;
};

// R, G, B from 0 to 1 as stored and alpha, of one pixel.
std::array<double, 4> samplesAt(const ImageBuffer& tile, int x, int y) {
    const std::byte* p = tile.row(y);
    const bool alpha = tile.hasAlpha();
    switch (tile.format()) {
    case PixelFormat::Bgrx8: {
        const auto* b = reinterpret_cast<const std::uint8_t*>(p) + static_cast<std::ptrdiff_t>(x) * 4;
        return {b[2] / 255.0, b[1] / 255.0, b[0] / 255.0, alpha ? b[3] / 255.0 : 1.0};
    }
    case PixelFormat::Rgba16: {
        const auto* s = reinterpret_cast<const std::uint16_t*>(p) + static_cast<std::ptrdiff_t>(x) * 4;
        return {s[0] / 65535.0, s[1] / 65535.0, s[2] / 65535.0, alpha ? s[3] / 65535.0 : 1.0};
    }
    case PixelFormat::Rgba32F: {
        const auto* f = reinterpret_cast<const float*>(p) + static_cast<std::ptrdiff_t>(x) * 4;
        return {f[0], f[1], f[2], alpha ? std::clamp(double(f[3]), 0.0, 1.0) : 1.0};
    }
    case PixelFormat::Gray8:
    case PixelFormat::Gray16:
    case PixelFormat::Gray32F: break;
    }
    return {0, 0, 0, 0};
}

void addTile(const ImageBuffer& tile, int x0, int y0, int x1, int y1, bool integer, PatchSums& sums) {
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const auto s = samplesAt(tile, x, y);
            sums.pixels += 1;
            sums.showing += s[3];
            if (s[3] <= 0) {
                continue;
            }
            if (integer && std::max({s[0], s[1], s[2]}) >= kClipped) {
                sums.clipped += s[3];
                continue;
            }
            for (std::size_t c = 0; c < 3; ++c) {
                sums.rgb[c] += srgbDecode(s[c]) * s[3];
            }
            sums.weight += s[3];
        }
    }
}

}  // namespace

Result<std::array<double, 3>> patchAverage(ImageSource& source, int subimage, int x, int y, int width, int height,
                                           const std::stop_token& stop) {
    const auto subimages = source.subimages();
    if (subimage < 0 || subimage >= static_cast<int>(subimages.size())) {
        return std::unexpected("there is no such page");
    }
    const SubimageInfo& info = subimages[static_cast<std::size_t>(subimage)];
    if (info.format == PixelFormat::Gray8 || info.format == PixelFormat::Gray16 || info.format == PixelFormat::Gray32F ||
        info.stored.colorModel.starts_with("Grayscale")) {
        return std::unexpected("A gray picture has no colour to balance.");
    }
    const LevelInfo& level = info.levels.front();
    const int left = std::max(x, 0);
    const int top = std::max(y, 0);
    const int right = std::min(x + width, level.size.width);
    const int bottom = std::min(y + height, level.size.height);
    if (left >= right || top >= bottom) {
        return std::unexpected("outside the image");
    }
    const bool integer = info.format != PixelFormat::Rgba32F;
    const Size tileSize = level.tileSize;
    PatchSums sums;
    for (int row = top / tileSize.height; row * tileSize.height < bottom; ++row) {
        for (int col = left / tileSize.width; col * tileSize.width < right; ++col) {
            const auto tile = source.readTile({subimage, 0, col, row}, stop);
            if (!tile) {
                return std::unexpected(tile.error());
            }
            const int tx = col * tileSize.width;
            const int ty = row * tileSize.height;
            addTile(*tile, std::max(left - tx, 0), std::max(top - ty, 0), std::min(right - tx, tile->width()),
                    std::min(bottom - ty, tile->height()), integer, sums);
        }
    }
    if (sums.showing < kLeastShowing * sums.pixels) {
        return std::unexpected("Mostly transparent: pick something that shows.");
    }
    if (sums.clipped > kMostClipped * sums.showing || sums.weight <= 0) {
        return std::unexpected("Clipped: a highlight at full brightness has lost its colour.");
    }
    const std::array<double, 3> mean{sums.rgb[0] / sums.weight, sums.rgb[1] / sums.weight, sums.rgb[2] / sums.weight};
    // By the weights of sRGB: enough to tell black, whatever the picture's primaries.
    if (0.2126 * mean[0] + 0.7152 * mean[1] + 0.0722 * mean[2] < kDarkest) {
        return std::unexpected("Too dark to tell the colour of the light.");
    }
    return mean;
}

std::string hexText(const std::array<std::uint8_t, 3>& rgb) {
    return std::format("#{:02x}{:02x}{:02x}", rgb[0], rgb[1], rgb[2]);
}

std::string valuesText(const PixelProbe& probe) {
    if (isFloat(probe.format)) {
        return joined(probe, [](double v) { return std::format("{:.4g}", v); });
    }
    return joined(probe, [](double v) { return std::format("{}", static_cast<int>(v)); });
}

std::string fractionsText(const PixelProbe& probe) {
    if (isFloat(probe.format)) {
        return valuesText(probe);
    }
    const double maximum = maximumOf(probe.format);
    const int digits = maximum > 255 ? 5 : 3;
    return joined(probe, [&](double v) { return std::format("{:.{}f}", v / maximum, digits); });
}

std::string eightBitText(const std::array<std::uint8_t, 3>& rgb) {
    return std::format("{}, {}, {}", rgb[0], rgb[1], rgb[2]);
}

}  // namespace iv
