// The annotation shapes of core/shapes.h: where they can be grabbed, what dragging a grip does
// and what a click lands on. No window is needed: the overlay only turns mouse positions into
// image pixels and calls these. Expected values are worked out by hand from the coordinates
// given in each test.

#include <gtest/gtest.h>

#include "core/shapes.h"

using namespace iv;

namespace iv {
void PrintTo(const PointD& p, std::ostream* os) {
    *os << p.x << "," << p.y;
}
void PrintTo(const ShapeHit& h, std::ostream* os) {
    *os << "shape " << h.shape << " grip " << h.grip;
}
}  // namespace iv

namespace {

Shape shape(ShapeKind kind, std::vector<PointD> points, ShapeStyle style = {}) {
    return {kind, std::move(points), style, {}};
}

}  // namespace

// Why: drawing starts with a press and goes on by dragging the last grip. A two-point shape
// starts with both points at the press; a polygon and a stroke grow from one.
TEST(Shapes, StartAtThePress) {
    ShapeStyle style;
    style.width = 5;
    for (const ShapeKind kind : {ShapeKind::Line, ShapeKind::Arrow, ShapeKind::Rectangle, ShapeKind::Pixelate, ShapeKind::Text}) {
        const Shape s = startShape(kind, style, {10, 20});
        EXPECT_EQ(s.points, (std::vector<PointD>{{10, 20}, {10, 20}}));
        EXPECT_EQ(s.style, style);
    }
    EXPECT_EQ(startShape(ShapeKind::Polygon, style, {10, 20}).points, (std::vector<PointD>{{10, 20}}));
    EXPECT_EQ(startShape(ShapeKind::Freehand, style, {10, 20}).points, (std::vector<PointD>{{10, 20}}));
}

// Why: a line is grabbed by its ends, and moving one leaves the other.
TEST(Shapes, ALineIsGrabbedByItsEnds) {
    Shape line = shape(ShapeKind::Arrow, {{10, 10}, {50, 30}});
    EXPECT_EQ(grips(line), (std::vector<PointD>{{10, 10}, {50, 30}}));
    moveGrip(line, 1, {60, 5});
    EXPECT_EQ(line.points, (std::vector<PointD>{{10, 10}, {60, 5}}));
    moveGrip(line, 7, {0, 0});  // no such grip
    EXPECT_EQ(line.points, (std::vector<PointD>{{10, 10}, {60, 5}}));
}

// Why: a rectangle stores two corners but is grabbed by four. A stored corner moves alone;
// a derived one takes its x from one point and its y from the other, so the corner opposite
// the one dragged always stays where it is.
TEST(Shapes, ARectangleHasFourCornersFromTwoPoints) {
    Shape box = shape(ShapeKind::Rectangle, {{10, 20}, {50, 80}});
    EXPECT_EQ(grips(box), (std::vector<PointD>{{10, 20}, {50, 20}, {50, 80}, {10, 80}}));

    moveGrip(box, 1, {70, 5});  // top right; bottom left (10, 80) stays
    EXPECT_EQ(grips(box), (std::vector<PointD>{{10, 5}, {70, 5}, {70, 80}, {10, 80}}));
    moveGrip(box, 3, {0, 90});  // bottom left; top right (70, 5) stays
    EXPECT_EQ(grips(box), (std::vector<PointD>{{0, 5}, {70, 5}, {70, 90}, {0, 90}}));
    moveGrip(box, 0, {20, 20});
    moveGrip(box, 2, {40, 40});
    EXPECT_EQ(box.points, (std::vector<PointD>{{20, 20}, {40, 40}}));

    // Dragged past the opposite corner it turns over, and still has its box.
    moveGrip(box, 2, {5, 10});
    EXPECT_EQ(bounds(box), (RectD{5, 10, 15, 10}));
}

// Why: a stroke has too many points to grab one by one, so it is grabbed by the box around
// it and stretched from the opposite corner. Here the box 10..30 by 10..50 is dragged by its
// bottom right corner to twice the width and half the height, around (10, 10).
TEST(Shapes, AStrokeIsStretchedByTheBoxAroundIt) {
    Shape stroke = shape(ShapeKind::Freehand, {{10, 10}, {30, 30}, {20, 50}});
    EXPECT_EQ(grips(stroke), (std::vector<PointD>{{10, 10}, {30, 10}, {30, 50}, {10, 50}}));
    moveGrip(stroke, 2, {50, 30});
    EXPECT_EQ(stroke.points, (std::vector<PointD>{{10, 10}, {50, 20}, {30, 30}}));

    // A stroke straight up has no width to stretch; its height still follows.
    Shape upright = shape(ShapeKind::Freehand, {{10, 10}, {10, 30}});
    moveGrip(upright, 2, {40, 50});
    EXPECT_EQ(upright.points, (std::vector<PointD>{{10, 10}, {10, 50}}));
}

// Why: dragging a shape by its body moves every point by the same amount.
TEST(Shapes, MovingMovesEveryPoint) {
    Shape polygon = shape(ShapeKind::Polygon, {{0, 0}, {10, 0}, {5, 8}});
    moveBy(polygon, {3, -2});
    EXPECT_EQ(polygon.points, (std::vector<PointD>{{3, -2}, {13, -2}, {8, 6}}));
    EXPECT_EQ(bounds(polygon), (RectD{3, -2, 10, 8}));
    EXPECT_EQ(bounds(Shape{}), (RectD{}));
}

// Why: a click must land on what the user sees. A line is hit within the reach or half its
// width, whichever is more, and not beyond its ends.
TEST(Shapes, ALineIsHitAlongItsWidth) {
    ShapeStyle thick;
    thick.width = 20;
    const std::vector<Shape> shapes{shape(ShapeKind::Line, {{0, 0}, {100, 0}}), shape(ShapeKind::Line, {{0, 50}, {100, 50}}, thick)};
    EXPECT_EQ(shapeAt(shapes, {50, 3}, 4), (ShapeHit{0, -1}));
    EXPECT_EQ(shapeAt(shapes, {50, 5}, 4), (ShapeHit{}));
    EXPECT_EQ(shapeAt(shapes, {106, 0}, 4), (ShapeHit{})) << "past the end";
    EXPECT_EQ(shapeAt(shapes, {50, 59}, 4), (ShapeHit{1, -1})) << "inside the wide line";
    EXPECT_EQ(shapeAt(shapes, {50, 61}, 4), (ShapeHit{}));
}

// Why: an outline lets a click through its middle to what is under it, while a filled shape,
// a marker, a pixelated box and a text box take it anywhere inside.
TEST(Shapes, AnOutlineIsHitOnItsEdgesAndASolidShapeInside) {
    ShapeStyle filled;
    filled.filled = true;
    ShapeStyle marker;
    marker.marker = true;
    const PointD middle{50, 50};
    const std::vector<PointD> corners{{0, 0}, {100, 100}};
    const std::vector<PointD> triangle{{0, 0}, {100, 0}, {50, 100}};

    EXPECT_EQ(shapeAt(std::vector{shape(ShapeKind::Rectangle, corners)}, middle, 4), (ShapeHit{}));
    EXPECT_EQ(shapeAt(std::vector{shape(ShapeKind::Rectangle, corners)}, {2, 50}, 4), (ShapeHit{0, -1}));
    EXPECT_EQ(shapeAt(std::vector{shape(ShapeKind::Rectangle, corners, filled)}, middle, 4), (ShapeHit{0, -1}));
    EXPECT_EQ(shapeAt(std::vector{shape(ShapeKind::Rectangle, corners, marker)}, middle, 4), (ShapeHit{0, -1}));
    EXPECT_EQ(shapeAt(std::vector{shape(ShapeKind::Pixelate, corners)}, middle, 4), (ShapeHit{0, -1}));
    EXPECT_EQ(shapeAt(std::vector{shape(ShapeKind::Text, corners)}, middle, 4), (ShapeHit{0, -1}));

    EXPECT_EQ(shapeAt(std::vector{shape(ShapeKind::Polygon, triangle)}, {50, 40}, 4), (ShapeHit{}));
    EXPECT_EQ(shapeAt(std::vector{shape(ShapeKind::Polygon, triangle)}, {75, 50}, 4), (ShapeHit{0, -1})) << "on its closing side";
    EXPECT_EQ(shapeAt(std::vector{shape(ShapeKind::Polygon, triangle, filled)}, {50, 40}, 4), (ShapeHit{0, -1}));
    EXPECT_EQ(shapeAt(std::vector{shape(ShapeKind::Polygon, triangle, filled)}, {5, 60}, 4), (ShapeHit{})) << "beside it";
}

// Why: shapes overlap. The last one drawn is on top and takes the click, but the grips of
// the shape selected come first, or a shape covered by another could not be changed.
TEST(Shapes, TheTopmostShapeIsHitButTheSelectedOnesGripsComeFirst) {
    ShapeStyle filled;
    filled.filled = true;
    const std::vector<Shape> shapes{shape(ShapeKind::Rectangle, {{0, 0}, {100, 100}}, filled),
                                    shape(ShapeKind::Rectangle, {{50, 50}, {150, 150}}, filled)};
    EXPECT_EQ(shapeAt(shapes, {75, 75}, 4), (ShapeHit{1, -1}));
    EXPECT_EQ(shapeAt(shapes, {25, 25}, 4), (ShapeHit{0, -1}));
    // The lower one's corner at 100, 100 lies inside the upper one.
    EXPECT_EQ(shapeAt(shapes, {99, 101}, 4), (ShapeHit{1, -1}));
    EXPECT_EQ(shapeAt(shapes, {99, 101}, 4, 0), (ShapeHit{0, 2}));
    EXPECT_EQ(shapeAt(shapes, {75, 75}, 4, 0), (ShapeHit{1, -1})) << "away from its grips, as without a selection";
    EXPECT_EQ(shapeAt(shapes, {300, 300}, 4, 1), (ShapeHit{}));
}
