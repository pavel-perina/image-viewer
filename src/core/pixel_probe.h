#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>

#include "core/image_source.h"

namespace iv {

// One pixel of a subimage at full size, as the file stores it and as colours to copy.
struct PixelProbe {
    PixelFormat format = PixelFormat::Bgrx8;
    int channels = 3;  // 1 for gray, which repeats its value in all three below
    // As stored: 0 to 255, 0 to 65535, or floats, which may lie outside 0 to 1.
    std::array<double, 3> values{};
    // The same reduced to 8 bits, in the file's own colours.
    std::array<std::uint8_t, 3> eightBit{};
    // Converted to sRGB through the file's colour profile, when it has one that differs.
    std::optional<std::array<std::uint8_t, 3>> srgb;
};

// The pixel at x, y of level 0, in stored coordinates, before any orientation.
Result<PixelProbe> probePixel(ImageSource& source, int subimage, int x, int y, const std::stop_token& stop);

// The mean colour of a rectangle of level 0, in stored coordinates, in linear light, for
// finding the white balance that makes it neutral. The rectangle is cut to the picture. Pixels
// count by how much of them shows, and those with a clipped channel are left out. Refused, with
// the reason, when what is covered says nothing reliable about the light: a gray picture, or
// a patch mostly transparent, nearly black or mostly clipped.
Result<std::array<double, 3>> patchAverage(ImageSource& source, int subimage, int x, int y, int width, int height,
                                           const std::stop_token& stop);

// "#405060".
std::string hexText(const std::array<std::uint8_t, 3>& rgb);

// "64, 80, 96" for 8 bits and the like for 16, "0.1235, 1.5, -0.02" for floats; one value for gray.
std::string valuesText(const PixelProbe& probe);

// The values from 0 to 1 of the format's range, "0.251, 0.314, 0.376", with more digits for
// 16 bits; floats as they are.
std::string fractionsText(const PixelProbe& probe);

// "64, 80, 96".
std::string eightBitText(const std::array<std::uint8_t, 3>& rgb);

}  // namespace iv
