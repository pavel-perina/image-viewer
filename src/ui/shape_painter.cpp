#include "ui/shape_painter.h"

#include <QImage>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace iv {

namespace {

const QColor kOutline(0, 0, 0, 200);
const QColor kShadow(0, 0, 0, 110);
constexpr double kMarkerPreview = 0.45;  // how much of a marker's colour shows before it is applied

QColor colourOf(const Shape& shape) {
    const ShapeColour& c = shape.style.colour;
    return {c.r, c.g, c.b, c.a};
}

QPointF point(PointD p) {
    return {p.x, p.y};
}

QRectF boxOf(const Shape& shape) {
    const RectD r = bounds(shape);
    return {r.x, r.y, r.width, r.height};
}

double outlineWidth(const Shape& shape) {
    return shape.style.outline ? std::max(1.0, std::round(shape.style.width * 0.25)) : 0;
}

double shadowOffset(const Shape& shape) {
    return shape.style.shadow ? std::max(2.0, std::round(shape.style.width * 0.6)) : 0;
}

// As long as four widths of the line and never tiny, so that it reads as an arrow.
double headLength(const Shape& shape) {
    return std::max(8.0, shape.style.width * 4);
}

bool isSolid(const Shape& shape) {
    return shape.style.filled || shape.style.marker;
}

// The shape in one colour, its lines wider by `extra`: once for the shape itself, and before
// that for its shadow and its outline.
void drawIn(QPainter* painter, const Shape& shape, const QColor& colour, double extra) {
    const double width = shape.style.width + extra;
    switch (shape.kind) {
    case ShapeKind::Line:
    case ShapeKind::Freehand: {
        QPolygonF line;
        for (const PointD& p : shape.points) {
            line << point(p);
        }
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(colour, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPolyline(line);
        break;
    }
    case ShapeKind::Arrow: {
        const QPointF from = point(shape.points.front());
        const QPointF to = point(shape.points.back());
        const QPointF along = to - from;
        const double length = std::hypot(along.x(), along.y());
        if (length <= 0) {
            break;
        }
        const QPointF unit = along / length;
        const QPointF across(-unit.y(), unit.x());
        const double head = std::min(headLength(shape), length);
        const QPointF base = to - unit * head;
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(colour, width, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(from, base);
        painter->setBrush(colour);
        painter->setPen(extra > 0 ? QPen(colour, extra, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin) : QPen(Qt::NoPen));
        painter->drawPolygon(QPolygonF{to, base + across * head * 0.4, base - across * head * 0.4});
        break;
    }
    case ShapeKind::Rectangle:
    case ShapeKind::Polygon: {
        QPainterPath path;
        if (shape.kind == ShapeKind::Rectangle) {
            // Its box is the outer edge, with the line inside it: with corners on whole pixels
            // and a whole width, every edge then lies between pixels and stays sharp.
            const QRectF box = boxOf(shape);
            const double inset = isSolid(shape) ? 0 : shape.style.width / 2;
            if (box.width() <= 2 * inset || box.height() <= 2 * inset) {
                painter->setBrush(colour);
                painter->setPen(extra > 0 ? QPen(colour, extra, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin) : QPen(Qt::NoPen));
                painter->drawRect(box);
                break;
            }
            path.addRect(box.adjusted(inset, inset, -inset, -inset));
        } else {
            QPolygonF corners;
            for (const PointD& p : shape.points) {
                corners << point(p);
            }
            path.addPolygon(corners);
            path.closeSubpath();
        }
        if (isSolid(shape)) {
            painter->setBrush(colour);
            painter->setPen(extra > 0 ? QPen(colour, extra, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin) : QPen(Qt::NoPen));
        } else {
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(colour, width, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
        }
        painter->drawPath(path);
        break;
    }
    case ShapeKind::Pixelate:
    case ShapeKind::Text:
        break;
    }
}

ImageBuffer toBuffer(const QImage& image) {
    const ImageBuffer buffer = ImageBuffer::create({image.width(), image.height()}, PixelFormat::Bgrx8);
    for (int y = 0; y < image.height(); ++y) {
        std::memcpy(buffer.row(y), image.constScanLine(y), static_cast<std::size_t>(image.width()) * 4);
    }
    return buffer;
}

}  // namespace

QRect shapeArea(const Shape& shape, Size image) {
    const double margin = shapeMargin(shape);
    return boxOf(shape).adjusted(-margin, -margin, margin, margin).toAlignedRect() & QRect(0, 0, image.width, image.height);
}

double shapeMargin(const Shape& shape) {
    const double head = shape.kind == ShapeKind::Arrow ? headLength(shape) * 0.4 : 0;
    return std::max(shape.style.width / 2, head) + outlineWidth(shape) + shadowOffset(shape) + 2;
}

void paintShape(QPainter* painter, const Shape& shape, bool preview) {
    if (shape.points.empty()) {
        return;
    }
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (shape.kind == ShapeKind::Pixelate) {
        if (preview) {
            painter->setPen(QPen(QColor(255, 255, 255, 160), 0));
            painter->setBrush(QBrush(QColor(0, 0, 0, 130), Qt::Dense4Pattern));
            painter->drawRect(boxOf(shape));
        }
        return;
    }
    if (shape.style.marker) {
        if (preview) {
            QColor colour = colourOf(shape);
            colour.setAlphaF(colour.alphaF() * static_cast<float>(kMarkerPreview));
            drawIn(painter, shape, colour, 0);
        }
        return;
    }
    if (const double offset = shadowOffset(shape); offset > 0) {
        painter->save();
        painter->translate(offset, offset);
        drawIn(painter, shape, kShadow, 2 * outlineWidth(shape));
        painter->restore();
    }
    if (const double outline = outlineWidth(shape); outline > 0) {
        drawIn(painter, shape, kOutline, 2 * outline);
    }
    drawIn(painter, shape, colourOf(shape), 0);
}

bool dependsOnPicture(const Shape& shape) {
    return shape.kind == ShapeKind::Pixelate || shape.style.marker;
}

DrawEdit renderShapes(std::span<const Shape> shapes, Size image, DisplayRange range) {
    DrawEdit edit;
    edit.range = range;
    QRectF drawn;
    bool markers = false;
    bool others = false;
    for (const Shape& shape : shapes) {
        if (shape.points.empty()) {
            continue;
        }
        if (shape.kind == ShapeKind::Pixelate) {
            const QRect box = boxOf(shape).toAlignedRect() & QRect(0, 0, image.width, image.height);
            if (!box.isEmpty()) {
                edit.pixelated.push_back({{box.x(), box.y(), box.width(), box.height()}, std::max(2, static_cast<int>(std::lround(shape.style.width)))});
            }
            continue;
        }
        const double margin = shapeMargin(shape);
        drawn |= boxOf(shape).adjusted(-margin, -margin, margin, margin);
        (shape.style.marker ? markers : others) = true;
    }
    const QRect area = drawn.toAlignedRect() & QRect(0, 0, image.width, image.height);
    if (area.isEmpty()) {
        return edit;
    }
    edit.x = area.x();
    edit.y = area.y();
    const auto layer = [&](bool forMarkers) {
        QImage pixels(area.size(), QImage::Format_ARGB32_Premultiplied);
        pixels.fill(Qt::transparent);
        QPainter painter(&pixels);
        painter.translate(-area.topLeft());
        painter.setRenderHint(QPainter::Antialiasing, true);
        for (const Shape& shape : shapes) {
            if (shape.points.empty() || shape.kind == ShapeKind::Pixelate || shape.style.marker != forMarkers) {
                continue;
            }
            if (forMarkers) {
                drawIn(&painter, shape, colourOf(shape), 0);
            } else {
                paintShape(&painter, shape, false);
            }
        }
        painter.end();
        return toBuffer(pixels);
    };
    if (markers) {
        edit.markers = layer(true);
    }
    if (others) {
        edit.drawing = layer(false);
    }
    edit.colour = (markers && hasColour(edit.markers)) || (others && hasColour(edit.drawing));
    return edit;
}

}  // namespace iv
