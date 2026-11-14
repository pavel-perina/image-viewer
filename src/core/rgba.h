#pragma once

#include <cstdint>

namespace iv {

// A colour of 8-bit sRGB samples with alpha, not premultiplied.
struct Rgba8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
    bool operator==(const Rgba8&) const = default;
};

}  // namespace iv
