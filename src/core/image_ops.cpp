#include "core/image_ops.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <thread>
#include <type_traits>
#include <vector>

#include "core/bands.h"
#include "core/color.h"
#include "core/oklab.h"

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

// For 16-bit samples, or 8-bit gray ones, which have one channel.
template <typename Sample>
DisplayRange autoRangeIntegers(std::span<const ImageBuffer> samples) {
    constexpr int kMax = std::numeric_limits<Sample>::max();
    std::vector<std::uint64_t> histogram(kMax + 1, 0);
    std::uint64_t total = 0;
    for (const ImageBuffer& image : samples) {
        const bool gray = image.format() == PixelFormat::Gray16 || image.format() == PixelFormat::Gray8;
        if (gray) {
            addToHistogram16<Sample, 1, 1>(image, histogram);
        } else {
            addToHistogram16<Sample, 4, 3>(image, histogram);
        }
        total += static_cast<std::uint64_t>(image.width()) * image.height() * (gray ? 1 : 3);
    }
    if (total == 0) {
        return {0, kMax};
    }

    const std::uint64_t skip = total / 1000;
    std::uint64_t seen = 0;
    int low = 0;
    for (int i = 0; i <= kMax; ++i) {
        seen += histogram[static_cast<std::size_t>(i)];
        if (seen > skip) {
            low = i;
            break;
        }
    }
    seen = 0;
    int high = kMax;
    for (int i = kMax; i >= 0; --i) {
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

// From samples of up to 16 bits to 8-bit values on screen.
std::vector<std::uint8_t> makeLut(DisplayRange range, int count) {
    std::vector<std::uint8_t> lut(static_cast<std::size_t>(count));
    const double scale = 255.0 / std::max(range.high - range.low, 1e-9);
    for (int i = 0; i < count; ++i) {
        lut[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(std::clamp((i - range.low) * scale + 0.5, 0.0, 255.0));
    }
    return lut;
}

std::vector<std::uint16_t> makeWideLut(DisplayRange range, int count) {
    std::vector<std::uint16_t> lut(static_cast<std::size_t>(count));
    const double scale = 65535.0 / std::max(range.high - range.low, 1e-9);
    for (int i = 0; i < count; ++i) {
        lut[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(std::clamp((i - range.low) * scale + 0.5, 0.0, 65535.0));
    }
    return lut;
}

std::uint16_t mapFloatWide(float value, double low, double scale) {
    const double mapped = (value - low) * scale + 0.5;  // NaN maps to black, as below
    return mapped >= 0 ? static_cast<std::uint16_t>(std::min(mapped, 65535.0)) : std::uint16_t{0};
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
    case PixelFormat::Gray8:   shrinkTyped<std::uint8_t, 1>(block, blockX, blockY, fullSize, target); break;
    case PixelFormat::Gray16:  shrinkTyped<std::uint16_t, 1>(block, blockX, blockY, fullSize, target); break;
    case PixelFormat::Rgba16:  shrinkTyped<std::uint16_t, 4>(block, blockX, blockY, fullSize, target); break;
    case PixelFormat::Gray32F: shrinkTyped<float, 1>(block, blockX, blockY, fullSize, target); break;
    case PixelFormat::Rgba32F: shrinkTyped<float, 4>(block, blockX, blockY, fullSize, target); break;
    }
}

ImageBuffer flattened(const ImageBuffer& source) {
    if (!source.hasAlpha()) {
        return source;
    }
    const ImageBuffer image = ImageBuffer::create(source.size(), source.format());
    const int width = image.width();
    const auto rowBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(bytesPerPixel(source.format()));
    for (int y = 0; y < image.height(); ++y) {
        std::memcpy(image.row(y), source.row(y), rowBytes);
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
    return image;
}

ImageBuffer shrink(const ImageBuffer& source, Size size) {
    if (source.size() == size) {
        return source;
    }
    const ImageBuffer target = ImageBuffer::create(size, source.format());
    shrinkBlockInto(source, 0, 0, source.size(), target);
    return target;
}

namespace {

// Encoding by table needs enough steps not to lose any value that decoding produced, which a
// test checks for all of them.
constexpr int kEncodeSteps = 4095;

const std::array<float, 256>& srgbToLinearTable() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> values{};
        for (int i = 0; i < 256; ++i) {
            const double c = i / 255.0;
            values[static_cast<std::size_t>(i)] =
                static_cast<float>(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
        }
        return values;
    }();
    return table;
}

const std::array<std::uint8_t, kEncodeSteps + 1>& linearToSrgbTable() {
    static const std::array<std::uint8_t, kEncodeSteps + 1> table = [] {
        std::array<std::uint8_t, kEncodeSteps + 1> values{};
        for (int i = 0; i <= kEncodeSteps; ++i) {
            const double c = static_cast<double>(i) / kEncodeSteps;
            const double encoded = c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055;
            values[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(std::lround(encoded * 255));
        }
        return values;
    }();
    return table;
}

// One channel of a row as floats that can be mixed, and back.
template <typename Sample>
struct PlainSamples {
    static float decode(Sample value, int) { return static_cast<float>(value); }
    static Sample encode(float value, int) {
        if constexpr (std::is_floating_point_v<Sample>) {
            return value;
        } else {
            // The range matters to a kernel that can overshoot.
            return static_cast<Sample>(std::clamp(value, 0.0F, static_cast<float>(std::numeric_limits<Sample>::max())) + 0.5F);
        }
    }
};

// The channels are B, G, R and the unused one, which is not light.
struct DisplaySamples {
    static float decode(std::uint8_t value, int channel) {
        return channel < 3 ? srgbToLinearTable()[value] : static_cast<float>(value);
    }
    static std::uint8_t encode(float value, int channel) {
        if (channel == 3) {
            return static_cast<std::uint8_t>(std::clamp(value, 0.0F, 255.0F) + 0.5F);
        }
        return linearToSrgbTable()[static_cast<std::size_t>(std::clamp(value, 0.0F, 1.0F) * kEncodeSteps + 0.5F)];
    }
};

template <typename Sample, int Channels, typename Samples>
void halveTyped(const ImageBuffer& strip, int stripY, int fullHeight, const ImageBuffer& target, int firstRow, int endRow) {
    const int width = strip.width();
    const auto rowLength = static_cast<std::size_t>(width) * Channels;

    // Each target row uses four source rows and shares two with the next one, so the decoded
    // rows are kept in four slots, each with the row it holds.
    std::array<std::vector<float>, 4> decoded;
    std::array<int, 4> decodedRow{-1, -1, -1, -1};
    const auto rowAt = [&](int y) -> const float* {
        const int sourceY = std::clamp(y, 0, fullHeight - 1);
        const auto slot = static_cast<std::size_t>(sourceY & 3);
        if (decodedRow[slot] != sourceY) {
            decoded[slot].resize(rowLength);
            const auto* in = reinterpret_cast<const Sample*>(strip.row(sourceY - stripY));
            float* out = decoded[slot].data();
            for (int x = 0; x < width; ++x) {
                for (int c = 0; c < Channels; ++c) {
                    *out++ = Samples::decode(*in++, c);
                }
            }
            decodedRow[slot] = sourceY;
        }
        return decoded[slot].data();
    };

    std::vector<float> mixed(rowLength);
    for (int y = firstRow; y < endRow; ++y) {
        const float* r0 = rowAt(2 * y - 1);
        const float* r1 = rowAt(2 * y);
        const float* r2 = rowAt(2 * y + 1);
        const float* r3 = rowAt(2 * y + 2);
        for (std::size_t i = 0; i < rowLength; ++i) {
            mixed[i] = r0[i] + 3 * (r1[i] + r2[i]) + r3[i];
        }
        auto* out = reinterpret_cast<Sample*>(target.row(y));
        for (int x = 0; x < target.width(); ++x) {
            const int centre = 2 * x;
            const float* a = mixed.data() + static_cast<std::size_t>(std::max(centre - 1, 0)) * Channels;
            const float* b = mixed.data() + static_cast<std::size_t>(centre) * Channels;
            const float* c1 = mixed.data() + static_cast<std::size_t>(std::min(centre + 1, width - 1)) * Channels;
            const float* d = mixed.data() + static_cast<std::size_t>(std::min(centre + 2, width - 1)) * Channels;
            for (int c = 0; c < Channels; ++c) {
                *out++ = Samples::encode((a[c] + 3 * (b[c] + c1[c]) + d[c]) / 64.0F, c);
            }
        }
    }
}

// 8-bit images are halved in integers, which compilers vectorize on their own: each sample
// becomes linear light in 16 bits through a table, the 1 3 3 1 weights are summed in 32 bits (64
// times 65535 fits), and the average goes back through a table with an entry for every 16-bit
// value, finer than the float path's.
const std::array<std::uint16_t, 256>& srgbToLinear16Table() {
    static const std::array<std::uint16_t, 256> table = [] {
        std::array<std::uint16_t, 256> values{};
        for (int i = 0; i < 256; ++i) {
            values[static_cast<std::size_t>(i)] =
                static_cast<std::uint16_t>(std::lround(srgbToLinearTable()[static_cast<std::size_t>(i)] * 65535.0));
        }
        return values;
    }();
    return table;
}

const std::vector<std::uint8_t>& linear16ToSrgbTable() {
    static const std::vector<std::uint8_t> table = [] {
        std::vector<std::uint8_t> values(65536);
        for (int i = 0; i < 65536; ++i) {
            const double c = i / 65535.0;
            const double encoded = c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055;
            values[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(std::lround(encoded * 255));
        }
        return values;
    }();
    return table;
}

void halveBgrx8(const ImageBuffer& strip, int stripY, int fullHeight, const ImageBuffer& target, int firstRow, int endRow) {
    const int width = strip.width();
    const auto rowLength = static_cast<std::size_t>(width) * 4;
    const auto& toLinear = srgbToLinear16Table();
    const std::uint8_t* toSrgb = linear16ToSrgbTable().data();

    std::array<std::vector<std::uint16_t>, 4> decoded;
    std::array<int, 4> decodedRow{-1, -1, -1, -1};
    const auto rowAt = [&](int y) -> const std::uint16_t* {
        const int sourceY = std::clamp(y, 0, fullHeight - 1);
        const auto slot = static_cast<std::size_t>(sourceY & 3);
        if (decodedRow[slot] != sourceY) {
            decoded[slot].resize(rowLength);
            const auto* in = reinterpret_cast<const std::uint8_t*>(strip.row(sourceY - stripY));
            std::uint16_t* out = decoded[slot].data();
            for (std::size_t i = 0; i < rowLength; i += 4) {
                out[i] = toLinear[in[i]];
                out[i + 1] = toLinear[in[i + 1]];
                out[i + 2] = toLinear[in[i + 2]];
                out[i + 3] = static_cast<std::uint16_t>(in[i + 3] * 257);  // not light
            }
            decodedRow[slot] = sourceY;
        }
        return decoded[slot].data();
    };

    std::vector<std::uint32_t> mixed(rowLength);
    for (int y = firstRow; y < endRow; ++y) {
        const std::uint16_t* r0 = rowAt(2 * y - 1);
        const std::uint16_t* r1 = rowAt(2 * y);
        const std::uint16_t* r2 = rowAt(2 * y + 1);
        const std::uint16_t* r3 = rowAt(2 * y + 2);
        std::uint32_t* m = mixed.data();
        for (std::size_t i = 0; i < rowLength; ++i) {
            m[i] = std::uint32_t{r0[i]} + 3 * (std::uint32_t{r1[i]} + r2[i]) + r3[i];
        }
        auto* out = reinterpret_cast<std::uint8_t*>(target.row(y));
        for (int x = 0; x < target.width(); ++x) {
            const int centre = 2 * x;
            const std::uint32_t* a = m + static_cast<std::size_t>(std::max(centre - 1, 0)) * 4;
            const std::uint32_t* b = m + static_cast<std::size_t>(centre) * 4;
            const std::uint32_t* c = m + static_cast<std::size_t>(std::min(centre + 1, width - 1)) * 4;
            const std::uint32_t* d = m + static_cast<std::size_t>(std::min(centre + 2, width - 1)) * 4;
            for (int k = 0; k < 3; ++k) {
                out[k] = toSrgb[(a[k] + 3 * (b[k] + c[k]) + d[k] + 32) >> 6];
            }
            out[3] = static_cast<std::uint8_t>((((a[3] + 3 * (b[3] + c[3]) + d[3] + 32) >> 6) + 128) / 257);
            out += 4;
        }
    }
}

void halveBand(const ImageBuffer& strip, int stripY, int fullHeight, const ImageBuffer& target, int firstRow, int endRow) {
    switch (strip.format()) {
    case PixelFormat::Bgrx8:   halveBgrx8(strip, stripY, fullHeight, target, firstRow, endRow); break;
    case PixelFormat::Gray8:   halveTyped<std::uint8_t, 1, DisplaySamples>(strip, stripY, fullHeight, target, firstRow, endRow); break;
    case PixelFormat::Gray16:  halveTyped<std::uint16_t, 1, PlainSamples<std::uint16_t>>(strip, stripY, fullHeight, target, firstRow, endRow); break;
    case PixelFormat::Rgba16:  halveTyped<std::uint16_t, 4, PlainSamples<std::uint16_t>>(strip, stripY, fullHeight, target, firstRow, endRow); break;
    case PixelFormat::Gray32F: halveTyped<float, 1, PlainSamples<float>>(strip, stripY, fullHeight, target, firstRow, endRow); break;
    case PixelFormat::Rgba32F: halveTyped<float, 4, PlainSamples<float>>(strip, stripY, fullHeight, target, firstRow, endRow); break;
    }
}

double mks2013(double x) {
    x = std::abs(x);
    if (x <= 0.5) {
        return 17.0 / 16.0 - 1.75 * x * x;
    }
    if (x <= 1.5) {
        return (1.0 - x) * (1.75 - x);
    }
    if (x <= 2.5) {
        return -0.125 * (x - 2.5) * (x - 2.5);
    }
    return 0.0;
}

double magicKernel(double x) {
    x = std::abs(x);
    if (x <= 0.5) {
        return 0.75 - x * x;
    }
    if (x <= 1.5) {
        return 0.5 * (x - 1.5) * (x - 1.5);
    }
    return 0.0;
}

// The source pixels of one target pixel, in a row, and how much of each.
struct KernelSpan {
    int first = 0;
    std::vector<float> weights;
};

// Magic Kernel Sharp is the Magic Kernel followed by the weights -1/4 3/2 -1/4 between target
// pixels. When reducing, doing those as two steps reads 3 rather than 5 source pixels per unit of
// the kernel, as the second step runs on the smaller image. When enlarging the kernel has no
// room to be split, as its units are source pixels.
struct AxisFilter {
    std::vector<KernelSpan> spans;
    bool sharpenAfter = false;
};

AxisFilter axisFilter(int sourceLength, int targetLength) {
    const double ratio = static_cast<double>(sourceLength) / targetLength;
    const double spacing = std::max(ratio, 1.0);
    AxisFilter filter;
    filter.sharpenAfter = ratio > 1.0;
    const double support = filter.sharpenAfter ? 1.5 : 2.5;
    const auto kernel = filter.sharpenAfter ? magicKernel : mks2013;
    filter.spans.resize(static_cast<std::size_t>(targetLength));
    for (int target = 0; target < targetLength; ++target) {
        const double centre = (target + 0.5) * ratio;
        const int first = static_cast<int>(std::ceil(centre - 0.5 - support * spacing));
        const int last = static_cast<int>(std::floor(centre - 0.5 + support * spacing));
        KernelSpan& span = filter.spans[static_cast<std::size_t>(target)];
        span.first = std::clamp(first, 0, sourceLength - 1);
        const auto count = static_cast<std::size_t>(std::clamp(last, 0, sourceLength - 1) - span.first + 1);
        std::vector<double> weights(count, 0.0);
        double sum = 0;
        for (int i = first; i <= last; ++i) {
            const double weight = kernel((i + 0.5 - centre) / spacing);
            // The edge pixel continues outwards, as nothing lies beyond the image.
            weights[static_cast<std::size_t>(std::clamp(i, 0, sourceLength - 1) - span.first)] += weight;
            sum += weight;
        }
        span.weights.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            span.weights[i] = static_cast<float>(weights[i] / sum);
        }
    }
    return filter;
}

// Applies -1/4 3/2 -1/4 along lines of an image of floats, one line after another.
void sharpenLines(float* data, int lines, int length, std::size_t pixelStep, std::size_t lineStep, int channels) {
    std::vector<float> line(static_cast<std::size_t>(length));
    for (int l = 0; l < lines; ++l) {
        for (int c = 0; c < channels; ++c) {
            float* start = data + static_cast<std::size_t>(l) * lineStep + static_cast<std::size_t>(c);
            for (int i = 0; i < length; ++i) {
                line[static_cast<std::size_t>(i)] = start[static_cast<std::size_t>(i) * pixelStep];
            }
            for (int i = 0; i < length; ++i) {
                const float before = line[static_cast<std::size_t>(std::max(i - 1, 0))];
                const float after = line[static_cast<std::size_t>(std::min(i + 1, length - 1))];
                start[static_cast<std::size_t>(i) * pixelStep] = 1.5F * line[static_cast<std::size_t>(i)] - 0.25F * (before + after);
            }
        }
    }
}

template <typename Sample, int Channels, typename Samples>
ImageBuffer resizeTyped(const ImageBuffer& source, Size size, const std::stop_token& stop) {
    const ImageBuffer target = ImageBuffer::create(size, source.format());
    const AxisFilter across = axisFilter(source.width(), size.width);
    const AxisFilter down = axisFilter(source.height(), size.height);

    // Across first, into floats that keep linear light and any overshoot.
    const auto rowLength = static_cast<std::size_t>(size.width) * Channels;
    std::vector<float> reduced(rowLength * static_cast<std::size_t>(source.height()));
    std::vector<float> decoded(static_cast<std::size_t>(source.width()) * Channels);
    for (int y = 0; y < source.height(); ++y) {
        if (stop.stop_requested()) {
            return {};
        }
        const auto* in = reinterpret_cast<const Sample*>(source.row(y));
        for (std::size_t i = 0; i < decoded.size(); ++i) {
            decoded[i] = Samples::decode(in[i], static_cast<int>(i % Channels));
        }
        float* out = reduced.data() + rowLength * static_cast<std::size_t>(y);
        for (const KernelSpan& span : across.spans) {
            std::array<float, Channels> sum{};
            const float* from = decoded.data() + static_cast<std::size_t>(span.first) * Channels;
            for (const float weight : span.weights) {
                for (int c = 0; c < Channels; ++c) {
                    sum[static_cast<std::size_t>(c)] += weight * *from++;
                }
            }
            out = std::copy(sum.begin(), sum.end(), out);
        }
    }

    std::vector<float> result(rowLength * static_cast<std::size_t>(size.height), 0.0F);
    for (int y = 0; y < size.height; ++y) {
        if (stop.stop_requested()) {
            return {};
        }
        const KernelSpan& span = down.spans[static_cast<std::size_t>(y)];
        float* mixed = result.data() + rowLength * static_cast<std::size_t>(y);
        const float* from = reduced.data() + rowLength * static_cast<std::size_t>(span.first);
        for (const float weight : span.weights) {
            for (std::size_t i = 0; i < rowLength; ++i) {
                mixed[i] += weight * from[i];
            }
            from += rowLength;
        }
    }

    if (across.sharpenAfter) {
        sharpenLines(result.data(), size.height, size.width, Channels, rowLength, Channels);
    }
    if (down.sharpenAfter) {
        sharpenLines(result.data(), size.width, size.height, rowLength, Channels, Channels);
    }

    for (int y = 0; y < size.height; ++y) {
        auto* out = reinterpret_cast<Sample*>(target.row(y));
        const float* in = result.data() + rowLength * static_cast<std::size_t>(y);
        for (std::size_t i = 0; i < rowLength; ++i) {
            out[i] = Samples::encode(in[i], static_cast<int>(i % Channels));
        }
    }
    return target;
}

}  // namespace

ImageBuffer resize(const ImageBuffer& source, Size size, const std::stop_token& stop) {
    if (source.size() == size) {
        return source;
    }
    switch (source.format()) {
    case PixelFormat::Bgrx8:   return resizeTyped<std::uint8_t, 4, DisplaySamples>(source, size, stop);
    case PixelFormat::Gray8:   return resizeTyped<std::uint8_t, 1, DisplaySamples>(source, size, stop);
    case PixelFormat::Gray16:  return resizeTyped<std::uint16_t, 1, PlainSamples<std::uint16_t>>(source, size, stop);
    case PixelFormat::Rgba16:  return resizeTyped<std::uint16_t, 4, PlainSamples<std::uint16_t>>(source, size, stop);
    case PixelFormat::Gray32F: return resizeTyped<float, 1, PlainSamples<float>>(source, size, stop);
    case PixelFormat::Rgba32F: return resizeTyped<float, 4, PlainSamples<float>>(source, size, stop);
    }
    return {};
}

namespace {

template <typename Sample, int Channels, typename Samples>
ImageBuffer warpTyped(const ImageBuffer& source, const std::array<double, 9>& m, Size size, const std::stop_token& stop) {
    const ImageBuffer target = ImageBuffer::create(size, source.format());
    const int sw = source.width();
    const int sh = source.height();
    std::atomic<bool> stopped = false;
    inBands(size.height, size.width, [&](int firstRow, int endRow) {
        for (int y = firstRow; y < endRow; ++y) {
            if (stop.stop_requested()) {
                stopped = true;
                return;
            }
            auto* out = reinterpret_cast<Sample*>(target.row(y));
            const double ty = y + 0.5;
            for (int x = 0; x < size.width; ++x) {
                const double tx = x + 0.5;
                const double w = m[6] * tx + m[7] * ty + m[8];
                // Into coordinates where pixel i has its centre at i.
                const double fx = (m[0] * tx + m[1] * ty + m[2]) / w - 0.5;
                const double fy = (m[3] * tx + m[4] * ty + m[5]) / w - 0.5;
                const double x0 = std::floor(fx);
                const double y0 = std::floor(fy);
                const auto weights = [](double t) {
                    return std::array<float, 4>{static_cast<float>(((-t + 2) * t - 1) * t / 2),
                                                static_cast<float>(((3 * t - 5) * t * t + 2) / 2),
                                                static_cast<float>(((-3 * t + 4) * t + 1) * t / 2),
                                                static_cast<float>((t - 1) * t * t / 2)};
                };
                const auto wx = weights(fx - x0);
                const auto wy = weights(fy - y0);
                std::array<int, 4> columns{};
                for (int k = 0; k < 4; ++k) {
                    columns[static_cast<std::size_t>(k)] = std::clamp(static_cast<int>(x0) - 1 + k, 0, sw - 1);
                }
                std::array<float, Channels> sum{};
                for (int j = 0; j < 4; ++j) {
                    const int row = std::clamp(static_cast<int>(y0) - 1 + j, 0, sh - 1);
                    const auto* in = reinterpret_cast<const Sample*>(source.row(row));
                    std::array<float, Channels> across{};
                    for (int k = 0; k < 4; ++k) {
                        const Sample* pixel = in + static_cast<std::size_t>(columns[static_cast<std::size_t>(k)]) * Channels;
                        for (int c = 0; c < Channels; ++c) {
                            across[static_cast<std::size_t>(c)] += wx[static_cast<std::size_t>(k)] * Samples::decode(pixel[c], c);
                        }
                    }
                    for (int c = 0; c < Channels; ++c) {
                        sum[static_cast<std::size_t>(c)] += wy[static_cast<std::size_t>(j)] * across[static_cast<std::size_t>(c)];
                    }
                }
                for (int c = 0; c < Channels; ++c) {
                    out[c] = Samples::encode(sum[static_cast<std::size_t>(c)], c);
                }
                out += Channels;
            }
        }
    });
    return stopped ? ImageBuffer{} : target;
}

}  // namespace

ImageBuffer warp(const ImageBuffer& source, const std::array<double, 9>& targetToSource, Size size, const std::stop_token& stop) {
    switch (source.format()) {
    case PixelFormat::Bgrx8:   return warpTyped<std::uint8_t, 4, DisplaySamples>(source, targetToSource, size, stop);
    case PixelFormat::Gray8:   return warpTyped<std::uint8_t, 1, DisplaySamples>(source, targetToSource, size, stop);
    case PixelFormat::Gray16:  return warpTyped<std::uint16_t, 1, PlainSamples<std::uint16_t>>(source, targetToSource, size, stop);
    case PixelFormat::Rgba16:  return warpTyped<std::uint16_t, 4, PlainSamples<std::uint16_t>>(source, targetToSource, size, stop);
    case PixelFormat::Gray32F: return warpTyped<float, 1, PlainSamples<float>>(source, targetToSource, size, stop);
    case PixelFormat::Rgba32F: return warpTyped<float, 4, PlainSamples<float>>(source, targetToSource, size, stop);
    }
    return {};
}

namespace {

// Oklab lightness of linear sRGB, cubed: with a and b at zero, Oklab goes back to equal linear
// R, G and B of this value.
double oklabGray(double r, double g, double b) {
    const double lightness = linearToOklab(r, g, b).L;
    return lightness * lightness * lightness;
}

double lumaOf(double r, double g, double b) { return 0.299 * r + 0.587 * g + 0.114 * b; }

// Samples from 0 to 1 as stored, R, G, B and alpha, to gray as stored.
template <typename Sample>
void grayRow(const Sample* in, Sample* out, int width, GrayMethod method) {
    constexpr double kMax = std::is_floating_point_v<Sample> ? 1.0 : std::numeric_limits<Sample>::max();
    for (int x = 0; x < width; ++x, in += 4, out += 4) {
        const double r = in[0] / kMax;
        const double g = in[1] / kMax;
        const double b = in[2] / kMax;
        const double gray = method == GrayMethod::Luma ? lumaOf(r, g, b)
                                                       : srgbEncode(oklabGray(srgbDecode(r), srgbDecode(g), srgbDecode(b)));
        Sample value;
        if constexpr (std::is_floating_point_v<Sample>) {
            value = static_cast<Sample>(gray);
        } else {
            value = static_cast<Sample>(std::clamp(gray, 0.0, 1.0) * kMax + 0.5);
        }
        out[0] = out[1] = out[2] = value;
        out[3] = in[3];
    }
}

// B, G, R and the unused byte, by integer weights or through the tables of the sRGB curve.
void grayRow8(const std::uint8_t* in, std::uint8_t* out, int width, GrayMethod method) {
    const auto& toLinear = srgbToLinearTable();
    const auto& toSrgb = linearToSrgbTable();
    for (int x = 0; x < width; ++x, in += 4, out += 4) {
        std::uint8_t value = 0;
        if (method == GrayMethod::Luma) {
            value = static_cast<std::uint8_t>((299 * in[2] + 587 * in[1] + 114 * in[0] + 500) / 1000);
        } else {
            const double gray = oklabGray(toLinear[in[2]], toLinear[in[1]], toLinear[in[0]]);
            value = toSrgb[static_cast<std::size_t>(std::clamp(gray, 0.0, 1.0) * kEncodeSteps + 0.5)];
        }
        out[0] = out[1] = out[2] = value;
        out[3] = in[3];
    }
}

}  // namespace

ImageBuffer toGray(const ImageBuffer& source, GrayMethod method, const std::stop_token& stop) {
    const PixelFormat format = source.format();
    if (format == PixelFormat::Gray8 || format == PixelFormat::Gray16 || format == PixelFormat::Gray32F) {
        return source;
    }
    const ImageBuffer target = ImageBuffer::create(source.size(), format);
    std::atomic<bool> stopped = false;
    inBands(source.height(), source.width(), [&](int firstRow, int endRow) {
        for (int y = firstRow; y < endRow; ++y) {
            if (stop.stop_requested()) {
                stopped = true;
                return;
            }
            switch (format) {
            case PixelFormat::Bgrx8:
                grayRow8(reinterpret_cast<const std::uint8_t*>(source.row(y)), reinterpret_cast<std::uint8_t*>(target.row(y)),
                         source.width(), method);
                break;
            case PixelFormat::Rgba16:
                grayRow(reinterpret_cast<const std::uint16_t*>(source.row(y)), reinterpret_cast<std::uint16_t*>(target.row(y)),
                        source.width(), method);
                break;
            case PixelFormat::Rgba32F:
                grayRow(reinterpret_cast<const float*>(source.row(y)), reinterpret_cast<float*>(target.row(y)), source.width(),
                        method);
                break;
            case PixelFormat::Gray8:
            case PixelFormat::Gray16:
            case PixelFormat::Gray32F:
                break;
            }
        }
    });
    return stopped ? ImageBuffer{} : target;
}

Size halvedSize(Size size) { return {std::max(1, (size.width + 1) / 2), std::max(1, (size.height + 1) / 2)}; }

void halveRows(const ImageBuffer& strip, int stripY, int fullHeight, const ImageBuffer& target, int firstRow, int endRow) {
    // Bands of target rows read the source only, so they run side by side. Below about a
    // megapixel of output, starting threads costs more than it saves.
    constexpr std::uint64_t kPixelsPerThread = 256 * 1024;
    const int rows = endRow - firstRow;
    const std::uint64_t pixels = static_cast<std::uint64_t>(std::max(rows, 0)) * static_cast<std::uint64_t>(target.width());
    const auto wanted = static_cast<int>(std::min<std::uint64_t>(pixels / kPixelsPerThread, 8));
    const int bands = std::clamp(std::min(wanted, static_cast<int>(std::thread::hardware_concurrency())), 1, std::max(rows, 1));
    if (bands <= 1) {
        halveBand(strip, stripY, fullHeight, target, firstRow, endRow);
        return;
    }
    std::vector<std::exception_ptr> errors(static_cast<std::size_t>(bands));
    {
        std::vector<std::jthread> workers;
        workers.reserve(static_cast<std::size_t>(bands - 1));
        const auto band = [&](int index) {
            try {
                halveBand(strip, stripY, fullHeight, target, firstRow + rows * index / bands, firstRow + rows * (index + 1) / bands);
            } catch (...) {
                errors[static_cast<std::size_t>(index)] = std::current_exception();
            }
        };
        for (int i = 1; i < bands; ++i) {
            workers.emplace_back(band, i);
        }
        band(0);
    }
    for (const std::exception_ptr& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }
}

ImageBuffer halve(const ImageBuffer& source) {
    const ImageBuffer target = ImageBuffer::create(halvedSize(source.size()), source.format());
    halveRows(source, 0, source.height(), target, 0, target.height());
    return target;
}

DisplayRange fullRange(PixelFormat format) {
    switch (format) {
    case PixelFormat::Bgrx8:
    case PixelFormat::Gray8:   return {0, 255};
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
    case PixelFormat::Gray8:
        return autoRangeIntegers<std::uint8_t>(samples);
    case PixelFormat::Gray16:
    case PixelFormat::Rgba16:
        return autoRangeIntegers<std::uint16_t>(samples);
    case PixelFormat::Gray32F:
    case PixelFormat::Rgba32F:
        return autoRangeFloat(samples);
    }
    return {0, 1};
}

bool usesAutoRangeByDefault(PixelFormat format) {
    // 16-bit images have a natural range, and stretching a photograph would falsify its
    // exposure. Floats have none, so 0 to 1 may show nothing at all.
    return format == PixelFormat::Gray32F || format == PixelFormat::Rgba32F;
}

ImageBuffer toDisplay(const ImageBuffer& source, DisplayRange range, const ColorTransform* transform) {
    if (source.hasAlpha()) {
        return toDisplay(flattened(source), range, transform);
    }
    if (source.format() == PixelFormat::Gray8) {
        // Spread to the screen's layout, through the range, and then as 8-bit pixels are.
        const auto lut = makeLut(range, 256);
        const ImageBuffer target = ImageBuffer::create(source.size(), PixelFormat::Bgrx8);
        for (int y = 0; y < source.height(); ++y) {
            const auto* in = reinterpret_cast<const std::uint8_t*>(source.row(y));
            auto* out = reinterpret_cast<std::uint8_t*>(target.row(y));
            for (int x = 0; x < source.width(); ++x, out += 4) {
                out[0] = out[1] = out[2] = lut[in[x]];
                out[3] = 255;
            }
        }
        return transform ? transform->apply(target) : target;
    }
    if (transform) {
        const PixelFormat format = source.format();
        if (format == PixelFormat::Bgrx8 || (format != PixelFormat::Gray16 && format != PixelFormat::Gray32F && range == fullRange(format))) {
            return transform->apply(source);
        }
        if (format == PixelFormat::Rgba16) {
            return transform->apply(toDisplay(source, range));
        }
    }
    if (source.format() == PixelFormat::Bgrx8) {
        return source;
    }
    const ImageBuffer target = ImageBuffer::create(source.size(), PixelFormat::Bgrx8);
    const int width = source.width();

    if (source.format() == PixelFormat::Gray16 || source.format() == PixelFormat::Rgba16) {
        const auto lut = makeLut(range, 65536);
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

ImageBuffer toDisplayWide(const ImageBuffer& source, DisplayRange range, const ColorTransform* transform) {
    if (source.hasAlpha()) {
        return toDisplayWide(flattened(source), range, transform);
    }
    const PixelFormat format = source.format();
    if (format == PixelFormat::Bgrx8 || format == PixelFormat::Gray8) {
        return toDisplay(source, range, transform);  // nothing to keep beyond 8 bits
    }
    const ImageBuffer target = ImageBuffer::create(source.size(), PixelFormat::Rgba16);
    const int width = source.width();
    const auto put = [](std::uint16_t* out, std::uint16_t r, std::uint16_t g, std::uint16_t b) {
        out[0] = r;
        out[1] = g;
        out[2] = b;
        out[3] = 65535;
    };
    if (format == PixelFormat::Gray32F || format == PixelFormat::Rgba32F) {
        const double scale = 65535.0 / std::max(range.high - range.low, 1e-30);
        const bool gray = format == PixelFormat::Gray32F;
        for (int y = 0; y < source.height(); ++y) {
            const auto* in = reinterpret_cast<const float*>(source.row(y));
            auto* out = reinterpret_cast<std::uint16_t*>(target.row(y));
            for (int x = 0; x < width; ++x, out += 4) {
                if (gray) {
                    const std::uint16_t v = mapFloatWide(in[x], range.low, scale);
                    put(out, v, v, v);
                } else {
                    const float* p = in + 4 * x;
                    put(out, mapFloatWide(p[0], range.low, scale), mapFloatWide(p[1], range.low, scale), mapFloatWide(p[2], range.low, scale));
                }
            }
        }
    } else {
        const auto lut = makeWideLut(range, 65536);
        for (int y = 0; y < source.height(); ++y) {
            const auto* in = reinterpret_cast<const std::uint16_t*>(source.row(y));
            auto* out = reinterpret_cast<std::uint16_t*>(target.row(y));
            for (int x = 0; x < width; ++x, out += 4) {
                if (format == PixelFormat::Gray16) {
                    put(out, lut[in[x]], lut[in[x]], lut[in[x]]);
                } else {
                    put(out, lut[in[4 * x]], lut[in[4 * x + 1]], lut[in[4 * x + 2]]);
                }
            }
        }
    }
    // As in toDisplay: stretched floats are data rather than colours.
    const bool stretchedFloat = (format == PixelFormat::Gray32F || format == PixelFormat::Rgba32F) && range != fullRange(format);
    return transform && !stretchedFloat ? transform->applyWide(target) : target;
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
            double outX = 0;
            double outY = 0;
            transform.apply(x + 0.5, y + 0.5, outX, outY);
            std::memcpy(target.row(static_cast<int>(outY)) + static_cast<std::size_t>(outX) * pixelBytes, in, pixelBytes);
        }
    }
    return target.withAlpha(source.hasAlpha());
}

}  // namespace iv
