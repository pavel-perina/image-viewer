#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/image_source.h"

namespace iv {

// Windows and OS/2 bitmaps, read only: palette pictures of 1, 4 and 8 bits (plain or RLE),
// 16 and 32 bits with or without bit fields and alpha, and 24 bits; the headers of OS/2 1.x and
// Windows 3 to 5, with an embedded colour profile. BMPs that hold a JPEG or PNG are refused.
bool isBmp(std::span<const std::uint8_t> header);
Result<std::shared_ptr<ImageSource>> openBmpSource(std::vector<std::uint8_t> bytes);

}  // namespace iv
