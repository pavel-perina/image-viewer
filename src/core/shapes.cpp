#include "core/shapes.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace iv {

namespace {

bool isBox(ShapeKind kind) {
    return snapsToPixels(kind);
}

// The corners of a box around two opposite ones, the first corner first and the second third.
std::vector<PointD> corners(PointD a, PointD b) {
    return {a, {b.x, a.y}, b, {a.x, b.y}};
}

std::vector<PointD> corners(const RectD& r) {
    return corners({r.x, r.y}, {r.x + r.width, r.y + r.height});
}

bool inside(const RectD& r, PointD p) {
    return p.x >= r.x && p.y >= r.y && p.x <= r.x + r.width && p.y <= r.y + r.height;
}

// Even-odd, so it holds for a polygon that crosses itself too.
bool inside(std::span<const PointD> polygon, PointD p) {
    bool in = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const PointD a = polygon[i];
        const PointD b = polygon[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
            in = !in;
        }
    }
    return in;
}

double distanceToPath(std::span<const PointD> points, PointD p, bool closed) {
    if (points.empty()) {
        return std::numeric_limits<double>::infinity();
    }
    double nearest = distanceToSegment(p, points.front(), points.front());
    for (std::size_t i = 1; i < points.size(); ++i) {
        nearest = std::min(nearest, distanceToSegment(p, points[i - 1], points[i]));
    }
    if (closed && points.size() > 2) {
        nearest = std::min(nearest, distanceToSegment(p, points.back(), points.front()));
    }
    return nearest;
}

bool hits(const Shape& shape, PointD at, double reach) {
    if (shape.points.empty()) {
        return false;
    }
    const double near = std::max(reach, shape.style.width / 2);
    const bool solid = shape.style.filled || shape.style.marker;
    switch (shape.kind) {
    case ShapeKind::Line:
    case ShapeKind::Arrow:
    case ShapeKind::Freehand:
        return distanceToPath(shape.points, at, false) <= near;
    case ShapeKind::Pixelate:
    case ShapeKind::Text:
        return inside(bounds(shape), at);
    case ShapeKind::Rectangle: {
        const RectD box = bounds(shape);
        return (solid && inside(box, at)) || distanceToPath(corners(box), at, true) <= near;
    }
    case ShapeKind::Polygon:
        return (solid && inside(shape.points, at)) || distanceToPath(shape.points, at, true) <= near;
    }
    return false;
}

}  // namespace

bool snapsToPixels(ShapeKind kind) {
    return kind == ShapeKind::Rectangle || kind == ShapeKind::Pixelate || kind == ShapeKind::Text;
}

Shape startShape(ShapeKind kind, const ShapeStyle& style, PointD at) {
    const bool grows = kind == ShapeKind::Polygon || kind == ShapeKind::Freehand;
    return {kind, grows ? std::vector<PointD>{at} : std::vector<PointD>{at, at}, style, {}};
}

std::vector<PointD> grips(const Shape& shape) {
    if (shape.points.empty()) {
        return {};
    }
    if (shape.kind == ShapeKind::Freehand) {
        return corners(bounds(shape));
    }
    if (isBox(shape.kind) && shape.points.size() == 2) {
        return corners(shape.points[0], shape.points[1]);
    }
    return shape.points;
}

void moveGrip(Shape& shape, int grip, PointD to) {
    const std::vector<PointD> at = grips(shape);
    if (grip < 0 || grip >= static_cast<int>(at.size())) {
        return;
    }
    if (shape.kind == ShapeKind::Freehand) {
        // Stretched from the corner opposite, which stays; a side of no length cannot be.
        const PointD from = at[static_cast<std::size_t>(grip)];
        const PointD fixed = at[static_cast<std::size_t>((grip + 2) % 4)];
        const double sx = from.x != fixed.x ? (to.x - fixed.x) / (from.x - fixed.x) : 1;
        const double sy = from.y != fixed.y ? (to.y - fixed.y) / (from.y - fixed.y) : 1;
        for (PointD& p : shape.points) {
            p = {fixed.x + (p.x - fixed.x) * sx, fixed.y + (p.y - fixed.y) * sy};
        }
        return;
    }
    if (isBox(shape.kind) && shape.points.size() == 2) {
        PointD& a = shape.points[0];
        PointD& b = shape.points[1];
        switch (grip) {
        case 0: a = to; break;
        case 1: b.x = to.x; a.y = to.y; break;
        case 2: b = to; break;
        case 3: a.x = to.x; b.y = to.y; break;
        }
        return;
    }
    shape.points[static_cast<std::size_t>(grip)] = to;
}

void moveBy(Shape& shape, PointD delta) {
    for (PointD& p : shape.points) {
        p = {p.x + delta.x, p.y + delta.y};
    }
}

RectD bounds(const Shape& shape) {
    if (shape.points.empty()) {
        return {};
    }
    PointD low = shape.points.front();
    PointD high = low;
    for (const PointD& p : shape.points) {
        low = {std::min(low.x, p.x), std::min(low.y, p.y)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y)};
    }
    return {low.x, low.y, high.x - low.x, high.y - low.y};
}

ShapeHit shapeAt(std::span<const Shape> shapes, PointD at, double reach, int selected) {
    if (selected >= 0 && selected < static_cast<int>(shapes.size())) {
        const std::vector<PointD> points = grips(shapes[static_cast<std::size_t>(selected)]);
        ShapeHit best;
        double nearest = reach;
        for (int i = 0; i < static_cast<int>(points.size()); ++i) {
            const PointD p = points[static_cast<std::size_t>(i)];
            const double d = std::hypot(at.x - p.x, at.y - p.y);
            if (d <= nearest) {
                nearest = d;
                best = {selected, i};
            }
        }
        if (best.shape >= 0) {
            return best;
        }
    }
    for (int i = static_cast<int>(shapes.size()) - 1; i >= 0; --i) {
        if (hits(shapes[static_cast<std::size_t>(i)], at, reach)) {
            return {i, -1};
        }
    }
    return {};
}

}  // namespace iv
