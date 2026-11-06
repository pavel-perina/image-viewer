#include "core/image_ops.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace iv {

namespace {

// Rounds down, in 64 bits so very large images cannot overflow.
int scaled(int value, int numerator, int denominator) {
    return static_cast<int>(static_cast<std::int64_t>(value) * numerator / denominator);
}

int scaledUp(int value, int numerator, int denominator) {
    return static_cast<int>((static_cast<std::int64_t>(value) * numerator + denominator - 1) / denominator);
}

template <typename Sample, int Channels>
void shrinkTyped(const ImageBuffer& block, int blockX, int blockY, Size full, const ImageBuffer& target) {
    using Sum = std::conditional_t<std::is_floating_point_v<Sample>, double, std::uint64_t>;

    const int firstY = scaled(blockY, target.height(), full.height);
    const int lastY = std::min(target.height(), scaledUp(blockY + block.height(), target.height(), full.height));
    const int firstX = scaled(blockX, target.width(), full.width);
    const int lastX = std::min(target.width(), scaledUp(blockX + block.width(), target.width(), full.width));
    if (firstX >= lastX) {
        return;
    }

    // The source span of a column is the same on every row, and this loop runs for every pixel
    // of images that can have hundreds of megapixels, so the divisions happen only once.
    struct Span {
        int begin;
        int end;
    };
    std::vector<Span> columns(static_cast<std::size_t>(lastX - firstX));
    for (int x = firstX; x < lastX; ++x) {
        const int left = std::max(scaled(x, full.width, target.width()), blockX);
        const int right = std::min(std::max(left + 1, scaledUp(x + 1, full.width, target.width())), blockX + block.width());
        columns[static_cast<std::size_t>(x - firstX)] = {left - blockX, right - blockX};
    }

    const auto* blockStart = reinterpret_cast<const std::uint8_t*>(block.row(0));
    const std::size_t stride = block.stride();
    int inverseOf = 0;
    double inverse = 0;
    for (int y = firstY; y < lastY; ++y) {
        const int top = std::max(scaled(y, full.height, target.height()), blockY) - blockY;
        const int bottom = std::min(std::max(top + blockY + 1, scaledUp(y + 1, full.height, target.height())) - blockY, block.height());
        auto* out = reinterpret_cast<Sample*>(target.row(y)) + static_cast<std::size_t>(firstX) * Channels;
        for (const Span& column : columns) {
            const int count = (bottom - top) * (column.end - column.begin);
            if (count > 0) {
                std::array<Sum, Channels> sum{};
                const std::uint8_t* rowStart = blockStart + static_cast<std::size_t>(top) * stride;
                for (int sy = top; sy < bottom; ++sy, rowStart += stride) {
                    const auto* in = reinterpret_cast<const Sample*>(rowStart) + static_cast<std::size_t>(column.begin) * Channels;
                    for (int sx = column.begin; sx < column.end; ++sx, in += Channels) {
                        for (int c = 0; c < Channels; ++c) {
                            sum[c] += in[c];
                        }
                    }
                }
                if (count != inverseOf) {
                    inverseOf = count;
                    inverse = 1.0 / count;
                }
                for (int c = 0; c < Channels; ++c) {
                    if constexpr (std::is_floating_point_v<Sample>) {
                        out[c] = static_cast<Sample>(sum[c] * inverse);
                    } else {
                        out[c] = static_cast<Sample>(static_cast<double>(sum[c]) * inverse + 0.5);  // rounds, so levels do not darken
                    }
                }
            }
            out += Channels;
        }
    }
}

template <typename Sample, int Channels, int UsedChannels>
void addToHistogram16(const ImageBuffer& image, std::vector<std::uint64_t>& histogram) {
    for (int y = 0; y < image.height(); ++y) {
        const auto* in = reinterpret_cast<const Sample*>(image.row(y));
        for (int x = 0; x < image.width(); ++x, in += Channels) {
            for (int c = 0; c < UsedChannels; ++c) {
                ++histogram[in[c]];
            }
        }
    }
}

DisplayRange autoRange16(std::span<const ImageBuffer> samples) {
    std::vector<std::uint64_t> histogram(65536, 0);
    std::uint64_t total = 0;
    for (const ImageBuffer& image : samples) {
        const bool gray = image.format() == PixelFormat::Gray16;
        if (gray) {
            addToHistogram16<std::uint16_t, 1, 1>(image, histogram);
        } else {
            addToHistogram16<std::uint16_t, 4, 3>(image, histogram);
        }
        total += static_cast<std::uint64_t>(image.width()) * image.height() * (gray ? 1 : 3);
    }
    if (total == 0) {
        return {0, 65535};
    }

    const std::uint64_t skip = total / 1000;
    std::uint64_t seen = 0;
    int low = 0;
    for (int i = 0; i < 65536; ++i) {
        seen += histogram[static_cast<std::size_t>(i)];
        if (seen > skip) {
            low = i;
            break;
        }
    }
    seen = 0;
    int high = 65535;
    for (int i = 65535; i >= 0; --i) {
        seen += histogram[static_cast<std::size_t>(i)];
        if (seen > skip) {
            high = i;
            break;
        }
    }
    if (high <= low) {
        // A flat image: keep its actual brightness rather than dividing by zero.
        return {0, std::max(low, 1) * 1.0};
    }
    return {static_cast<double>(low), static_cast<double>(high)};
}

template <int Channels, int UsedChannels>
void collectFloats(const ImageBuffer& image, std::vector<float>& values) {
    // A sparse sample is enough to estimate percentiles and keeps this fast.
    const int step = std::max(1, std::max(image.width(), image.height()) / 512);
    for (int y = 0; y < image.height(); y += step) {
        const auto* in = reinterpret_cast<const float*>(image.row(y));
        for (int x = 0; x < image.width(); x += step, in += static_cast<std::ptrdiff_t>(step) * Channels) {
            for (int c = 0; c < UsedChannels; ++c) {
                if (std::isfinite(in[c])) {
                    values.push_back(in[c]);
                }
            }
        }
    }
}

DisplayRange autoRangeFloat(std::span<const ImageBuffer> samples) {
    std::vector<float> values;
    for (const ImageBuffer& image : samples) {
        if (image.format() == PixelFormat::Gray32F) {
            collectFloats<1, 1>(image, values);
        } else {
            collectFloats<4, 3>(image, values);
        }
    }
    if (values.empty()) {
        return {0, 1};
    }
    const std::size_t skip = values.size() / 1000;
    const auto lowIt = values.begin() + static_cast<std::ptrdiff_t>(skip);
    std::ranges::nth_element(values, lowIt);
    const double low = *lowIt;
    const auto highIt = values.end() - 1 - static_cast<std::ptrdiff_t>(skip);
    std::ranges::nth_element(values, highIt);
    const double high = *highIt;
    if (high <= low) {
        return {low, low + 1};
    }
    return {low, high};
}

std::array<std::uint8_t, 65536> makeLut16(DisplayRange range) {
    std::array<std::uint8_t, 65536> lut;
    const double scale = 255.0 / std::max(range.high - range.low, 1e-9);
    for (int i = 0; i < 65536; ++i) {
        lut[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(std::clamp((i - range.low) * scale + 0.5, 0.0, 255.0));
    }
    return lut;
}

std::uint8_t mapFloat(float value, double low, double scale) {
    // The comparison is written so NaN maps to black.
    const double mapped = (value - low) * scale + 0.5;
    return mapped >= 0 ? static_cast<std::uint8_t>(std::min(mapped, 255.0)) : std::uint8_t{0};
}

}  // namespace

void shrinkBlockInto(const ImageBuffer& block, int blockX, int blockY, Size fullSize, const ImageBuffer& target) {
    switch (block.format()) {
    case PixelFormat::Bgrx8:   shrinkTyped<std::uint8_t, 4>(block, blockX, blockY, fullSize, target); break;
    case PixelFormat::Gray16:  shrinkTyped<std::uint16_t, 1>(block, blockX, blockY, fullSize, target); break;
    case PixelFormat::Rgba16:  shrinkTyped<std::uint16_t, 4>(block, blockX, blockY, fullSize, target); break;
    case PixelFormat::Gray32F: shrinkTyped<float, 1>(block, blockX, blockY, fullSize, target); break;
    case PixelFormat::Rgba32F: shrinkTyped<float, 4>(block, blockX, blockY, fullSize, target); break;
    }
}

void flattenAlpha(const ImageBuffer& image) {
    const int width = image.width();
    for (int y = 0; y < image.height(); ++y) {
        if (image.format() == PixelFormat::Bgrx8) {
            auto* p = reinterpret_cast<std::uint8_t*>(image.row(y));
            for (int x = 0; x < width; ++x, p += 4) {
                const unsigned alpha = p[3];
                if (alpha != 255) {
                    for (int c = 0; c < 3; ++c) {
                        p[c] = static_cast<std::uint8_t>((p[c] * alpha + 128u * (255 - alpha) + 127) / 255);
                    }
                    p[3] = 255;
                }
            }
        } else if (image.format() == PixelFormat::Rgba16) {
            auto* p = reinterpret_cast<std::uint16_t*>(image.row(y));
            for (int x = 0; x < width; ++x, p += 4) {
                const std::uint32_t alpha = p[3];
                if (alpha != 65535) {
                    for (int c = 0; c < 3; ++c) {
                        const std::uint64_t mixed = static_cast<std::uint64_t>(p[c]) * alpha + 32768ull * (65535 - alpha);
                        p[c] = static_cast<std::uint16_t>((mixed + 32767) / 65535);
                    }
                    p[3] = 65535;
                }
            }
        } else if (image.format() == PixelFormat::Rgba32F) {
            auto* p = reinterpret_cast<float*>(image.row(y));
            for (int x = 0; x < width; ++x, p += 4) {
                const float alpha = std::clamp(p[3], 0.0F, 1.0F);
                for (int c = 0; c < 3; ++c) {
                    p[c] = p[c] * alpha + 0.5F * (1 - alpha);
                }
                p[3] = 1;
            }
        }
    }
}

ImageBuffer shrink(const ImageBuffer& source, Size size) {
    if (source.size() == size) {
        return source;
    }
    const ImageBuffer target = ImageBuffer::create(size, source.format());
    shrinkBlockInto(source, 0, 0, source.size(), target);
    return target;
}

DisplayRange fullRange(PixelFormat format) {
    switch (format) {
    case PixelFormat::Bgrx8:   return {0, 255};
    case PixelFormat::Gray16:
    case PixelFormat::Rgba16:  return {0, 65535};
    case PixelFormat::Gray32F:
    case PixelFormat::Rgba32F: return {0, 1};
    }
    return {};
}

DisplayRange autoRange(std::span<const ImageBuffer> samples) {
    if (samples.empty()) {
        return {0, 1};
    }
    switch (samples.front().format()) {
    case PixelFormat::Bgrx8:
        return {0, 255};
    case PixelFormat::Gray16:
    case PixelFormat::Rgba16:
        return autoRange16(samples);
    case PixelFormat::Gray32F:
    case PixelFormat::Rgba32F:
        return autoRangeFloat(samples);
    }
    return {0, 1};
}

bool usesAutoRangeByDefault(PixelFormat format) {
    // 16-bit RGB is normally a photograph, where stretching would falsify its exposure.
    return format == PixelFormat::Gray16 || format == PixelFormat::Gray32F || format == PixelFormat::Rgba32F;
}

ImageBuffer toDisplay(const ImageBuffer& source, DisplayRange range) {
    if (source.format() == PixelFormat::Bgrx8) {
        return source;
    }
    const ImageBuffer target = ImageBuffer::create(source.size(), PixelFormat::Bgrx8);
    const int width = source.width();

    if (source.format() == PixelFormat::Gray16 || source.format() == PixelFormat::Rgba16) {
        const auto lut = makeLut16(range);
        const bool gray = source.format() == PixelFormat::Gray16;
        for (int y = 0; y < source.height(); ++y) {
            const auto* in = reinterpret_cast<const std::uint16_t*>(source.row(y));
            auto* out = reinterpret_cast<std::uint8_t*>(target.row(y));
            if (gray) {
                for (int x = 0; x < width; ++x, out += 4) {
                    out[0] = out[1] = out[2] = lut[in[x]];
                    out[3] = 255;
                }
            } else {
                for (int x = 0; x < width; ++x, in += 4, out += 4) {
                    out[0] = lut[in[2]];
                    out[1] = lut[in[1]];
                    out[2] = lut[in[0]];
                    out[3] = 255;
                }
            }
        }
        return target;
    }

    const double scale = 255.0 / std::max(range.high - range.low, 1e-30);
    const bool gray = source.format() == PixelFormat::Gray32F;
    for (int y = 0; y < source.height(); ++y) {
        const auto* in = reinterpret_cast<const float*>(source.row(y));
        auto* out = reinterpret_cast<std::uint8_t*>(target.row(y));
        if (gray) {
            for (int x = 0; x < width; ++x, out += 4) {
                out[0] = out[1] = out[2] = mapFloat(in[x], range.low, scale);
                out[3] = 255;
            }
        } else {
            for (int x = 0; x < width; ++x, in += 4, out += 4) {
                out[0] = mapFloat(in[2], range.low, scale);
                out[1] = mapFloat(in[1], range.low, scale);
                out[2] = mapFloat(in[0], range.low, scale);
                out[3] = 255;
            }
        }
    }
    return target;
}

}  // namespace iv
