#include "core/canvas.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/coverage.h"
#include "core/image_layer.h"

namespace iv {

namespace {

// A layer as blendLayer takes it: colours multiplied by alpha.
ImageBuffer premultiplied(const ImageBuffer& picture) {
    const ImageBuffer layer = ImageBuffer::create(picture.size(), PixelFormat::Bgrx8);
    for (int y = 0; y < picture.height(); ++y) {
        const auto* in = reinterpret_cast<const std::uint8_t*>(picture.row(y));
        auto* out = reinterpret_cast<std::uint8_t*>(layer.row(y));
        for (int x = 0; x < picture.width(); ++x, in += 4, out += 4) {
            const int a = picture.hasAlpha() ? in[3] : 255;
            for (int c = 0; c < 3; ++c) {
                out[c] = static_cast<std::uint8_t>((in[c] * a + 127) / 255);
            }
            out[3] = static_cast<std::uint8_t>(a);
        }
    }
    return layer;
}

// What the pictures cover on the canvas, from 0 to 1.
Coverage coverageOf(Size size, std::span<const PlacedPicture> pictures) {
    Coverage covered(size.width, size.height);
    for (const PlacedPicture& picture : pictures) {
        const int firstX = std::max(0, picture.x);
        const int firstY = std::max(0, picture.y);
        const int endX = std::min(size.width, picture.x + picture.pixels.width());
        const int endY = std::min(size.height, picture.y + picture.pixels.height());
        for (int y = firstY; y < endY; ++y) {
            const auto* in = reinterpret_cast<const std::uint8_t*>(picture.pixels.row(y - picture.y));
            for (int x = firstX; x < endX; ++x) {
                const float a = picture.pixels.hasAlpha() ? in[(x - picture.x) * 4 + 3] / 255.0F : 1.0F;
                covered.at(x, y) = std::max(covered.at(x, y), a);
            }
        }
    }
    return covered;
}

}  // namespace

ImageBuffer composeCanvas(Size size, std::span<const PlacedPicture> pictures, const CanvasLook& look) {
    const ImageBuffer canvas = ImageBuffer::create(size, PixelFormat::Bgrx8).withAlpha(!look.background);
    const Rgba8 fill = look.background.value_or(Rgba8{0, 0, 0, 0});
    for (int y = 0; y < size.height; ++y) {
        auto* out = reinterpret_cast<std::uint8_t*>(canvas.row(y));
        for (int x = 0; x < size.width; ++x, out += 4) {
            out[0] = fill.b;
            out[1] = fill.g;
            out[2] = fill.r;
            out[3] = look.background ? 255 : 0;
        }
    }
    const DisplayRange range = fullRange(PixelFormat::Bgrx8);
    if (look.shadow > 0 && look.shadowStrength > 0) {
        const double offset = look.shadow / 3.0;
        const Coverage shade = blurred(shifted(coverageOf(size, pictures), offset, offset), look.shadow * 2.0 / 9.0);
        // Black, so its colours multiplied by alpha are nought.
        const ImageBuffer layer = ImageBuffer::create(size, PixelFormat::Bgrx8);
        const double strength = std::clamp(look.shadowStrength, 0.0, 1.0);
        for (int y = 0; y < size.height; ++y) {
            auto* out = reinterpret_cast<std::uint8_t*>(layer.row(y));
            for (int x = 0; x < size.width; ++x, out += 4) {
                out[0] = out[1] = out[2] = 0;
                out[3] = static_cast<std::uint8_t>(std::lround(std::clamp(static_cast<double>(shade.at(x, y)), 0.0, 1.0) * strength * 255));
            }
        }
        blendLayerInto(canvas, layer, 0, 0, LayerBlend::Over, range);
    }
    for (const PlacedPicture& picture : pictures) {
        blendLayerInto(canvas, premultiplied(picture.pixels), picture.x, picture.y, LayerBlend::Over, range);
    }
    return canvas;
}

}  // namespace iv
