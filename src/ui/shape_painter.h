#pragma once

#include <span>

#include <QPainter>
#include <QRect>

#include "core/document.h"
#include "core/shapes.h"

namespace iv {

class OverlayStyle;

// Draws an annotation shape with a painter whose coordinates are pixels of the image, as it
// will be burned in. Two kinds depend on what is under them and cannot be drawn this way: a
// marker and a pixelated box. Previewing, they are stood in for, a marker by its colour let
// through and a box by a pattern, for while ImageView::drawPreview has nothing yet; not
// previewing, they draw nothing here. Previewing is asked for by giving the style of the
// tools, whose colours the pattern takes; without one the shape is drawn as burned in.
//
// A dark edge and a shadow are made from the shape's drawn pixels (core/coverage.h), which
// costs by the area worked on. A view says what part it shows, so that only that is, and how
// many pixels of its own one of the image is while that is less than one, so that a reduced
// picture is worked on at the size shown.
struct ShapeRaster {
    QRect clip;        // in image pixels; empty for all of the shape
    double scale = 1;  // at most 1
};
void paintShape(QPainter* painter, const Shape& shape, const OverlayStyle* preview, const ShapeRaster& raster = {});

// How far beyond its points a shape may draw, with its width, outline, shadow and arrowhead.
double shapeMargin(const Shape& shape);

// The part of a picture a shape may change.
QRect shapeArea(const Shape& shape, Size image);

// Whether what a shape gives depends on what is under it: a marker and a pixelated box.
bool dependsOnPicture(const Shape& shape);

// The shapes as one edit of a picture of the given size: the layer of markers, the layer of
// the rest, and the boxes to pixelate. The layers cover only where something is drawn.
DrawEdit renderShapes(std::span<const Shape> shapes, Size image, DisplayRange range);

}  // namespace iv
