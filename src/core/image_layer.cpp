#include "core/image_layer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace iv {

namespace {

int channelsOf(PixelFormat format) {
    return isGray(format) ? 1 : 4;
}

ImageBuffer copyOf(const ImageBuffer& source) {
    const ImageBuffer target = ImageBuffer::create(source.size(), source.format()).withAlpha(source.hasAlpha());
    const std::size_t rowBytes = static_cast<std::size_t>(source.width()) * static_cast<std::size_t>(bytesPerPixel(source.format()));
    for (int y = 0; y < source.height(); ++y) {
        std::memcpy(target.row(y), source.row(y), rowBytes);
    }
    return target;
}

template <typename Sample>
Sample toSample(double value) {
    if constexpr (std::numeric_limits<Sample>::is_integer) {
        return static_cast<Sample>(std::clamp(std::lround(value), 0L, static_cast<long>(std::numeric_limits<Sample>::max())));
    } else {
        return static_cast<Sample>(value);
    }
}

// Where red, green and blue are among the samples of a pixel: Bgrx8 has them reversed.
struct ChannelOrder {
    int r = 0, g = 1, b = 2;
};

template <typename Sample>
constexpr double kOpaque = std::numeric_limits<Sample>::is_integer ? static_cast<double>(std::numeric_limits<Sample>::max()) : 1.0;

// The same on a picture with alpha, where what is drawn also fills in what was transparent.
// The layer's colour is first blended with what is under it, weighed by how much of that
// shows, and then put over it: the usual compositing of a source over a backdrop. On an
// opaque pixel it gives what the opaque path does.
template <typename Sample>
void blendWithAlpha(const ImageBuffer& image, const ImageBuffer& layer, int atX, int atY, LayerBlend blend, DisplayRange range) {
    const ChannelOrder order = image.format() == PixelFormat::Bgrx8 ? ChannelOrder{2, 1, 0} : ChannelOrder{};
    const double span = range.high - range.low;
    const double scale = std::abs(span) > 1e-30 ? 1.0 / span : 0.0;
    const int firstY = std::max(0, atY);
    const int endY = std::min(image.height(), atY + layer.height());
    const int firstX = std::max(0, atX);
    const int endX = std::min(image.width(), atX + layer.width());
    for (int y = firstY; y < endY; ++y) {
        const auto* in = reinterpret_cast<const std::uint8_t*>(layer.row(y - atY)) + static_cast<std::ptrdiff_t>(firstX - atX) * 4;
        auto* out = reinterpret_cast<Sample*>(image.row(y)) + static_cast<std::ptrdiff_t>(firstX) * 4;
        for (int x = firstX; x < endX; ++x, in += 4, out += 4) {
            if (in[3] == 0) {
                continue;
            }
            const double alpha = in[3] / 255.0;
            const double under = std::clamp(static_cast<double>(out[3]) / kOpaque<Sample>, 0.0, 1.0);
            const double shown = alpha + under * (1 - alpha);
            const int channel[3] = {order.r, order.g, order.b};
            const std::uint8_t colour[3] = {in[2], in[1], in[0]};
            for (int k = 0; k < 3; ++k) {
                const double backdrop = out[channel[k]];
                // The layer's own colour, no longer multiplied by alpha, on the picture's scale.
                const double fraction = colour[k] / 255.0 / alpha;
                double source = range.low + fraction * span;
                if (blend == LayerBlend::Multiply) {
                    const double product = range.low + (backdrop - range.low) * scale * fraction * span;
                    source = (1 - under) * source + under * product;
                }
                out[channel[k]] = toSample<Sample>((alpha * source + (1 - alpha) * under * backdrop) / shown);
            }
            out[3] = toSample<Sample>(shown * kOpaque<Sample>);
        }
    }
}

template <typename Sample>
void blendTyped(const ImageBuffer& image, const ImageBuffer& layer, int atX, int atY, LayerBlend blend, DisplayRange range) {
    if (image.hasAlpha()) {
        blendWithAlpha<Sample>(image, layer, atX, atY, blend, range);
        return;
    }
    const int channels = channelsOf(image.format());
    const ChannelOrder order = image.format() == PixelFormat::Bgrx8 ? ChannelOrder{2, 1, 0} : ChannelOrder{};
    const double span = range.high - range.low;
    const int firstY = std::max(0, atY);
    const int endY = std::min(image.height(), atY + layer.height());
    const int firstX = std::max(0, atX);
    const int endX = std::min(image.width(), atX + layer.width());
    for (int y = firstY; y < endY; ++y) {
        const auto* in = reinterpret_cast<const std::uint8_t*>(layer.row(y - atY)) + static_cast<std::ptrdiff_t>(firstX - atX) * 4;
        auto* out = reinterpret_cast<Sample*>(image.row(y)) + static_cast<std::ptrdiff_t>(firstX) * channels;
        for (int x = firstX; x < endX; ++x, in += 4, out += channels) {
            if (in[3] == 0) {
                continue;
            }
            const double alpha = in[3] / 255.0;
            // Multiplied by alpha already, from 0 to alpha.
            const double b = in[0] / 255.0;
            const double g = in[1] / 255.0;
            const double r = in[2] / 255.0;
            const auto mix = [&](double under, double colour) {
                return blend == LayerBlend::Over ? under * (1 - alpha) + range.low * alpha + colour * span
                                                 : range.low + (under - range.low) * (1 - alpha + colour);
            };
            if (channels == 1) {
                out[0] = toSample<Sample>(mix(out[0], 0.299 * r + 0.587 * g + 0.114 * b));
            } else {
                out[order.r] = toSample<Sample>(mix(out[order.r], r));
                out[order.g] = toSample<Sample>(mix(out[order.g], g));
                out[order.b] = toSample<Sample>(mix(out[order.b], b));
            }
        }
    }
}

template <typename Sample>
void pixelateTyped(const ImageBuffer& image, PixelRect rect, int block) {
    const int channels = channelsOf(image.format());
    const int endX = std::min(image.width(), rect.right());
    const int endY = std::min(image.height(), rect.bottom());
    for (int blockY = rect.y; blockY < endY; blockY += block) {
        for (int blockX = rect.x; blockX < endX; blockX += block) {
            const int x0 = std::max(0, blockX);
            const int y0 = std::max(0, blockY);
            const int x1 = std::min(endX, blockX + block);
            const int y1 = std::min(endY, blockY + block);
            if (x0 >= x1 || y0 >= y1) {
                continue;
            }
            // With alpha, each colour counts by how much of it shows, so the colour stored under
            // transparency does not come into the block; alpha itself is averaged.
            const bool alpha = image.hasAlpha();
            double sum[4] = {};
            for (int y = y0; y < y1; ++y) {
                const auto* in = reinterpret_cast<const Sample*>(image.row(y)) + static_cast<std::ptrdiff_t>(x0) * channels;
                for (int x = x0; x < x1; ++x, in += channels) {
                    const double weight = alpha ? static_cast<double>(in[3]) : 1.0;
                    for (int c = 0; c < channels; ++c) {
                        sum[c] += c < 3 ? in[c] * weight : in[c];
                    }
                }
            }
            const double count = static_cast<double>(x1 - x0) * (y1 - y0);
            const double weights = alpha ? sum[3] : count;
            Sample mean[4];
            for (int c = 0; c < channels; ++c) {
                const double divisor = c < 3 ? weights : count;
                mean[c] = toSample<Sample>(divisor > 0 ? sum[c] / divisor : 0.0);
            }
            for (int y = y0; y < y1; ++y) {
                auto* out = reinterpret_cast<Sample*>(image.row(y)) + static_cast<std::ptrdiff_t>(x0) * channels;
                for (int x = x0; x < x1; ++x, out += channels) {
                    std::copy_n(mean, channels, out);
                }
            }
        }
    }
}

template <typename Sample>
void spreadGray(const ImageBuffer& source, const ImageBuffer& target, Sample opaque) {
    for (int y = 0; y < source.height(); ++y) {
        const auto* in = reinterpret_cast<const Sample*>(source.row(y));
        auto* out = reinterpret_cast<Sample*>(target.row(y));
        for (int x = 0; x < source.width(); ++x, out += 4) {
            out[0] = out[1] = out[2] = in[x];
            out[3] = opaque;
        }
    }
}

}  // namespace

bool isGray(PixelFormat format) {
    return format == PixelFormat::Gray8 || format == PixelFormat::Gray16 || format == PixelFormat::Gray32F;
}

ImageBuffer toColour(const ImageBuffer& source) {
    switch (source.format()) {
    case PixelFormat::Gray8: {
        const ImageBuffer target = ImageBuffer::create(source.size(), PixelFormat::Bgrx8);
        spreadGray<std::uint8_t>(source, target, 255);
        return target;
    }
    case PixelFormat::Gray16: {
        const ImageBuffer target = ImageBuffer::create(source.size(), PixelFormat::Rgba16);
        spreadGray<std::uint16_t>(source, target, 65535);
        return target;
    }
    case PixelFormat::Gray32F: {
        const ImageBuffer target = ImageBuffer::create(source.size(), PixelFormat::Rgba32F);
        spreadGray<float>(source, target, 1.0F);
        return target;
    }
    case PixelFormat::Bgrx8:
    case PixelFormat::Rgba16:
    case PixelFormat::Rgba32F:
        break;
    }
    return source;
}

bool hasColour(const ImageBuffer& layer) {
    for (int y = 0; y < layer.height(); ++y) {
        const auto* in = reinterpret_cast<const std::uint8_t*>(layer.row(y));
        for (int x = 0; x < layer.width(); ++x, in += 4) {
            if (in[0] != in[1] || in[1] != in[2]) {
                return true;
            }
        }
    }
    return false;
}

ImageBuffer blendLayer(const ImageBuffer& image, const ImageBuffer& layer, int x, int y, LayerBlend blend, DisplayRange range) {
    const ImageBuffer result = copyOf(image);
    switch (image.format()) {
    case PixelFormat::Bgrx8:
    case PixelFormat::Gray8: blendTyped<std::uint8_t>(result, layer, x, y, blend, range); break;
    case PixelFormat::Gray16:
    case PixelFormat::Rgba16: blendTyped<std::uint16_t>(result, layer, x, y, blend, range); break;
    case PixelFormat::Gray32F:
    case PixelFormat::Rgba32F: blendTyped<float>(result, layer, x, y, blend, range); break;
    }
    return result;
}

ImageBuffer pixelate(const ImageBuffer& image, PixelRect rect, int block) {
    const ImageBuffer result = copyOf(image);
    block = std::max(block, 1);
    switch (image.format()) {
    case PixelFormat::Bgrx8:
    case PixelFormat::Gray8: pixelateTyped<std::uint8_t>(result, rect, block); break;
    case PixelFormat::Gray16:
    case PixelFormat::Rgba16: pixelateTyped<std::uint16_t>(result, rect, block); break;
    case PixelFormat::Gray32F:
    case PixelFormat::Rgba32F: pixelateTyped<float>(result, rect, block); break;
    }
    return result;
}

}  // namespace iv
