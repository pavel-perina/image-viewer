#include "core/canvas.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/coverage.h"
#include "core/image_layer.h"
#include "core/image_ops.h"

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

namespace {

int frameWidth(const FrameLook& look) {
    int total = 0;
    for (const FrameLook::Frame& frame : look.frames) {
        total += std::max(0, frame.width);
    }
    return total;
}

// How far the shadow's faint edge reaches left and above.
int shadowLead(const FrameLook& look) { return look.shadow > 0 ? (look.shadow + 2) / 3 : 0; }

}  // namespace

Size framedSize(Size picture, const FrameLook& look) {
    const int around = 2 * frameWidth(look) + shadowLead(look) + std::max(0, look.shadow);
    return {picture.width + around, picture.height + around};
}

PixelRect framedPicture(Size picture, const FrameLook& look) {
    const int at = shadowLead(look) + frameWidth(look);
    return {at, at, picture.width, picture.height};
}

bool framedInColour(const FrameLook& look) {
    if (look.shadow > 0 && look.transparent) {
        return true;
    }
    return std::ranges::any_of(look.frames, [](const FrameLook::Frame& frame) {
        return frame.width > 0 && (frame.colour.r != frame.colour.g || frame.colour.g != frame.colour.b);
    });
}

bool framedWithAlpha(const FrameLook& look) { return look.shadow > 0 && look.transparent; }

ImageBuffer framed(const ImageBuffer& picture, const FrameLook& look) {
    ImageBuffer inside = flattened(picture, 1.0);
    if (framedInColour(look)) {
        inside = toColour(inside);
    }
    const Size size = framedSize(inside.size(), look);
    const PixelRect hole = framedPicture(inside.size(), look);
    if (size == inside.size()) {
        return inside;
    }

    // The frames, as one opaque picture with the hole in the middle, which composeCanvas puts
    // over its shadow on the background.
    const int lead = shadowLead(look);
    const Size outer{size.width - lead - std::max(0, look.shadow), size.height - lead - std::max(0, look.shadow)};
    const ImageBuffer frames = ImageBuffer::create(outer, PixelFormat::Bgrx8);
    for (int y = 0; y < outer.height; ++y) {
        auto* out = reinterpret_cast<std::uint8_t*>(frames.row(y));
        for (int x = 0; x < outer.width; ++x, out += 4) {
            // Which frame the pixel is in: by how far it is from the nearest edge.
            const int from = std::min({x, y, outer.width - 1 - x, outer.height - 1 - y});
            Rgba8 colour{0, 0, 0, 255};
            int edge = 0;
            for (const FrameLook::Frame& frame : look.frames) {
                edge += std::max(0, frame.width);
                if (from < edge) {
                    colour = frame.colour;
                    break;
                }
            }
            out[0] = colour.b;
            out[1] = colour.g;
            out[2] = colour.r;
            out[3] = 255;
        }
    }
    CanvasLook canvasLook;
    canvasLook.shadow = look.shadow;
    if (look.transparent) {
        canvasLook.background.reset();
    }
    const std::array placed{PlacedPicture{frames, lead, lead}};
    const ImageBuffer surround = composeCanvas(size, placed, canvasLook);

    // As a layer, clear where the picture goes, laid over the picture in its own format.
    ImageBuffer layer = premultiplied(surround);
    for (int y = hole.y; y < hole.y + hole.height; ++y) {
        std::memset(reinterpret_cast<std::uint8_t*>(layer.row(y)) + static_cast<std::ptrdiff_t>(hole.x) * 4, 0,
                    static_cast<std::size_t>(hole.width) * 4);
    }
    const ImageBuffer result = ImageBuffer::create(size, inside.format()).withAlpha(framedWithAlpha(look));
    const auto pixelBytes = static_cast<std::size_t>(bytesPerPixel(inside.format()));
    for (int y = 0; y < size.height; ++y) {
        std::memset(result.row(y), 0, static_cast<std::size_t>(size.width) * pixelBytes);
    }
    for (int y = 0; y < hole.height; ++y) {
        auto* out = reinterpret_cast<std::uint8_t*>(result.row(hole.y + y)) + static_cast<std::size_t>(hole.x) * pixelBytes;
        std::memcpy(out, inside.row(y), static_cast<std::size_t>(hole.width) * pixelBytes);
    }
    // Opaque, as pixels without alpha are kept too; with alpha, clear around the picture, for
    // the background and shadow to fill.
    if (!isGray(result.format())) {
        for (int y = 0; y < size.height; ++y) {
            for (int x = 0; x < size.width; ++x) {
                const bool shown = !result.hasAlpha() || (x >= hole.x && x < hole.x + hole.width && y >= hole.y && y < hole.y + hole.height);
                switch (result.format()) {
                case PixelFormat::Bgrx8: reinterpret_cast<std::uint8_t*>(result.row(y))[x * 4 + 3] = shown ? 255 : 0; break;
                case PixelFormat::Rgba16: reinterpret_cast<std::uint16_t*>(result.row(y))[x * 4 + 3] = shown ? 65535 : 0; break;
                default: reinterpret_cast<float*>(result.row(y))[x * 4 + 3] = shown ? 1.0F : 0.0F; break;
                }
            }
        }
    }
    blendLayerInto(result, layer, 0, 0, LayerBlend::Over, fullRange(result.format()));
    return result;
}

}  // namespace iv
