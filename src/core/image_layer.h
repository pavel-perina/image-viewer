#pragma once

#include "core/crop.h"
#include "core/image_buffer.h"
#include "core/image_ops.h"

namespace iv {

// Drawing on a picture: what annotations become when they are applied. The shapes are drawn
// elsewhere, with fonts and curves, into a layer of 8-bit pixels; here the layer is put on
// pictures of any format, so that a 16-bit picture stays 16-bit.

bool isGray(PixelFormat format);

// The same picture with three colour channels, all equal: Gray8 as Bgrx8, Gray16 as Rgba16
// and Gray32F as Rgba32F. A colour picture is returned as it is.
ImageBuffer toColour(const ImageBuffer& source);

// Whether any pixel of a layer has a colour, and not a gray.
bool hasColour(const ImageBuffer& layer);

// Over covers what is under it; Multiply darkens it, as a highlighter does, so that black
// text under yellow stays black.
enum class LayerBlend { Over, Multiply };

// A copy of the picture with a layer put on it, the layer's top left corner at x, y. The
// layer is Bgrx8 with alpha in its fourth byte and the colours multiplied by it, and may
// reach beyond the picture. Its black and white become the low and high of the range, so
// that on a picture using a small part of its values, shown stretched, white is the white
// seen and not a value far above it. A gray picture stays gray, taking the layer's luma:
// use toColour first to keep colours.
ImageBuffer blendLayer(const ImageBuffer& image, const ImageBuffer& layer, int x, int y, LayerBlend blend, DisplayRange range);

// A copy of the picture with a rectangle coarsened into blocks of one colour each, the
// average of what was there, counted from the rectangle's corner. The part of the rectangle
// outside the picture is ignored.
ImageBuffer pixelate(const ImageBuffer& image, PixelRect rect, int block);

}  // namespace iv
