#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

#include "test_jpeg.h"

namespace iv::test {

// One image directory in a test file. The pixel function returns 8-bit values; other bit
// depths scale them up, so colours can be compared across formats.
struct TiffImage {
    int width = 64;
    int height = 64;
    int bits = 8;
    int samples = 3;  // 1 for grayscale
    int tileSize = 0;  // 0 writes strips
    int rowsPerStrip = 16;
    int photometric = -1;  // -1 chooses from samples; PHOTOMETRIC_MINISWHITE is 0
    int planar = 1;       // PLANARCONFIG_CONTIG
    int sampleFormat = 1;  // SAMPLEFORMAT_UINT
    int compression = 1;   // COMPRESSION_NONE
    int orientation = 1;
    bool reduced = false;  // mark as a reduced resolution image
    std::vector<std::uint8_t> iccProfile{};
    std::function<Rgb(int x, int y)> pixel = [](int, int) { return Rgb{128, 128, 128}; };
    // For 16-bit unsigned files, each sample at full precision instead of pixel.
    std::function<std::uint16_t(int x, int y, int sample)> wide16{};
    std::vector<TiffImage> subIfds{};  // written as SubIFDs of this image
};

void writeTiff(const std::filesystem::path& path, const std::vector<TiffImage>& images, bool bigTiff = false);

}  // namespace iv::test
