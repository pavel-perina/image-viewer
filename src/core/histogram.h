#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <stop_token>
#include <vector>

#include "core/geometry.h"
#include "core/image_source.h"

namespace iv {

// Counts of sample values. Bins are evenly spaced from low to high inclusive.
struct Histogram {
    bool gray = false;    // only channel 0 is used, because all channels were equal
    int bitsPerBin = 8;   // 8 and 16 for integer images, 0 for float where the bins are arbitrary
    double low = 0;       // the value of the first bin
    double high = 255;    // the value of the last bin
    Size measuredSize;    // the level the counts come from, which may be a reduced one
    std::array<std::vector<std::uint64_t>, 3> channels;  // red, green, blue

    int binCount() const { return static_cast<int>(channels[0].size()); }
};

// The level to measure: the smallest with at least a megapixel, which is plenty for the
// shape of a histogram, unless the next finer level would be too expensive to read.
int histogramLevel(std::span<const LevelInfo> levels);

// Reads every tile of that level, so call it from a worker thread.
Result<Histogram> computeHistogram(ImageSource& source, int subimage, const std::stop_token& stop);

// The same for pixels already in memory. All buffers must have the same format.
Histogram histogramOf(std::span<const ImageBuffer> tiles);

}  // namespace iv
