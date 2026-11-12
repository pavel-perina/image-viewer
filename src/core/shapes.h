#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/geometry.h"

namespace iv {

// What can be drawn on an image as an annotation. Every kind is a list of points in pixels
// of the image, so that moving, selecting and removing are written once; a kind decides only
// what its points mean, where it can be grabbed and how it is drawn.
//
//   Line, Arrow     two ends; an arrow points at the second
//   Rectangle       two opposite corners
//   Pixelate        the same, and coarsens what is under it rather than covering it
//   Text            the same, a box the text is laid out in
//   Polygon         its corners in order, closed
//   Freehand        the samples of a stroke
enum class ShapeKind { Line, Arrow, Rectangle, Pixelate, Text, Polygon, Freehand };

// As sRGB, not premultiplied.
struct ShapeColour {
    std::uint8_t r = 255;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
    bool operator==(const ShapeColour&) const = default;
};

// Lengths are in image pixels, so a shape looks the same at any zoom and in the file saved.
struct ShapeStyle {
    ShapeColour colour;
    double width = 2;      // of lines and edges
    bool filled = false;   // rectangles and polygons
    bool marker = false;   // darkens what is under it, as a highlighter does, rather than covering
    bool outline = false;  // a dark edge around it, to show on any picture
    bool shadow = false;
    double textSize = 16;
    bool operator==(const ShapeStyle&) const = default;
};

struct Shape {
    ShapeKind kind = ShapeKind::Line;
    std::vector<PointD> points;
    ShapeStyle style;
    std::string text;
};

// A shape while it is being drawn from a press at one point: both of its points there, or
// for a polygon and a stroke the first.
Shape startShape(ShapeKind kind, const ShapeStyle& style, PointD at);

// Where a shape can be grabbed to change it. Not always its points: a rectangle has four
// corners from its two, and a stroke the four corners of the box around it.
std::vector<PointD> grips(const Shape& shape);

// Moves a grip to a point, changing the points as the kind requires: a rectangle's derived
// corner takes x from one point and y from the other, and a stroke is stretched from the
// corner opposite. This is where a kind keeps its own rules, such as lines that stay parallel.
void moveGrip(Shape& shape, int grip, PointD to);

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
// filled or marker shape, a pixelated box and a text box anywhere inside, an outline along
// its edges.
ShapeHit shapeAt(std::span<const Shape> shapes, PointD at, double reach, int selected = -1);

}  // namespace iv
