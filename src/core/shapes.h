#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/geometry.h"
#include "core/rgba.h"

namespace iv {

// What can be drawn on an image as an annotation. Every kind is a list of points in pixels
// of the image, so that moving, selecting and removing are written once; a kind decides only
// what its points mean, where it can be grabbed and how it is drawn.
//
//   Line, Arrow     two ends; an arrow points at the second
//   Rectangle       two opposite corners
//   Ellipse         the same, of the box it fills
//   Number          one point, the middle of a disc with its text in it: a step, a callout
//   Pixelate        the same, and coarsens what is under it rather than covering it
//   Text            the same, a box the text is laid out in
//   Polygon         its corners in order, closed
//   Freehand        the samples of a stroke
enum class ShapeKind { Line, Arrow, Rectangle, Pixelate, Text, Polygon, Freehand, Ellipse, Number };

// As sRGB, not premultiplied.
using ShapeColour = Rgba8;

// Lengths are in image pixels, so a shape looks the same at any zoom and in the file saved.
struct ShapeStyle {
    ShapeColour colour{255, 0, 0, 255};
    double width = 2;      // of lines and edges
    bool filled = false;   // rectangles and polygons
    bool marker = false;   // darkens what is under it, as a highlighter does, rather than covering
    bool outline = false;  // a dark edge around it, to show on any picture
    bool shadow = false;
    // How wide the dark edge is, where the shadow lies from the shape (right and down) and
    // how far it is blurred, as the sigma of a Gaussian. Unset, each follows the shape's size.
    std::optional<double> outlineWidth;
    std::optional<PointD> shadowOffset;
    std::optional<double> shadowBlur;
    double textSize = 16;    // the size of the typeface, which is taller than its capitals
    std::string fontFamily;  // empty for the application's own
    bool bold = false;
    bool operator==(const ShapeStyle&) const = default;
};

struct Shape {
    ShapeKind kind = ShapeKind::Line;
    std::vector<PointD> points;
    ShapeStyle style;
    std::string text;
    bool operator==(const Shape&) const = default;
};

// Whether a kind keeps its points on whole pixels: the boxes, whose straight edges would
// otherwise fall between pixels and blur.
bool snapsToPixels(ShapeKind kind);

// The radius of a number's disc, which follows the size of its text.
double numberRadius(const ShapeStyle& style);

// What the next number placed should say: one more than the highest among the shapes, and
// 1 for the first. Numbers whose text was changed to something else are passed over.
int nextNumber(std::span<const Shape> shapes);

// A shape while it is being drawn from a press at one point: both of its points there, or
// for a polygon, a stroke and a number the only one.
Shape startShape(ShapeKind kind, const ShapeStyle& style, PointD at);

// Where a shape can be grabbed to change it. Not always its points: a rectangle has four
// corners from its two, and a stroke the four corners of the box around it.
std::vector<PointD> grips(const Shape& shape);

// Moves a grip to a point, changing the points as the kind requires: a rectangle's derived
// corner takes x from one point and y from the other, and a stroke is stretched from the
// corner opposite. This is where a kind keeps its own rules, such as lines that stay parallel.
// With `even`, a box keeps its sides equal, the longer deciding: a square, a circle; and a
// line or an arrow keeps to steps of 15 degrees from its other end, at the length asked.
void moveGrip(Shape& shape, int grip, PointD to, bool even = false);

// Moves all of it.
void moveBy(Shape& shape, PointD delta);

// The box around its points, without the width of its lines.
RectD bounds(const Shape& shape);

struct ShapeHit {
    int shape = -1;
    int grip = -1;  // -1 for the shape itself
    bool operator==(const ShapeHit&) const = default;
};

// What is at a point: a grip of the shape selected, which alone shows them, or else the
// topmost shape, the last being on top. Reach is how far away still counts, in image pixels,
// so that the view can keep it a fixed size on screen. A line is hit along its width, a
// filled or marker shape, a pixelated box, a text box and a number's disc anywhere inside, an
// outline along its edges.
ShapeHit shapeAt(std::span<const Shape> shapes, PointD at, double reach, int selected = -1);

}  // namespace iv
