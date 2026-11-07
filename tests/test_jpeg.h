#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace iv::test {

struct Rgb {
    std::uint8_t r, g, b;
};

struct JpegOptions {
    int width = 64;
    int height = 64;
    int orientation = 0;  // EXIF value; 0 writes no EXIF block
    std::vector<std::uint8_t> exif;  // a complete APP1 payload, instead of orientation
    std::vector<std::uint8_t> iccProfile;
    bool progressive = false;
    std::function<Rgb(int x, int y)> pixel = [](int, int) { return Rgb{128, 128, 128}; };
};

std::vector<std::uint8_t> encodeJpeg(const JpegOptions& options);

struct DecodedJpeg {
    int width = 0;
    int height = 0;
    std::vector<Rgb> pixels;          // row by row, as stored, whatever the EXIF orientation
    std::vector<std::uint8_t> exif;   // the APP1 payload starting with "Exif", if any
    std::vector<std::uint8_t> iccProfile;
    bool progressive = false;

    Rgb at(int x, int y) const { return pixels[static_cast<std::size_t>(y) * width + x]; }
};

// Fails the current test and returns nothing if the bytes are not a valid JPEG.
DecodedJpeg decodeJpeg(const std::vector<std::uint8_t>& bytes);
void writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes);

}  // namespace iv::test
