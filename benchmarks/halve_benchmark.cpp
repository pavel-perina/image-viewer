// Making the reduced levels of an image, at the size of a 25 MP photo.
//
// The synthetic image has smooth shapes over several scales and per-pixel grain, so that it
// costs about what a photo does. Set IV_BENCHMARK_IMAGE to a file to measure a real one too.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

#include "core/image_ops.h"
#include "core/image_source.h"

namespace iv {
namespace {

constexpr Size kPhotoSize{5776, 4336};

// Value noise: random values on a grid, smoothly interpolated between its points.
class ValueNoise {
public:
    ValueNoise(int cells, std::mt19937& random) : m_cells(cells), m_values(static_cast<std::size_t>((cells + 1) * (cells + 1))) {
        std::uniform_real_distribution<float> value(-1.0F, 1.0F);
        for (float& v : m_values) {
            v = value(random);
        }
    }

    // At u and v from 0 to 1.
    float at(float u, float v) const {
        const float x = u * static_cast<float>(m_cells);
        const float y = v * static_cast<float>(m_cells);
        const int x0 = std::min(static_cast<int>(x), m_cells - 1);
        const int y0 = std::min(static_cast<int>(y), m_cells - 1);
        const float fx = smooth(x - static_cast<float>(x0));
        const float fy = smooth(y - static_cast<float>(y0));
        const auto value = [&](int cx, int cy) { return m_values[static_cast<std::size_t>(cy * (m_cells + 1) + cx)]; };
        const float top = value(x0, y0) + (value(x0 + 1, y0) - value(x0, y0)) * fx;
        const float bottom = value(x0, y0 + 1) + (value(x0 + 1, y0 + 1) - value(x0, y0 + 1)) * fx;
        return top + (bottom - top) * fy;
    }

private:
    static float smooth(float t) { return t * t * (3 - 2 * t); }

    int m_cells;
    std::vector<float> m_values;
};

// Made once, on first use, as it takes longer than what is measured.
const ImageBuffer& photoLike(PixelFormat format) {
    static const std::vector<ImageBuffer> images = [] {
        std::mt19937 random(2026);
        std::vector<ValueNoise> octaves;
        for (const int cells : {4, 16, 64, 256}) {
            for (int channel = 0; channel < 3; ++channel) {
                octaves.emplace_back(cells, random);
            }
        }
        std::normal_distribution<float> grain(0.0F, 0.02F);
        std::vector<float> rgb(static_cast<std::size_t>(kPhotoSize.width) * kPhotoSize.height * 3);
        for (int y = 0; y < kPhotoSize.height; ++y) {
            for (int x = 0; x < kPhotoSize.width; ++x) {
                const float u = static_cast<float>(x) / kPhotoSize.width;
                const float v = static_cast<float>(y) / kPhotoSize.height;
                for (int c = 0; c < 3; ++c) {
                    float value = 0.45F;
                    float amplitude = 0.3F;
                    for (std::size_t o = 0; o < 4; ++o) {
                        value += amplitude * octaves[o * 3 + static_cast<std::size_t>(c)].at(u, v);
                        amplitude *= 0.5F;
                    }
                    rgb[(static_cast<std::size_t>(y) * kPhotoSize.width + x) * 3 + static_cast<std::size_t>(c)] =
                        std::clamp(value + grain(random), 0.0F, 1.0F);
                }
            }
        }
        std::vector<ImageBuffer> made;
        for (const PixelFormat f : {PixelFormat::Bgrx8, PixelFormat::Rgba16, PixelFormat::Rgba32F}) {
            ImageBuffer image = ImageBuffer::create(kPhotoSize, f);
            for (int y = 0; y < kPhotoSize.height; ++y) {
                const float* in = rgb.data() + static_cast<std::size_t>(y) * kPhotoSize.width * 3;
                for (int x = 0; x < kPhotoSize.width; ++x, in += 3) {
                    if (f == PixelFormat::Bgrx8) {
                        auto* out = reinterpret_cast<std::uint8_t*>(image.row(y)) + static_cast<std::size_t>(x) * 4;
                        out[0] = static_cast<std::uint8_t>(std::lround(in[2] * 255));
                        out[1] = static_cast<std::uint8_t>(std::lround(in[1] * 255));
                        out[2] = static_cast<std::uint8_t>(std::lround(in[0] * 255));
                        out[3] = 255;
                    } else if (f == PixelFormat::Rgba16) {
                        auto* out = reinterpret_cast<std::uint16_t*>(image.row(y)) + static_cast<std::size_t>(x) * 4;
                        for (int c = 0; c < 3; ++c) {
                            out[c] = static_cast<std::uint16_t>(std::lround(in[c] * 65535));
                        }
                        out[3] = 65535;
                    } else {
                        auto* out = reinterpret_cast<float*>(image.row(y)) + static_cast<std::size_t>(x) * 4;
                        std::copy(in, in + 3, out);
                        out[3] = 1.0F;
                    }
                }
            }
            made.push_back(std::move(image));
        }
        return made;
    }();
    switch (format) {
    case PixelFormat::Rgba16:  return images[1];
    case PixelFormat::Rgba32F: return images[2];
    default:                   return images[0];
    }
}

// The first halving reads the whole image, so it is the one that counts.
void halveOnce(benchmark::State& state, PixelFormat format) {
    const ImageBuffer& image = photoLike(format);
    for (auto _ : state) {
        benchmark::DoNotOptimize(halve(image));
    }
    state.SetItemsProcessed(state.iterations() * kPhotoSize.width * kPhotoSize.height);
}

// What a viewer builds for fit on a typical screen: 1/2 and then 1/4.
void halveTwice(benchmark::State& state) {
    const ImageBuffer& image = photoLike(PixelFormat::Bgrx8);
    for (auto _ : state) {
        benchmark::DoNotOptimize(halve(halve(image)));
    }
}

// A real file: its full decode, then the halving, as for any format without stored levels.
void realImage(benchmark::State& state, bool decodeOnly) {
    const char* path = std::getenv("IV_BENCHMARK_IMAGE");
    if (!path) {
        state.SkipWithMessage("set IV_BENCHMARK_IMAGE to a file to measure it");
        return;
    }
    for (auto _ : state) {
        // Opened each time, as a source keeps what it has decoded.
        auto source = openImageSource(path);
        if (!source) {
            state.SkipWithError(source.error());
            return;
        }
        auto pixels = readLevel(**source, 0, 0, {});
        if (!pixels) {
            state.SkipWithError(pixels.error());
            return;
        }
        if (!decodeOnly) {
            benchmark::DoNotOptimize(halve(halve(*pixels)));
        }
    }
}

// The 1/4 level as the viewer gets it: halved twice from the full decode, or read from the file
// where it stores one.
void realQuarter(benchmark::State& state) {
    const char* path = std::getenv("IV_BENCHMARK_IMAGE");
    if (!path) {
        state.SkipWithMessage("set IV_BENCHMARK_IMAGE to a file to measure it");
        return;
    }
    for (auto _ : state) {
        auto source = openImageSource(path);  // each time, as a source keeps what it has decoded
        if (!source) {
            state.SkipWithError(source.error());
            return;
        }
        const std::span<const SubimageInfo> subimages = (*source)->subimages();
        const auto& levels = subimages.front().levels;
        if (levels.size() < 3) {
            state.SkipWithError("the image has no 1/4 level");
            return;
        }
        benchmark::DoNotOptimize(readLevel(**source, 0, 2, {}));
    }
}

BENCHMARK_CAPTURE(halveOnce, bgrx8, PixelFormat::Bgrx8)->Unit(benchmark::kMillisecond)->UseRealTime();
BENCHMARK_CAPTURE(halveOnce, rgba16, PixelFormat::Rgba16)->Unit(benchmark::kMillisecond)->UseRealTime();
BENCHMARK_CAPTURE(halveOnce, rgba32f, PixelFormat::Rgba32F)->Unit(benchmark::kMillisecond)->UseRealTime();
BENCHMARK(halveTwice)->Unit(benchmark::kMillisecond)->UseRealTime();
BENCHMARK_CAPTURE(realImage, decode, true)->Unit(benchmark::kMillisecond)->UseRealTime();
BENCHMARK_CAPTURE(realImage, decodeAndHalveTwice, false)->Unit(benchmark::kMillisecond)->UseRealTime();
BENCHMARK(realQuarter)->Unit(benchmark::kMillisecond)->UseRealTime();

}  // namespace
}  // namespace iv
