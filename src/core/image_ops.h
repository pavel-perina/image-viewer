#pragma once

#include <span>

#include "core/image_buffer.h"

namespace iv {

// Shrinks by averaging. Works for every pixel format.
ImageBuffer shrink(const ImageBuffer& source, Size size);

// Averages one block of a larger image into a target that represents the whole image at a
// smaller size. Lets an overview be built one tile at a time, without the full image in
// memory. The target must have the block's format.
void shrinkBlockInto(const ImageBuffer& block, int blockX, int blockY, Size fullSize, const ImageBuffer& target);

// Puts a picture with alpha in its fourth channel on a mid gray background, where both dark
// and light content stay visible, and makes it opaque. Not for gray formats, which have none.
void flattenAlpha(const ImageBuffer& image);

// The sample values that map to black and white on screen.
struct DisplayRange {
    double low = 0;
    double high = 1;
    bool operator==(const DisplayRange&) const = default;
};

// The format's whole range. Floats have no natural range and use 0 to 1.
DisplayRange fullRange(PixelFormat format);

// Ignores the darkest and brightest 0.1% of samples, so a few outliers do not flatten the
// image. Scientific images often use a small part of the 16-bit range and look black without
// this.
DisplayRange autoRange(std::span<const ImageBuffer> samples);

// Whether a format looks right only after stretching to its content by default.
bool usesAutoRangeByDefault(PixelFormat format);

// Converts for display. An 8-bit image is returned as is, without copying.
ImageBuffer toDisplay(const ImageBuffer& source, DisplayRange range);

}  // namespace iv
