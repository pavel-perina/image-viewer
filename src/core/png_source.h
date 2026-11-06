#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/image_source.h"

namespace iv {

bool isPng(std::span<const std::uint8_t> header);
Result<std::shared_ptr<ImageSource>> openPngSource(std::vector<std::uint8_t> bytes);

}  // namespace iv
