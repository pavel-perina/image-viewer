#pragma once

#include "core/geometry.h"

namespace iv {

// A rectangle of whole pixels, so that its edges lie on the pixel grid.
struct PixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    int right() const { return x + width; }
    int bottom() const { return y + height; }
    bool operator==(const PixelRect&) const = default;
};

struct PointD {
    double x = 0;
    double y = 0;
};

// Width to height, such as 3:2. Zero in either means free.
struct AspectRatio {
    int width = 0;
    int height = 0;

    bool isFree() const { return width <= 0 || height <= 0; }
    AspectRatio swapped() const { return {height, width}; }
};

enum class CropHandle { None, Inside, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };

// The handle at a point, in image pixels. Reach is how far from an edge still counts, also in
// image pixels, so that the view can keep it a fixed size on screen.
CropHandle cropHandleAt(const PixelRect& rect, PointD point, double reach);

// The edge or corner on the side of the rectangle where the point is, however far away: the
// handle a press outside the image takes, where there is nothing to draw a new rectangle on.
// Inside the rectangle it is Inside.
CropHandle cropHandleToward(const PixelRect& rect, PointD point);

// The crop after dragging a handle by delta image pixels from where it was at the press. Edges
// land on the nearest pixel boundary and stay inside the image. Dragging an edge or a corner
// past the opposite one turns the rectangle over. With a ratio, an edge sets the size and
// keeps the rectangle centred across it, and a corner keeps the opposite corner in place.
PixelRect dragCrop(const PixelRect& atPress, CropHandle handle, PointD delta, Size image, AspectRatio aspect);

// A new crop drawn from one point to another.
PixelRect drawCrop(PointD from, PointD to, Size image, AspectRatio aspect);

// The largest rectangle of the ratio inside the given one, centred in it.
PixelRect fitAspect(const PixelRect& rect, AspectRatio aspect);

// The crop with its fields set, as typed in. A size keeps the top-left corner and, with a
// ratio, sets the other side; anything is moved or shrunk to stay inside the image.
PixelRect withPosition(const PixelRect& rect, int x, int y, Size image);
PixelRect withWidth(const PixelRect& rect, int width, Size image, AspectRatio aspect);
PixelRect withHeight(const PixelRect& rect, int height, Size image, AspectRatio aspect);

}  // namespace iv
