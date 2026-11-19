#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/image_source.h"

namespace iv {

// Truevision TGA, read only: colour-mapped, true colour and gray, plain or run-length (types 1,
// 2, 3, 9, 10, 11), any corner as origin, with the v2 footer's word on what alpha means.
//
// TGA has no magic number, so isTga only says whether a header is plausible; the caller asks it
// only of files named .tga that no other format claimed.
bool isTga(std::span<const std::uint8_t> header);
Result<std::shared_ptr<ImageSource>> openTgaSource(std::vector<std::uint8_t> bytes);

}  // namespace iv
