#include "ui/shape_painter.h"

#include "core/coverage.h"

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

const QColor kOutline(20, 20, 20);
constexpr float kShadow = 0.55F;     // how dark a shadow is where it is whole
constexpr double kOutlineSoft = 1.5;  // pixels the dark edge fades over: a little softer than drawn
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

bool hasLetters(const Shape& shape) {
    return shape.kind == ShapeKind::Text || shape.kind == ShapeKind::Number;
}

// Text has no line width; its edge and shadow follow the size of its letters.
double weightOf(const Shape& shape) {
    return hasLetters(shape) ? shape.style.textSize * 0.16 : shape.style.width;
}

// Unless set: a quarter of the line, and two pixels at least, as less does not show.
double outlineWidth(const Shape& shape) {
    if (!shape.style.outline) {
        return 0;
    }
    return std::max(0.0, shape.style.outlineWidth.value_or(std::max(2.0, std::round(weightOf(shape) * 0.25))));
}

// Unless set: right and down by a little more than half the line.
QPointF shadowOffset(const Shape& shape) {
    if (shape.style.shadowOffset) {
        return point(*shape.style.shadowOffset);
    }
    const double least = hasLetters(shape) ? 1 : 2;
    const double by = std::max(least, std::round(weightOf(shape) * 0.6));
    return {by, by};
}

// Unless set: softer the further away the shadow lies.
double shadowBlur(const Shape& shape) {
    const QPointF offset = shadowOffset(shape);
    return std::max(0.0, shape.style.shadowBlur.value_or(0.4 * std::max(std::abs(offset.x()), std::abs(offset.y()))));
}

// How far a shape's dark edge and shadow reach beyond the shape itself.
double reachOfEffects(const Shape& shape) {
    const double edge = shape.style.outline ? outlineWidth(shape) + kOutlineSoft : 0;
    if (!shape.style.shadow) {
        return edge;
    }
    const QPointF offset = shadowOffset(shape);
    return edge + std::max(std::abs(offset.x()), std::abs(offset.y())) + 3 * shadowBlur(shape);
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

QRectF discOf(const Shape& shape) {
    const double radius = numberRadius(shape.style);
    return QRectF(point(shape.points.front()) - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius));
}

// What a shape covers before its width: its points, for text also what runs below its box,
// and a number's disc.
QRectF extentOf(const Shape& shape) {
    if (shape.kind == ShapeKind::Number) {
        return discOf(shape);
    }
    return shape.kind == ShapeKind::Text ? boxOf(shape) | layOut(shape).outlines.boundingRect() : boxOf(shape);
}

// Black or white, whichever reads on the colour.
QColor readableOn(const QColor& colour) {
    return 0.299 * colour.red() + 0.587 * colour.green() + 0.114 * colour.blue() > 150 ? QColor(20, 20, 20) : QColor(Qt::white);
}

// The shape in one colour, as one flat area, never one piece over another, so that a colour
// which lets the picture through stays even.
void drawIn(QPainter* painter, const Shape& shape, const QColor& colour) {
    const double width = shape.style.width;
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
        painter->fillPath(stroker.createStroke(shaft).united(tip), colour);
        break;
    }
    case ShapeKind::Rectangle:
    case ShapeKind::Ellipse:
    case ShapeKind::Polygon: {
        QPainterPath path;
        bool whole = isSolid(shape);
        if (shape.kind != ShapeKind::Polygon) {
            // Its box is the outer edge, with the line inside it: with corners on whole pixels
            // and a whole width, every edge then lies between pixels and stays sharp.
            const QRectF box = boxOf(shape);
            const double inset = whole ? 0 : shape.style.width / 2;
            // Too small to have a middle: all of it is line.
            whole = whole || box.width() <= 2 * inset || box.height() <= 2 * inset;
            const QRectF line = whole ? box : box.adjusted(inset, inset, -inset, -inset);
            if (shape.kind == ShapeKind::Ellipse) {
                path.addEllipse(line);
            } else {
                path.addRect(line);
            }
        } else {
            QPolygonF corners;
            for (const PointD& p : shape.points) {
                corners << point(p);
            }
            path.addPolygon(corners);
            path.closeSubpath();
        }
        if (whole) {
            painter->fillPath(path, colour);
        } else {
            painter->setPen(QPen(colour, width, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
            painter->drawPath(path);
        }
        break;
    }
    case ShapeKind::Text: {
        painter->setPen(colour);
        for (const QGlyphRun& run : layOut(shape).runs) {
            painter->drawGlyphRun({0, 0}, run);
        }
        break;
    }
    case ShapeKind::Number: {
        const QRectF disc = discOf(shape);
        QPainterPath path;
        path.addEllipse(disc);
        painter->fillPath(path, colour);
        if (shape.text.empty()) {
            break;
        }
        // Its text in the middle, made smaller when it is too long for the disc.
        const QString label = QString::fromStdString(shape.text);
        QFont font = fontOf(shape);
        const double room = disc.width() * 0.8;
        if (const double wide = QFontMetricsF(font).horizontalAdvance(label); wide > room) {
            font.setPixelSize(std::max(1, static_cast<int>(font.pixelSize() * room / wide)));
        }
        painter->setFont(font);
        painter->setPen(readableOn(colour));
        const QFontMetricsF metrics(font);
        // By the height of capitals and digits, so that a number sits in the middle whatever
        // the room the typeface leaves under its line.
        const double left = disc.center().x() - metrics.horizontalAdvance(label) / 2;
        const double baseline = disc.center().y() + metrics.capHeight() / 2;
        painter->drawText(QPointF(std::round(left), std::round(baseline)), label);
        break;
    }
    case ShapeKind::Pixelate:
        break;
    }
}

// The dark edge and the shadow of a shape, which lie under it. Made from the pixels the shape
// covers: grown for the edge, and that moved and blurred for the shadow, so that they are the
// same for every kind of shape and fit what is really drawn, letters included.
void paintUnder(QPainter* painter, const Shape& shape, const ShapeRaster& raster) {
    const double reach = reachOfEffects(shape);
    const int margin = static_cast<int>(std::ceil(shapeMargin(shape)));
    QRect area = extentOf(shape).adjusted(-margin, -margin, margin, margin).toAlignedRect();
    if (!raster.clip.isEmpty()) {
        // What is in view, and around it as much as can reach into it.
        const int around = static_cast<int>(std::ceil(reach)) + 1;
        area &= raster.clip.adjusted(-around, -around, around, around);
    }
    if (area.isEmpty()) {
        return;
    }
    const double scale = std::clamp(raster.scale, 0.01, 1.0);
    const QSize size(std::max(1, static_cast<int>(std::ceil(area.width() * scale))), std::max(1, static_cast<int>(std::ceil(area.height() * scale))));
    QImage pixels(size, QImage::Format_ARGB32_Premultiplied);
    pixels.fill(Qt::transparent);
    {
        QPainter alone(&pixels);
        alone.setRenderHint(QPainter::Antialiasing, true);
        alone.scale(scale, scale);
        alone.translate(-area.topLeft());
        drawIn(&alone, shape, Qt::white);
    }
    Coverage body(size.width(), size.height());
    for (int y = 0; y < size.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(pixels.constScanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            body.at(x, y) = static_cast<float>(qAlpha(row[x])) / 255.0F;
        }
    }
    Coverage edge;
    if (shape.style.outline) {
        edge = spread(body, outlineWidth(shape) * scale, kOutlineSoft);
    }
    Coverage shade;
    if (shape.style.shadow) {
        // Of the shape with its edge, as that is what stands above the picture.
        const QPointF offset = shadowOffset(shape) * scale;
        shade = blurred(shifted(shape.style.outline ? edge : body, offset.x(), offset.y()), shadowBlur(shape) * scale);
    }
    for (int y = 0; y < size.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(pixels.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            const float dark = edge.values.empty() ? 0 : edge.at(x, y);
            const float shadow = shade.values.empty() ? 0 : std::min(1.0F, shade.at(x, y)) * kShadow;
            if (dark <= 0 && shadow <= 0) {
                row[x] = 0;
                continue;
            }
            // The edge over the shadow, which is black; multiplied by alpha as the image is.
            const float alpha = dark + shadow * (1 - dark);
            const auto level = [&](int of) { return static_cast<int>(std::lround(static_cast<float>(of) * dark)); };
            row[x] = qRgba(level(kOutline.red()), level(kOutline.green()), level(kOutline.blue()), static_cast<int>(std::lround(alpha * 255.0F)));
        }
    }
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawImage(QRectF(area.topLeft(), QSizeF(size) / scale), pixels);
    painter->restore();
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
    const double half = hasLetters(shape) ? 0 : shape.style.width / 2;
    return std::max(half, head) + reachOfEffects(shape) + 2;
}

void paintShape(QPainter* painter, const Shape& shape, bool preview, const ShapeRaster& raster) {
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
            drawIn(painter, shape, colour);
        }
        return;
    }
    if (shape.style.outline || shape.style.shadow) {
        paintUnder(painter, shape, raster);
    }
    drawIn(painter, shape, colourOf(shape));
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
                drawIn(&painter, shape, colourOf(shape));
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
