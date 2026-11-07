#include "core/crop.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace iv {
namespace {

int nearest(double value) {
    return static_cast<int>(std::lround(value));
}

// The height of the ratio for a width, to the nearest pixel and at least one.
int heightFor(int width, AspectRatio aspect) {
    const std::int64_t twice = 2LL * width * aspect.height + aspect.width;
    return std::max(1, static_cast<int>(twice / (2LL * aspect.width)));
}

int widthFor(int height, AspectRatio aspect) {
    return heightFor(height, aspect.swapped());
}

PixelRect transposed(const PixelRect& r) {
    return {r.y, r.x, r.height, r.width};
}

Size transposed(Size s) {
    return {s.height, s.width};
}

// Which way from a fixed edge at `fixed` the rectangle goes, given where the moving edge is.
// It keeps the previous way when the two meet, and turns where the image ends.
int direction(int fixed, int moving, int previous, int extent) {
    int way = moving > fixed ? 1 : moving < fixed ? -1 : previous;
    if ((way > 0 ? extent - fixed : fixed) <= 0) {
        way = -way;
    }
    return way;
}

int room(int fixed, int way, int extent) {
    return std::max(1, way > 0 ? extent - fixed : fixed);
}

// From a fixed corner towards a moving one.
PixelRect span(int fx, int fy, int px, int py, int wayX, int wayY, Size image, AspectRatio aspect) {
    const int sx = direction(fx, px, wayX, image.width);
    const int sy = direction(fy, py, wayY, image.height);
    const int roomX = room(fx, sx, image.width);
    const int roomY = room(fy, sy, image.height);
    int w = std::max(1, std::abs(px - fx));
    int h = std::max(1, std::abs(py - fy));
    if (!aspect.isFree()) {
        // The side the pointer asks more of sets the size.
        if (static_cast<std::int64_t>(w) * aspect.height >= static_cast<std::int64_t>(h) * aspect.width) {
            h = heightFor(w, aspect);
        } else {
            w = widthFor(h, aspect);
        }
        if (w > roomX) {
            w = roomX;
            h = heightFor(w, aspect);
        }
        if (h > roomY) {
            h = roomY;
            w = widthFor(h, aspect);
        }
    }
    w = std::clamp(w, 1, roomX);
    h = std::clamp(h, 1, roomY);
    return {sx > 0 ? fx : fx - w, sy > 0 ? fy : fy - h, w, h};
}

// The left or right edge moved; the top and bottom are handled by transposing.
PixelRect dragSide(const PixelRect& r, bool right, double dx, Size image, AspectRatio aspect) {
    const int fixed = right ? r.x : r.right();
    const int moving = nearest((right ? r.right() : r.x) + dx);
    const int sx = direction(fixed, moving, right ? 1 : -1, image.width);
    const int roomX = room(fixed, sx, image.width);
    int w = std::clamp(std::abs(moving - fixed), 1, roomX);
    int h = r.height;
    int y = r.y;
    if (!aspect.isFree()) {
        h = heightFor(w, aspect);
        if (h > image.height) {
            h = image.height;
            w = std::clamp(widthFor(h, aspect), 1, roomX);
            h = std::min(heightFor(w, aspect), image.height);
        }
        // Centred across where it was.
        y = static_cast<int>(std::floor((2.0 * r.y + r.height - h) / 2 + 0.5));
        y = std::clamp(y, 0, image.height - h);
    }
    return {sx > 0 ? fixed : fixed - w, y, w, h};
}

PixelRect dragCorner(const PixelRect& r, bool left, bool top, PointD delta, Size image, AspectRatio aspect) {
    const int fx = left ? r.right() : r.x;
    const int fy = top ? r.bottom() : r.y;
    const int px = nearest((left ? r.x : r.right()) + delta.x);
    const int py = nearest((top ? r.y : r.bottom()) + delta.y);
    return span(fx, fy, px, py, left ? -1 : 1, top ? -1 : 1, image, aspect);
}

}  // namespace

CropHandle cropHandleAt(const PixelRect& rect, PointD point, double reach) {
    if (rect.width <= 0 || rect.height <= 0) {
        return CropHandle::None;
    }
    const bool withinX = point.x >= rect.x - reach && point.x <= rect.right() + reach;
    const bool withinY = point.y >= rect.y - reach && point.y <= rect.bottom() + reach;

    // Of two edges in reach, as on a small rectangle, the nearer one.
    const double toLeft = std::abs(point.x - rect.x);
    const double toRight = std::abs(point.x - rect.right());
    const double toTop = std::abs(point.y - rect.y);
    const double toBottom = std::abs(point.y - rect.bottom());
    const int horizontal = !withinY ? 0 : (toLeft <= reach || toRight <= reach) ? (toLeft <= toRight ? -1 : 1) : 0;
    const int vertical = !withinX ? 0 : (toTop <= reach || toBottom <= reach) ? (toTop <= toBottom ? -1 : 1) : 0;

    if (horizontal < 0) {
        return vertical < 0 ? CropHandle::TopLeft : vertical > 0 ? CropHandle::BottomLeft : CropHandle::Left;
    }
    if (horizontal > 0) {
        return vertical < 0 ? CropHandle::TopRight : vertical > 0 ? CropHandle::BottomRight : CropHandle::Right;
    }
    if (vertical != 0) {
        return vertical < 0 ? CropHandle::Top : CropHandle::Bottom;
    }
    const bool inside = point.x > rect.x && point.x < rect.right() && point.y > rect.y && point.y < rect.bottom();
    return inside ? CropHandle::Inside : CropHandle::None;
}

CropHandle cropHandleToward(const PixelRect& rect, PointD point) {
    const int horizontal = point.x < rect.x ? -1 : point.x > rect.right() ? 1 : 0;
    const int vertical = point.y < rect.y ? -1 : point.y > rect.bottom() ? 1 : 0;
    if (horizontal < 0) {
        return vertical < 0 ? CropHandle::TopLeft : vertical > 0 ? CropHandle::BottomLeft : CropHandle::Left;
    }
    if (horizontal > 0) {
        return vertical < 0 ? CropHandle::TopRight : vertical > 0 ? CropHandle::BottomRight : CropHandle::Right;
    }
    return vertical < 0 ? CropHandle::Top : vertical > 0 ? CropHandle::Bottom : CropHandle::Inside;
}

PixelRect dragCrop(const PixelRect& atPress, CropHandle handle, PointD delta, Size image, AspectRatio aspect) {
    switch (handle) {
    case CropHandle::None:
        return atPress;
    case CropHandle::Inside:
        return withPosition(atPress, nearest(atPress.x + delta.x), nearest(atPress.y + delta.y), image);
    case CropHandle::Left:
    case CropHandle::Right:
        return dragSide(atPress, handle == CropHandle::Right, delta.x, image, aspect);
    case CropHandle::Top:
    case CropHandle::Bottom:
        return transposed(dragSide(transposed(atPress), handle == CropHandle::Bottom, delta.y, transposed(image), aspect.swapped()));
    case CropHandle::TopLeft:     return dragCorner(atPress, true, true, delta, image, aspect);
    case CropHandle::TopRight:    return dragCorner(atPress, false, true, delta, image, aspect);
    case CropHandle::BottomLeft:  return dragCorner(atPress, true, false, delta, image, aspect);
    case CropHandle::BottomRight: return dragCorner(atPress, false, false, delta, image, aspect);
    }
    return atPress;
}

PixelRect drawCrop(PointD from, PointD to, Size image, AspectRatio aspect) {
    const int fx = std::clamp(nearest(from.x), 0, image.width);
    const int fy = std::clamp(nearest(from.y), 0, image.height);
    return span(fx, fy, nearest(to.x), nearest(to.y), 1, 1, image, aspect);
}

PixelRect fitAspect(const PixelRect& rect, AspectRatio aspect) {
    if (aspect.isFree()) {
        return rect;
    }
    int w = rect.width;
    int h = heightFor(w, aspect);
    if (h > rect.height) {
        h = rect.height;
        w = std::min(widthFor(h, aspect), rect.width);
    }
    return {rect.x + (rect.width - w) / 2, rect.y + (rect.height - h) / 2, w, h};
}

PixelRect withPosition(const PixelRect& rect, int x, int y, Size image) {
    return {std::clamp(x, 0, std::max(0, image.width - rect.width)),
            std::clamp(y, 0, std::max(0, image.height - rect.height)), rect.width, rect.height};
}

PixelRect withWidth(const PixelRect& rect, int width, Size image, AspectRatio aspect) {
    int w = std::clamp(width, 1, image.width);
    int h = rect.height;
    if (!aspect.isFree()) {
        h = heightFor(w, aspect);
        if (h > image.height) {
            h = image.height;
            w = std::clamp(widthFor(h, aspect), 1, image.width);
        }
    }
    return withPosition({rect.x, rect.y, w, h}, rect.x, rect.y, image);
}

PixelRect withHeight(const PixelRect& rect, int height, Size image, AspectRatio aspect) {
    return transposed(withWidth(transposed(rect), height, transposed(image), aspect.swapped()));
}

}  // namespace iv
