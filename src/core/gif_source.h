#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/image_source.h"

namespace iv {

// GIF 87a and 89a, read only. The first frame is shown on the file's logical screen, which is
// transparent where the frame does not reach, as browsers show it; an animation is marked so.
bool isGif(std::span<const std::uint8_t> header);
Result<std::shared_ptr<ImageSource>> openGifSource(std::vector<std::uint8_t> bytes);

}  // namespace iv
