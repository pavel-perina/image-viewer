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
    std::function<Rgb(int x, int y)> pixel = [](int, int) { return Rgb{128, 128, 128}; };
};

std::vector<std::uint8_t> encodeJpeg(const JpegOptions& options);
void writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes);

}  // namespace iv::test
