#include "ui/annotate_overlay.h"

#include <QFontDatabase>
#include <QGuiApplication>
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
    case Tool::Text: return ShapeKind::Text;
    case Tool::Ellipse: return ShapeKind::Ellipse;
    case Tool::Number: return ShapeKind::Number;
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
    case ShapeKind::Text: return Tool::Text;
    case ShapeKind::Ellipse: return Tool::Ellipse;
    case ShapeKind::Number: return Tool::Number;
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
// follows the size of the picture, a yellow marker (both colours of the row offered), blocks a sixtieth of the picture and
// letters a fiftieth of it.
ShapeStyle AnnotateOverlay::styleOf(Tool tool) const {
    const double longEdge = view() ? std::max(view()->imageWidth(), view()->imageHeight()) : 1000;
    ShapeStyle style;
    style.colour = tool == Tool::Marker ? ShapeColour{255, 255, 51, 255} : ShapeColour{228, 26, 28, 255};
    style.width = tool == Tool::Pixelate ? std::max(4.0, std::round(longEdge / 60)) : std::max(1.0, std::round(longEdge / 500));
    style.marker = tool == Tool::Marker;
    style.textSize = std::max(12.0, std::round(longEdge / 50));

    const QSettings settings;
    const auto stored = [&](const char* key) { return settings.value(settingsKey(tool, key)); };
    if (const QColor colour = stored("colour").value<QColor>(); colour.isValid()) {
        style.colour = toShapeColour(colour);
    }
    if (const double width = stored("width").toDouble(); width > 0) {
        style.width = width;
    }
    if (const double size = stored("textSize").toDouble(); size > 0) {
        style.textSize = size;
    }
    style.fontFamily = stored("font").toString().toStdString();
    style.bold = stored("bold").toBool();
    style.filled = stored("filled").toBool();
    style.outline = stored("outline").toBool();
    style.shadow = stored("shadow").toBool();
    // Kept only once set; below zero is "follow the shape".
    const auto set = [&](const char* key) {
        const QVariant value = stored(key);
        return value.isValid() && value.toDouble() >= 0 ? std::optional<double>(value.toDouble()) : std::nullopt;
    };
    style.outlineWidth = set("outlineWidth");
    style.shadowBlur = set("shadowBlur");
    if (const auto offset = set("shadowOffset")) {
        style.shadowOffset = PointD{*offset, *offset};
    }
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

void AnnotateOverlay::setOutlineWidth(double width) {
    if (width != outlineWidth()) {
        changeStyle("outlineWidth", width, [&](ShapeStyle& style) { style.outlineWidth = width >= 0 ? std::optional<double>(width) : std::nullopt; });
    }
}

// One distance here, the same right and down, though a shape may have any two.
double AnnotateOverlay::shadowOffset() const {
    const auto offset = shownStyle().shadowOffset;
    return offset ? offset->x : -1;
}

void AnnotateOverlay::setShadowOffset(double offset) {
    if (offset != shadowOffset()) {
        changeStyle("shadowOffset", offset,
                    [&](ShapeStyle& style) { style.shadowOffset = offset >= 0 ? std::optional<PointD>(PointD{offset, offset}) : std::nullopt; });
    }
}

void AnnotateOverlay::setShadowBlur(double blur) {
    if (blur != shadowBlur()) {
        changeStyle("shadowBlur", blur, [&](ShapeStyle& style) { style.shadowBlur = blur >= 0 ? std::optional<double>(blur) : std::nullopt; });
    }
}

void AnnotateOverlay::setTextSize(double size) {
    if (size > 0 && size != textSize()) {
        changeStyle("textSize", size, [&](ShapeStyle& style) { style.textSize = size; });
    }
}

// The application's own typeface where none was chosen.
QString AnnotateOverlay::fontFamily() const {
    const std::string family = shownStyle().fontFamily;
    return family.empty() ? QGuiApplication::font().family() : QString::fromStdString(family);
}

void AnnotateOverlay::setFontFamily(const QString& family) {
    if (!family.isEmpty() && family != fontFamily()) {
        changeStyle("font", family, [&](ShapeStyle& style) { style.fontFamily = family.toStdString(); });
    }
}

void AnnotateOverlay::setBold(bool bold) {
    if (bold != this->bold()) {
        changeStyle("bold", bold, [&](ShapeStyle& style) { style.bold = bold; });
    }
}

QStringList AnnotateOverlay::fontFamilies() const {
    QStringList families = QFontDatabase::families();
    if (const QString own = QGuiApplication::font().family(); !families.contains(own)) {
        families.prepend(own);
    }
    return families;
}

QString AnnotateOverlay::text() const {
    return m_selected >= 0 ? QString::fromStdString(m_shapes[static_cast<std::size_t>(m_selected)].text) : QString();
}

void AnnotateOverlay::setText(const QString& text) {
    if (m_selected < 0 || text == this->text()) {
        return;
    }
    if (const ShapeKind kind = m_shapes[static_cast<std::size_t>(m_selected)].kind; kind != ShapeKind::Text && kind != ShapeKind::Number) {
        return;
    }
    m_shapes[static_cast<std::size_t>(m_selected)].text = text.toStdString();
    update();
    emit textChanged();
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
        emit textChanged();
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

Shape AnnotateOverlay::begun(PointD imagePoint) const {
    const ShapeKind kind = kindOf(m_tool);
    Shape shape = startShape(kind, styleOf(m_tool), placed(kind, imagePoint));
    if (kind == ShapeKind::Number) {
        shape.text = std::to_string(nextNumber(m_shapes));
    }
    return shape;
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
        moveGrip(shape, m_hit.grip, placed(shape.kind, now), shiftHeld());
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
        m_shapes.push_back(begun(m_pressImage));
        // The grip at the second point: the far end of a line, the opposite corner of a box.
        m_hit = {static_cast<int>(m_shapes.size()) - 1, static_cast<int>(grips(m_shapes.back()).size()) / 2};
        m_drag = Drag::Grip;
        moveGrip(m_shapes.back(), m_hit.grip, placed(m_shapes.back().kind, now), shiftHeld());
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
        const int under = hitAt(itemPoint).shape;
        if (under < 0 && m_tool == Tool::Number) {
            m_shapes.push_back(begun(m_pressImage));
            select(static_cast<int>(m_shapes.size()) - 1);
            shapesChanged();
            return;
        }
        if (under < 0 && m_tool == Tool::Text) {
            // On nothing, with the Text tool, it places a box of a size to start typing in.
            Shape box = begun(m_pressImage);
            const ShapeStyle& style = box.style;
            box.points[1] = {box.points[0].x + std::round(style.textSize * 10), box.points[0].y + std::round(style.textSize * 1.5)};
            m_shapes.push_back(std::move(box));
            select(static_cast<int>(m_shapes.size()) - 1);
            shapesChanged();
            emit textWanted();
            return;
        }
        select(under);
        return;
    }
    // A shape too small to mean anything goes again.
    if (drag == Drag::Grip && m_selected >= 0 && m_shapes[static_cast<std::size_t>(m_selected)].kind != ShapeKind::Number) {
        const RectD box = bounds(m_shapes[static_cast<std::size_t>(m_selected)]);
        if (std::hypot(box.width, box.height) * itemPerPixel() < kSmallest) {
            removeSelected();
        } else if (m_hit.shape == count() - 1 && m_shapes.back().kind == ShapeKind::Text && m_shapes.back().text.empty()) {
            emit textWanted();
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
    m_previews.resize(m_shapes.size());
    const auto paintAll = [this](QPainter* target, const ShapeRaster& raster) {
        for (std::size_t i = 0; i < m_shapes.size(); ++i) {
            if (dependsOnPicture(m_shapes[i])) {
                paintPreview(target, i);
            } else {
                paintShape(target, m_shapes[i], true, raster);
            }
        }
    };
    // Enlarged, the shapes are drawn as pixels of the image and those shown enlarged, like
    // the picture under them: this is what will be burned in, down to how letters sit on the
    // pixel grid, so that it can be judged at any zoom. Only where shapes are in view.
    const Size image{view()->imageWidth(), view()->imageHeight()};
    const PointD first = toImage({0, 0});
    const PointD last = toImage({width(), height()});
    QRect drawn;
    for (const Shape& shape : m_shapes) {
        drawn |= shapeArea(shape, image);
    }
    const QRect inView = QRectF(QPointF(first.x, first.y), QPointF(last.x, last.y)).toAlignedRect();
    drawn &= inView;
    const double enlarged = itemPerPixel() / devicePixel();
    if (enlarged >= 1 && !drawn.isEmpty()) {
        QImage layer(drawn.size(), QImage::Format_ARGB32_Premultiplied);
        layer.fill(Qt::transparent);
        QPainter onLayer(&layer);
        onLayer.translate(-drawn.topLeft());
        paintAll(&onLayer, {.clip = drawn, .scale = 1});
        onLayer.end();
        // As a pattern to fill with, not drawImage: enlarging with that rounds where the
        // image starts to a whole pixel of the screen, which moves every edge of the shapes
        // off the edges of the picture's pixels.
        const QRectF target(toItem({double(drawn.left()), double(drawn.top())}),
                            toItem({drawn.left() + double(drawn.width()), drawn.top() + double(drawn.height())}));
        QBrush pattern(layer);
        pattern.setTransform(QTransform::fromTranslate(target.left(), target.top()).scale(itemPerPixel(), itemPerPixel()));
        painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->fillRect(target, pattern);
    } else if (!drawn.isEmpty()) {
        // Reduced, drawing at the size shown is close enough, and far less work than all
        // the pixels of a large picture; dark edges and shadows are worked out at that size too.
        painter->save();
        painter->translate(toItem({0, 0}));
        painter->scale(itemPerPixel(), itemPerPixel());
        paintAll(painter, {.clip = inView, .scale = enlarged});
        painter->restore();
    }

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
