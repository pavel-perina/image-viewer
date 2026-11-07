#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "core/image_source.h"

namespace iv {

// Repacks a JPEG as JPEG XL without loss: the DCT coefficients are coded again, more tightly,
// and with them what is needed to rebuild the original file bit for bit, metadata included.
// Before returning, this rebuilds it and fails unless every byte matches.
Result<std::vector<std::uint8_t>> jpegToJxl(std::span<const std::uint8_t> jpeg);

// Rebuilds the JPEG file that a JPEG XL file was repacked from.
Result<std::vector<std::uint8_t>> jxlToJpeg(std::span<const std::uint8_t> jxl);

// Writes name.jxl next to the JPEG, with the same modification time, as it holds the same
// picture. Fails if that file exists. Returns its path.
Result<std::filesystem::path> convertJpegFileToJxl(const std::filesystem::path& jpeg);

}  // namespace iv
