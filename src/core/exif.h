#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "core/geometry.h"

namespace iv {

// Reads the orientation from the payload of a JPEG APP1 marker ("Exif\0\0" + TIFF
// structure). The data is untrusted; anything malformed yields Orientation::Normal.
Orientation parseExifOrientation(std::span<const std::uint8_t> app1);

}  // namespace iv
