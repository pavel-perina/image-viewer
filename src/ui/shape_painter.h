#pragma once

#include <span>

#include <QPainter>
#include <QRect>

#include "core/document.h"
#include "core/shapes.h"

namespace iv {

// Draws an annotation shape with a painter whose coordinates are pixels of the image, as it
// will be burned in. Two kinds depend on what is under them and cannot be drawn this way: a
// marker and a pixelated box. Previewing, they are stood in for, a marker by its colour let
// through and a box by a pattern, for while ImageView::drawPreview has nothing yet; not
// previewing, they draw nothing here.
void paintShape(QPainter* painter, const Shape& shape, bool preview);

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
