#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/image_source.h"

namespace iv {

bool isJpeg(std::span<const std::uint8_t> bytes);

// JPEG can decode directly at 1/2, 1/4 and 1/8 size, so those sizes are exposed as
// pyramid levels. The source keeps the compressed bytes.
Result<std::shared_ptr<ImageSource>> openJpegSource(std::vector<std::uint8_t> bytes);

}  // namespace iv
