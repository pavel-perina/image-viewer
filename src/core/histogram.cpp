#include "core/histogram.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iv {

namespace {

constexpr std::uint64_t kEnoughPixels = 1'000'000;
constexpr std::uint64_t kMostPixels = 16'000'000;
constexpr int kFloatBins = 4096;

std::uint64_t pixelsOf(const LevelInfo& level) {
    return static_cast<std::uint64_t>(level.size.width) * level.size.height;
}

void resizeAll(Histogram& histogram, int bins) {
    for (auto& channel : histogram.channels) {
        channel.assign(static_cast<std::size_t>(bins), 0);
    }
}

// Keeps channel 0 only when the picture turned out to have no colour.
void finish(Histogram& histogram, bool anyColor) {
    histogram.gray = !anyColor;
    if (histogram.gray) {
        histogram.channels[1].clear();
        histogram.channels[2].clear();
    }
}

void countIntegers(std::span<const ImageBuffer> tiles, Histogram& histogram) {
    const PixelFormat format = tiles.front().format();
    const bool is8 = format == PixelFormat::Bgrx8 || format == PixelFormat::Gray8;
    histogram.bitsPerBin = is8 ? 8 : 16;
    histogram.low = 0;
    histogram.high = is8 ? 255 : 65535;
    resizeAll(histogram, is8 ? 256 : 65536);

    auto* red = histogram.channels[0].data();
    auto* green = histogram.channels[1].data();
    auto* blue = histogram.channels[2].data();
    unsigned different = 0;
    for (const ImageBuffer& tile : tiles) {
        for (int y = 0; y < tile.height(); ++y) {
            const std::byte* row = tile.row(y);
            const int width = tile.width();
            if (format == PixelFormat::Gray8) {
                const auto* in = reinterpret_cast<const std::uint8_t*>(row);
                for (int x = 0; x < width; ++x) {
                    ++red[in[x]];
                }
            } else if (is8) {
                const auto* in = reinterpret_cast<const std::uint8_t*>(row);
                for (int x = 0; x < width; ++x, in += 4) {
                    ++blue[in[0]];
                    ++green[in[1]];
                    ++red[in[2]];
                    different |= static_cast<unsigned>(in[0] ^ in[1]) | static_cast<unsigned>(in[1] ^ in[2]);
                }
            } else if (format == PixelFormat::Rgba16) {
                const auto* in = reinterpret_cast<const std::uint16_t*>(row);
                for (int x = 0; x < width; ++x, in += 4) {
                    ++red[in[0]];
                    ++green[in[1]];
                    ++blue[in[2]];
                    different |= static_cast<unsigned>(in[0] ^ in[1]) | static_cast<unsigned>(in[1] ^ in[2]);
                }
            } else {
                const auto* in = reinterpret_cast<const std::uint16_t*>(row);
                for (int x = 0; x < width; ++x) {
                    ++red[in[x]];
                }
            }
        }
    }
    finish(histogram, different != 0);
}

void countFloats(std::span<const ImageBuffer> tiles, Histogram& histogram) {
    const int step = tiles.front().format() == PixelFormat::Gray32F ? 1 : 4;
    const int used = step == 1 ? 1 : 3;

    // The bins have to cover whatever the data holds, so the range comes first.
    float low = std::numeric_limits<float>::infinity();
    float high = -std::numeric_limits<float>::infinity();
    for (const ImageBuffer& tile : tiles) {
        for (int y = 0; y < tile.height(); ++y) {
            const auto* in = reinterpret_cast<const float*>(tile.row(y));
            for (int x = 0; x < tile.width(); ++x, in += step) {
                for (int c = 0; c < used; ++c) {
                    if (std::isfinite(in[c])) {
                        low = std::min(low, in[c]);
                        high = std::max(high, in[c]);
                    }
                }
            }
        }
    }
    if (low > high) {  // nothing finite
        low = 0;
        high = 1;
    } else if (low == high) {
        high = low + 1;
    }

    histogram.bitsPerBin = 0;
    histogram.low = low;
    histogram.high = high;
    resizeAll(histogram, kFloatBins);
    const double scale = (kFloatBins - 1) / (static_cast<double>(high) - low);
    bool different = false;
    for (const ImageBuffer& tile : tiles) {
        for (int y = 0; y < tile.height(); ++y) {
            const auto* in = reinterpret_cast<const float*>(tile.row(y));
            for (int x = 0; x < tile.width(); ++x, in += step) {
                for (int c = 0; c < used; ++c) {
                    if (std::isfinite(in[c])) {
                        const double position = (in[c] - low) * scale + 0.5;
                        ++histogram.channels[static_cast<std::size_t>(c)][static_cast<std::size_t>(position)];
                    }
                }
                different = different || (used == 3 && (in[0] != in[1] || in[1] != in[2]));
            }
        }
    }
    finish(histogram, different);
}

}  // namespace

int histogramLevel(std::span<const LevelInfo> levels) {
    int chosen = static_cast<int>(levels.size()) - 1;
    for (int i = chosen; i >= 0; --i) {
        const std::uint64_t pixels = pixelsOf(levels[static_cast<std::size_t>(i)]);
        if (pixels > kMostPixels) {
            break;
        }
        chosen = i;
        if (pixels >= kEnoughPixels) {
            break;
        }
    }
    return chosen;
}

Histogram histogramOf(std::span<const ImageBuffer> tiles) {
    Histogram histogram;
    if (tiles.empty()) {
        return histogram;
    }
    const PixelFormat format = tiles.front().format();
    if (format == PixelFormat::Gray32F || format == PixelFormat::Rgba32F) {
        countFloats(tiles, histogram);
    } else {
        countIntegers(tiles, histogram);
    }
    return histogram;
}

Result<Histogram> computeHistogram(ImageSource& source, int subimage, const std::stop_token& stop) {
    const auto subimages = source.subimages();
    if (subimage < 0 || subimage >= std::ssize(subimages)) {
        return std::unexpected("page does not exist");
    }
    const auto& levels = subimages[static_cast<std::size_t>(subimage)].levels;
    const int levelIndex = histogramLevel(levels);
    const LevelInfo& level = levels[static_cast<std::size_t>(levelIndex)];
    const int cols = (level.size.width + level.tileSize.width - 1) / level.tileSize.width;
    const int rows = (level.size.height + level.tileSize.height - 1) / level.tileSize.height;

    // Holding the tiles is bounded by kMostPixels, unless even the coarsest level is larger.
    std::vector<ImageBuffer> tiles;
    tiles.reserve(static_cast<std::size_t>(cols) * rows);
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            auto tile = source.readTile({subimage, levelIndex, col, row}, stop);
            if (tile) {
                tiles.push_back(std::move(*tile));
            } else if (tile.error() == "cancelled") {
                return std::unexpected(tile.error());
            }
            // A damaged tile is left out, so the rest still gives the shape.
        }
    }
    if (tiles.empty()) {
        return std::unexpected("cannot read image");
    }
    Histogram histogram = histogramOf(tiles);
    histogram.measuredSize = level.size;
    return histogram;
}

}  // namespace iv
