#include "core/pixel_probe.h"

#include <format>
#include <new>

#include "core/color.h"
#include "core/image_ops.h"

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
