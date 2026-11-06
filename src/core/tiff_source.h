#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

#include "core/image_source.h"

namespace iv {

bool isTiff(std::span<const std::uint8_t> header);

// Classic TIFF and BigTIFF: pages as subimages, and reduced resolution images, in the main
// directory chain or in SubIFDs, as pyramid levels. Pixels stay in the file until asked for.
Result<std::shared_ptr<ImageSource>> openTiffSource(const std::filesystem::path& path);

}  // namespace iv
