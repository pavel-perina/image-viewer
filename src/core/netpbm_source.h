#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/image_source.h"

namespace iv {

// Netpbm, read only: PBM, PGM and PPM as text (P1 to P3) or binary (P4 to P6), up to 16 bits;
// PAM (P7) with gray, colour and alpha; and PFM floats (PF, Pf), taken as linear light, as the
// format is used. Only the first picture of a file holding several is read.
bool isNetpbm(std::span<const std::uint8_t> header);
Result<std::shared_ptr<ImageSource>> openNetpbmSource(std::vector<std::uint8_t> bytes);

}  // namespace iv
