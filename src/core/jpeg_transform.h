#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "core/image_source.h"

namespace iv {

enum class JpegEdit {
    RotateClockwise,
    RotateCounterclockwise,
    Rotate180,
    FlipHorizontal,
    FlipVertical,
    ApplyOrientation,  // stores the image the way the EXIF orientation shows it
};

// Edits a JPEG without decoding it, by moving and mirroring its DCT blocks, so nothing is lost.
// The edit applies to the image as shown, after its EXIF orientation, and the result is stored
// that way with the orientation tag reset to 1. Markers are kept, except that the EXIF
// thumbnail is unlinked, as it would show the old orientation, and the MPF index and any data
// after the image are dropped, as their offsets no longer hold.
//
// An edge that has to move must end on a whole block, 8 or 16 pixels depending on the chroma
// subsampling. If it does not, this fails, or with trimEdges cuts off the partial blocks there,
// up to 15 pixels. An empty result means there was nothing to do.
Result<std::vector<std::uint8_t>> transformJpeg(std::span<const std::uint8_t> jpeg, JpegEdit edit, bool trimEdges = false);

// The same on a file, which is replaced only once the new one is complete. Returns whether it
// changed.
Result<bool> transformJpegFile(const std::filesystem::path& path, JpegEdit edit, bool trimEdges = false);

// Whether the name ends in an extension used for JPEG files.
bool hasJpegExtension(const std::filesystem::path& path);

}  // namespace iv
