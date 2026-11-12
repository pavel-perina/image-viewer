#include "ui/annotate_overlay.h"

#include <QKeyEvent>
#include <QMetaEnum>
#include <QPainter>
#include <QSettings>

#include <algorithm>
#include <cmath>

#include "ui/shape_painter.h"

namespace iv {

namespace {

constexpr double kSmallest = 6;  // shapes drawn smaller than this, in item units, are dropped

using Tool = AnnotateOverlay::Tool;

ShapeKind kindOf(Tool tool) {
    switch (tool) {
    case Tool::Line: return ShapeKind::Line;
    case Tool::Arrow: return ShapeKind::Arrow;
    case Tool::Pixelate: return ShapeKind::Pixelate;
    case Tool::Rectangle:
    case Tool::Marker:
    case Tool::Select: break;
    }
    return ShapeKind::Rectangle;
}

Tool toolOf(const Shape& shape) {
    switch (shape.kind) {
    case ShapeKind::Line: return Tool::Line;
    case ShapeKind::Arrow: return Tool::Arrow;
    case ShapeKind::Pixelate: return Tool::Pixelate;
    default: return shape.style.marker ? Tool::Marker : Tool::Rectangle;
    }
}

// Where a tool keeps its style between runs.
QString settingsKey(Tool tool, const char* value) {
    return QStringLiteral("Annotate/%1/%2").arg(QLatin1StringView(QMetaEnum::fromType<Tool>().valueToKey(static_cast<int>(tool))), QLatin1StringView(value));
}

QColor toQColor(ShapeColour c) {
    return {c.r, c.g, c.b, c.a};
}

ShapeColour toShapeColour(const QColor& c) {
    return {static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()), static_cast<std::uint8_t>(c.blue()),
            static_cast<std::uint8_t>(c.alpha())};
}

}  // namespace

AnnotateOverlay::AnnotateOverlay(QQuickItem* parent) : ImageOverlay(parent) {}

void AnnotateOverlay::viewAttached() {
    m_previews.clear();
    if (view()) {
        connect(view(), &ImageView::drawReady, this, [this] {
            m_previews.clear();
            update();
        });
    }
}

PointD AnnotateOverlay::placed(ShapeKind kind, PointD imagePoint) {
    return snapsToPixels(kind) ? PointD{std::round(imagePoint.x), std::round(imagePoint.y)} : imagePoint;
}

// What a tool draws with: what it was last set to, and before that red lines whose width
// follows the size of the picture, a yellow marker, and blocks a sixtieth of the picture.
ShapeStyle AnnotateOverlay::styleOf(Tool tool) const {
    const double longEdge = view() ? std::max(view()->imageWidth(), view()->imageHeight()) : 1000;
    ShapeStyle style;
    style.colour = tool == Tool::Marker ? ShapeColour{255, 235, 59, 255} : ShapeColour{229, 57, 53, 255};
    style.width = tool == Tool::Pixelate ? std::max(4.0, std::round(longEdge / 60)) : std::max(1.0, std::round(longEdge / 500));
    style.marker = tool == Tool::Marker;

    const QSettings settings;
    const auto stored = [&](const char* key) { return settings.value(settingsKey(tool, key)); };
    if (const QColor colour = stored("colour").value<QColor>(); colour.isValid()) {
        style.colour = toShapeColour(colour);
    }
    if (const double width = stored("width").toDouble(); width > 0) {
        style.width = width;
    }
    style.filled = stored("filled").toBool();
    style.outline = stored("outline").toBool();
    style.shadow = stored("shadow").toBool();
    return style;
}

ShapeStyle AnnotateOverlay::shownStyle() const {
    return m_selected >= 0 ? m_shapes[static_cast<std::size_t>(m_selected)].style : styleOf(m_tool);
}

Tool AnnotateOverlay::styledTool() const {
    return m_selected >= 0 ? toolOf(m_shapes[static_cast<std::size_t>(m_selected)]) : m_tool;
}

template <typename Change>
void AnnotateOverlay::changeStyle(const char* key, const QVariant& value, Change change) {
    if (m_selected >= 0) {
        change(m_shapes[static_cast<std::size_t>(m_selected)].style);
        update();
    }
    // The tool remembers it too, so the next shape of the kind is like this one.
    if (const Tool tool = styledTool(); tool != Tool::Select) {
        QSettings().setValue(settingsKey(tool, key), value);
    }
    emit styleValuesChanged();
}

QColor AnnotateOverlay::colour() const {
    return toQColor(shownStyle().colour);
}

void AnnotateOverlay::setColour(const QColor& colour) {
    if (colour.isValid() && colour != this->colour()) {
        changeStyle("colour", colour, [&](ShapeStyle& style) { style.colour = toShapeColour(colour); });
    }
}

double AnnotateOverlay::lineWidth() const {
    return shownStyle().width;
}

void AnnotateOverlay::setLineWidth(double width) {
    if (width > 0 && width != lineWidth()) {
        changeStyle("width", width, [&](ShapeStyle& style) { style.width = width; });
    }
}

void AnnotateOverlay::setFilled(bool filled) {
    if (filled != this->filled()) {
        changeStyle("filled", filled, [&](ShapeStyle& style) { style.filled = filled; });
    }
}

void AnnotateOverlay::setOutline(bool outline) {
    if (outline != this->outline()) {
        changeStyle("outline", outline, [&](ShapeStyle& style) { style.outline = outline; });
    }
}

void AnnotateOverlay::setShadow(bool shadow) {
    if (shadow != this->shadow()) {
        changeStyle("shadow", shadow, [&](ShapeStyle& style) { style.shadow = shadow; });
    }
}

void AnnotateOverlay::setTool(Tool tool) {
    if (tool != m_tool) {
        m_tool = tool;
        select(-1);
        emit toolChanged();
        emit styleValuesChanged();
    }
}

void AnnotateOverlay::select(int index) {
    if (index != m_selected) {
        m_selected = index;
        update();
        emit selectedChanged();
        emit styleValuesChanged();
    }
}

void AnnotateOverlay::shapesChanged() {
    update();
    emit countChanged();
}

void AnnotateOverlay::removeSelected() {
    if (m_selected < 0) {
        return;
    }
    m_shapes.erase(m_shapes.begin() + m_selected);
    m_drag = Drag::None;
    select(-1);
    shapesChanged();
}

void AnnotateOverlay::clear() {
    m_shapes.clear();
    m_drag = Drag::None;
    select(-1);
    shapesChanged();
}

void AnnotateOverlay::apply() {
    if (view() && !m_shapes.empty()) {
        view()->draw(m_shapes);
    }
    clear();
}

ShapeHit AnnotateOverlay::hitAt(QPointF itemPoint) const {
    return shapeAt(m_shapes, toImage(itemPoint), kReach / itemPerPixel(), m_selected);
}

void AnnotateOverlay::pressed(QPointF itemPoint) {
    m_pressImage = toImage(itemPoint);
    m_hit = hitAt(itemPoint);
    if (m_hit.grip >= 0) {
        m_drag = Drag::Grip;
    } else if (m_tool != Tool::Select) {
        m_drag = Drag::Draw;  // or a click, which selects what is under it
    } else if (m_hit.shape >= 0) {
        select(m_hit.shape);
        m_drag = Drag::Move;
    } else {
        m_drag = Drag::Click;
    }
    if (m_hit.shape >= 0) {
        m_atPress = m_shapes[static_cast<std::size_t>(m_hit.shape)];
    }
}

void AnnotateOverlay::dragged(QPointF itemPoint, bool moved) {
    const PointD now = toImage(itemPoint);
    switch (m_drag) {
    case Drag::None:
    case Drag::Click:
        return;
    case Drag::Grip: {
        Shape& shape = m_shapes[static_cast<std::size_t>(m_hit.shape)];
        moveGrip(shape, m_hit.grip, placed(shape.kind, now));
        break;
    }
    case Drag::Move: {
        Shape& shape = m_shapes[static_cast<std::size_t>(m_hit.shape)];
        shape = m_atPress;
        moveBy(shape, placed(shape.kind, {now.x - m_pressImage.x, now.y - m_pressImage.y}));
        break;
    }
    case Drag::Draw:
        // A click, or a hand that shakes a little, draws nothing.
        if (!moved) {
            return;
        }
        m_shapes.push_back(startShape(kindOf(m_tool), styleOf(m_tool), placed(kindOf(m_tool), m_pressImage)));
        // The grip at the second point: the far end of a line, the opposite corner of a box.
        m_hit = {static_cast<int>(m_shapes.size()) - 1, static_cast<int>(grips(m_shapes.back()).size()) / 2};
        m_drag = Drag::Grip;
        moveGrip(m_shapes.back(), m_hit.grip, placed(m_shapes.back().kind, now));
        select(m_hit.shape);
        emit countChanged();
        break;
    }
    update();
}

void AnnotateOverlay::released(QPointF itemPoint) {
    const Drag drag = m_drag;
    m_drag = Drag::None;
    if (drag == Drag::Draw || drag == Drag::Click) {
        // Nothing was drawn or moved: a click, which selects what is under it, or nothing.
        select(hitAt(itemPoint).shape);
        return;
    }
    // A shape too small to mean anything goes again.
    if (drag == Drag::Grip && m_selected >= 0) {
        const RectD box = bounds(m_shapes[static_cast<std::size_t>(m_selected)]);
        if (std::hypot(box.width, box.height) * itemPerPixel() < kSmallest) {
            removeSelected();
        }
    }
}

Qt::CursorShape AnnotateOverlay::cursorAt(QPointF itemPoint) const {
    const ShapeHit hit = hitAt(itemPoint);
    if (hit.grip >= 0) {
        return Qt::PointingHandCursor;
    }
    if (m_tool == Tool::Select) {
        return hit.shape >= 0 ? Qt::SizeAllCursor : Qt::ArrowCursor;
    }
    return Qt::CrossCursor;
}

bool AnnotateOverlay::keyPressed(QKeyEvent* event) {
    if (m_selected < 0) {
        return false;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        removeSelected();
        return true;
    }
    if (event->key() == Qt::Key_Escape) {
        select(-1);
        return true;
    }
    return false;
}

// A marker or a pixelated box as it will be, from the pixels under it; until the view has
// those, a stand-in.
void AnnotateOverlay::paintPreview(QPainter* painter, std::size_t index) {
    const Shape& shape = m_shapes[index];
    Preview& preview = m_previews[index];
    if (preview.pixels.isNull() || preview.shape != shape) {
        preview.shape = shape;
        preview.pixels = view()->drawPreview(shape, &preview.where);
    }
    if (preview.pixels.isNull()) {
        paintShape(painter, shape, true);
        return;
    }
    // Enlarged, its pixels stay squares, as those of the picture under it do.
    painter->setRenderHint(QPainter::SmoothPixmapTransform, itemPerPixel() < 1);
    painter->drawImage(QRectF(preview.where), preview.pixels);
}

void AnnotateOverlay::paint(QPainter* painter) {
    if (!view() || view()->imageWidth() <= 0) {
        return;
    }
    // The shapes in image pixels, as they will be burned in.
    painter->save();
    const QPointF origin = toItem({0, 0});
    painter->translate(origin);
    painter->scale(itemPerPixel(), itemPerPixel());
    m_previews.resize(m_shapes.size());
    for (std::size_t i = 0; i < m_shapes.size(); ++i) {
        if (dependsOnPicture(m_shapes[i])) {
            paintPreview(painter, i);
        } else {
            paintShape(painter, m_shapes[i], true);
        }
    }
    painter->restore();

    if (m_selected < 0) {
        return;
    }
    // Around the one selected, a thin frame and its grips, at their size on screen.
    const Shape& chosen = m_shapes[static_cast<std::size_t>(m_selected)];
    painter->setRenderHint(QPainter::Antialiasing, true);
    const std::vector<PointD> at = grips(chosen);
    if (at.size() == 4 && chosen.kind != ShapeKind::Polygon) {
        for (std::size_t i = 0; i < 4; ++i) {
            painter->setPen(QPen(style().outline(), 2.5 * devicePixel()));
            painter->drawLine(toItem(at[i]), toItem(at[(i + 1) % 4]));
            QPen dashes(style().line(), devicePixel(), Qt::DashLine);
            painter->setPen(dashes);
            painter->drawLine(toItem(at[i]), toItem(at[(i + 1) % 4]));
        }
    }
    for (const PointD& grip : at) {
        drawGrip(painter, toItem(grip), style().line(), {.chosen = true});
    }
}

}  // namespace iv
