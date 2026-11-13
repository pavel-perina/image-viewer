#include "ui/shape_painter.h"

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QPainterPath>
#include <QGlyphRun>
#include <QPolygonF>
#include <QRawFont>
#include <QTextLayout>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace iv {

namespace {

// Opaque, so that a pass drawn in pieces shows no seams where they overlap.
const QColor kOutline(20, 20, 20);
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

// Text has no line width; its edge and shadow follow the size of its letters.
double weightOf(const Shape& shape) {
    return shape.kind == ShapeKind::Text ? shape.style.textSize * 0.16 : shape.style.width;
}

double outlineWidth(const Shape& shape) {
    return shape.style.outline ? std::max(1.0, std::round(weightOf(shape) * 0.25)) : 0;
}

double shadowOffset(const Shape& shape) {
    const double least = shape.kind == ShapeKind::Text ? 1 : 2;
    return shape.style.shadow ? std::max(least, std::round(weightOf(shape) * 0.6)) : 0;
}

// As long as four widths of the line and never tiny, so that it reads as an arrow.
double headLength(const Shape& shape) {
    return std::max(8.0, shape.style.width * 4);
}

bool isSolid(const Shape& shape) {
    return shape.style.filled || shape.style.marker;
}

QFont fontOf(const Shape& shape) {
    QFont font = QGuiApplication::font();
    if (!shape.style.fontFamily.empty()) {
        font.setFamily(QString::fromStdString(shape.style.fontFamily));
    }
    font.setBold(shape.style.bold);
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(shape.style.textSize))));
    // Letters fitted to the pixel grid, which is what keeps small text sharp.
    font.setHintingPreference(QFont::PreferFullHinting);
    return font;
}

// The words of a text box laid out from its top left corner, wrapped at its width, in the
// typeface chosen at the size given in image pixels. Text longer than the box is high runs on
// below it. As glyph runs, which Qt draws fitted to the pixel grid, and as their outlines, for
// the dark edge and the shadow, which lie under the letters and need not be as sharp.
struct TextLines {
    QList<QGlyphRun> runs;
    QPainterPath outlines;
};

TextLines layOut(const Shape& shape) {
    TextLines lines;
    // Nonzero, as the strokes of a letter may overlap, and would cut holes where they do.
    lines.outlines.setFillRule(Qt::WindingFill);
    const QFont font = fontOf(shape);
    const QFontMetricsF metrics(font);
    const QRectF box = boxOf(shape);
    const double width = std::max(box.width(), shape.style.textSize);
    const double step = std::ceil(metrics.lineSpacing());
    double top = 0;
    QTextOption wrap;
    wrap.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    const QStringList paragraphs = QString::fromStdString(shape.text).split(QLatin1Char('\n'));
    for (const QString& paragraph : paragraphs) {
        if (paragraph.isEmpty()) {
            top += step;
            continue;
        }
        QTextLayout layout(paragraph, font);
        layout.setTextOption(wrap);
        layout.beginLayout();
        for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
            line.setLineWidth(width);
            line.setPosition({0, top});
            top += step;
        }
        layout.endLayout();
        for (QGlyphRun run : layout.glyphRuns()) {
            // From the box's corner, like the outlines.
            QList<QPointF> positions = run.positions();
            const QList<quint32> glyphs = run.glyphIndexes();
            const QRawFont raw = run.rawFont();
            for (qsizetype i = 0; i < positions.size(); ++i) {
                positions[i] += box.topLeft();
                lines.outlines.addPath(raw.pathForGlyph(glyphs[i]).translated(positions[i]));
            }
            run.setPositions(positions);
            lines.runs.append(run);
        }
    }
    return lines;
}

// What a shape covers before its width: its points, and for text also what runs below its box.
QRectF extentOf(const Shape& shape) {
    return shape.kind == ShapeKind::Text ? boxOf(shape) | layOut(shape).outlines.boundingRect() : boxOf(shape);
}

// A path grown by half of `extra` all round, as one area.
QPainterPath grown(const QPainterPath& path, double extra, Qt::PenJoinStyle join) {
    if (extra <= 0) {
        return path;
    }
    QPainterPathStroker stroker;
    stroker.setWidth(extra);
    stroker.setJoinStyle(join);
    return path.united(stroker.createStroke(path));
}

// The shape in one colour, its lines wider by `extra`: once for the shape itself, and before
// that for its shadow and its dark edge. Each call paints the whole shape as one flat area,
// never one piece over another, so a colour that lets the picture through stays even, and the
// order shadow, edge, shape holds for all of a shape and not piece by piece.
enum class Pass { Shadow, Edge, Body };

void drawIn(QPainter* painter, const Shape& shape, const QColor& colour, double extra, Pass pass) {
    const double width = shape.style.width + extra;
    painter->setBrush(Qt::NoBrush);
    painter->setPen(Qt::NoPen);
    switch (shape.kind) {
    case ShapeKind::Line:
    case ShapeKind::Freehand: {
        QPolygonF line;
        for (const PointD& p : shape.points) {
            line << point(p);
        }
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
        QPainterPath shaft;
        shaft.moveTo(from);
        shaft.lineTo(base);
        QPainterPathStroker stroker;
        stroker.setWidth(width);
        stroker.setCapStyle(Qt::RoundCap);
        QPainterPath tip;
        tip.addPolygon(QPolygonF{to, base + across * head * 0.4, base - across * head * 0.4});
        tip.closeSubpath();
        painter->fillPath(stroker.createStroke(shaft).united(grown(tip, extra, Qt::MiterJoin)), colour);
        break;
    }
    case ShapeKind::Rectangle:
    case ShapeKind::Polygon: {
        QPainterPath path;
        bool whole = isSolid(shape);
        if (shape.kind == ShapeKind::Rectangle) {
            // Its box is the outer edge, with the line inside it: with corners on whole pixels
            // and a whole width, every edge then lies between pixels and stays sharp.
            const QRectF box = boxOf(shape);
            const double inset = whole ? 0 : shape.style.width / 2;
            // Too small to have a middle: all of it is line.
            whole = whole || box.width() <= 2 * inset || box.height() <= 2 * inset;
            path.addRect(whole ? box : box.adjusted(inset, inset, -inset, -inset));
        } else {
            QPolygonF corners;
            for (const PointD& p : shape.points) {
                corners << point(p);
            }
            path.addPolygon(corners);
            path.closeSubpath();
        }
        if (whole) {
            painter->fillPath(grown(path, extra, Qt::MiterJoin), colour);
        } else {
            painter->setPen(QPen(colour, width, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
            painter->drawPath(path);
        }
        break;
    }
    case ShapeKind::Text: {
        const TextLines lines = layOut(shape);
        if (pass == Pass::Edge) {
            // Opaque, so stroking over the fill leaves no seam.
            painter->setBrush(colour);
            painter->setPen(QPen(colour, extra, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->drawPath(lines.outlines);
        } else if (pass == Pass::Shadow) {
            // The letters alone, as one area; under a dark edge it lies further out instead
            // of being grown, which would need the letters and their edge joined into one.
            painter->fillPath(lines.outlines, colour);
        } else {
            painter->setPen(colour);
            for (const QGlyphRun& run : lines.runs) {
                painter->drawGlyphRun({0, 0}, run);
            }
        }
        break;
    }
    case ShapeKind::Pixelate:
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
    return extentOf(shape).adjusted(-margin, -margin, margin, margin).toAlignedRect() & QRect(0, 0, image.width, image.height);
}

double shapeMargin(const Shape& shape) {
    const double head = shape.kind == ShapeKind::Arrow ? headLength(shape) * 0.4 : 0;
    const double half = shape.kind == ShapeKind::Text ? 0 : shape.style.width / 2;
    return std::max(half, head) + outlineWidth(shape) + shadowOffset(shape) + 2;
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
    if (shape.kind == ShapeKind::Text && shape.text.empty()) {
        // Nothing typed yet: while previewing, the box to type into.
        if (preview) {
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(colourOf(shape), 0, Qt::DashLine));
            painter->drawRect(boxOf(shape));
        }
        return;
    }
    if (shape.style.marker) {
        if (preview) {
            QColor colour = colourOf(shape);
            colour.setAlphaF(colour.alphaF() * static_cast<float>(kMarkerPreview));
            drawIn(painter, shape, colour, 0, Pass::Body);
        }
        return;
    }
    if (const double offset = shadowOffset(shape); offset > 0) {
        painter->save();
        const double edge = outlineWidth(shape);
        const bool text = shape.kind == ShapeKind::Text;
        painter->translate(offset + (text ? edge : 0), offset + (text ? edge : 0));
        drawIn(painter, shape, kShadow, text ? 0 : 2 * edge, Pass::Shadow);
        painter->restore();
    }
    if (const double outline = outlineWidth(shape); outline > 0) {
        drawIn(painter, shape, kOutline, 2 * outline, Pass::Edge);
    }
    drawIn(painter, shape, colourOf(shape), 0, Pass::Body);
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
        if (shape.kind == ShapeKind::Text && shape.text.empty()) {
            continue;
        }
        drawn |= extentOf(shape).adjusted(-margin, -margin, margin, margin);
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
                drawIn(&painter, shape, colourOf(shape), 0, Pass::Body);
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
