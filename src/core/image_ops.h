#pragma once

#include <stop_token>

#include <array>
#include <span>

#include "core/geometry.h"
#include "core/image_buffer.h"

namespace iv {

class ColorTransform;

// Shrinks by averaging. Works for every pixel format.
ImageBuffer shrink(const ImageBuffer& source, Size size);

// Averages one block of a larger image into a target that represents the whole image at a
// smaller size. Lets an overview be built one tile at a time, without the full image in
// memory. The target must have the block's format.
void shrinkBlockInto(const ImageBuffer& block, int blockX, int blockY, Size fullSize, const ImageBuffer& target);

// The size halveRows gives, rounding up.
Size halvedSize(Size size);

// Halves with the weights 1 3 3 1 in each direction, which keeps much less of the detail that
// the smaller image cannot hold than an average of 2x2 pixels does, and never overshoots. 8-bit
// images are mixed in linear light, assuming the sRGB curve, and other formats as they are.
ImageBuffer halve(const ImageBuffer& source);

// The same for rows [firstRow, endRow) of the target, from a strip of the source that begins
// at row stripY of fullHeight and has its full width. An image too large to hold is reduced
// this way, so the strip has to contain rows 2 * firstRow - 1 to 2 * endRow, where they exist.
void halveRows(const ImageBuffer& strip, int stripY, int fullHeight, const ImageBuffer& target, int firstRow, int endRow);

// Any size to any size with Magic Kernel Sharp 2013, stretched along when reducing. 8-bit images
// are filtered in linear light, assuming the sRGB curve, and other formats as they are. Works on
// the whole image, so use it for what has to be right, not for what has to be drawn quickly.
// A stop request makes it give up between rows and return an empty buffer.
ImageBuffer resize(const ImageBuffer& source, Size size, const std::stop_token& stop = {});

// A picture of the given size whose pixel centres are taken from the source where a projective
// transform of target pixel coordinates leads, row by row as 3 x 3, with a Catmull-Rom cubic.
// 8-bit images are mixed in linear light. Points beyond the edge repeat it. A stop request gives
// up between rows and returns an empty buffer.
ImageBuffer warp(const ImageBuffer& source, const std::array<double, 9>& targetToSource, Size size,
                 const std::stop_token& stop = {});

// How colour becomes gray. Luma weighs the samples as stored by Rec. 601, as JPEG does for its
// Y. Lightness keeps the lightness of Oklab and sets its chroma to zero, which follows how light
// colours look more closely, such as saturated blue looking dark; it assumes sRGB.
enum class GrayMethod { Luma, Lightness };

// The same picture in gray, in the same format with R, G and B equal, keeping alpha. Gray formats
// are returned as they are. A stop request gives up between rows and returns an empty buffer.
ImageBuffer toGray(const ImageBuffer& source, GrayMethod method, const std::stop_token& stop = {});

// The pixels turned and mirrored the way an orientation shows them. Normal returns the same buffer.
ImageBuffer applyOrientation(const ImageBuffer& source, Orientation orientation);

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
// image. Images that use a small part of their range look black without this.
DisplayRange autoRange(std::span<const ImageBuffer> samples);

// Whether a format looks right only after stretching to its content by default.
bool usesAutoRangeByDefault(PixelFormat format);

// Converts for display. An 8-bit image without a transform is returned as is, without copying.
// A transform applies to RGB, but not to floats that were stretched: those are data rather than
// colours, and their encoding may be linear, which 8 bits could not hold before converting.
ImageBuffer toDisplay(const ImageBuffer& source, DisplayRange range, const ColorTransform* transform = nullptr);

// The same in Rgba16, R, G, B and opaque alpha, for images with more than 8 bits, whose steps
// 8 bits would lose when they are stretched or adjusted further. 8-bit images go to toDisplay.
ImageBuffer toDisplayWide(const ImageBuffer& source, DisplayRange range, const ColorTransform* transform = nullptr);

}  // namespace iv
