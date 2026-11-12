#include "ui/straighten_overlay.h"

#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace iv {

namespace {

constexpr double kMinLength = 12;  // shorter new lines, in item units, are dropped

double distanceToSegment(QPointF p, QPointF a, QPointF b) {
    return iv::distanceToSegment(PointD{p.x(), p.y()}, PointD{a.x(), a.y()}, PointD{b.x(), b.y()});
}

}  // namespace

StraightenOverlay::StraightenOverlay(QQuickItem* parent) : ImageOverlay(parent) {
    connect(this, &StraightenOverlay::coloursChanged, this, [this] { update(); });
}

void StraightenOverlay::viewAttached() {
    if (view()) {
        connect(view(), &ImageView::straightenReady, this, [this] {
            m_previewValid = false;
            update();
        });
    }
}

QVariantList StraightenOverlay::lines() const {
    QVariantList result;
    for (const GuideLine& line : m_lines) {
        result.append(QVariantMap{{QStringLiteral("x1"), line.a.x}, {QStringLiteral("y1"), line.a.y},
                                  {QStringLiteral("x2"), line.b.x}, {QStringLiteral("y2"), line.b.y},
                                  {QStringLiteral("drawn"), line.meant}});
    }
    return result;
}

void StraightenOverlay::setLines(const QVariantList& lines) {
    m_lines.clear();
    for (const QVariant& value : lines) {
        const QVariantMap line = value.toMap();
        m_lines.push_back({{line.value(QStringLiteral("x1")).toDouble(), line.value(QStringLiteral("y1")).toDouble()},
                           {line.value(QStringLiteral("x2")).toDouble(), line.value(QStringLiteral("y2")).toDouble()},
                           line.value(QStringLiteral("drawn")).toBool()});
    }
    m_used.clear();
    select(-1);
    m_drag = Drag::None;
    update();
    emit linesChanged();
    emit usedChanged();
}

void StraightenOverlay::setUsed(const QVariantList& used) {
    m_used = used;
    update();
    emit usedChanged();
}

void StraightenOverlay::setRoll(double value) {
    if (value != m_tilt.roll) {
        m_tilt.roll = value;
        tiltChange();
    }
}

void StraightenOverlay::setPitch(double value) {
    if (value != m_tilt.pitch) {
        m_tilt.pitch = value;
        tiltChange();
    }
}

void StraightenOverlay::setYaw(double value) {
    if (value != m_tilt.yaw) {
        m_tilt.yaw = value;
        tiltChange();
    }
}

void StraightenOverlay::setFocal35mm(double value) {
    if (value != m_focal35mm) {
        m_focal35mm = value;
        tiltChange();
    }
}

void StraightenOverlay::setKeepRatio(bool value) {
    if (value != m_keepRatio) {
        m_keepRatio = value;
        tiltChange();
    }
}

void StraightenOverlay::setShowResult(bool value) {
    if (value != m_showResult) {
        m_showResult = value;
        update();
        emit showResultChanged();
    }
}

void StraightenOverlay::tiltChange() {
    m_previewValid = false;
    update();
    emit tiltChanged();
}

void StraightenOverlay::removeSelected() {
    if (m_selected < 0 || m_selected >= static_cast<int>(m_lines.size())) {
        return;
    }
    m_lines.erase(m_lines.begin() + m_selected);
    m_used.clear();
    select(-1);
    update();
    emit linesChanged();
    emit linesEdited();
}

void StraightenOverlay::select(int index) {
    if (index != m_selected) {
        m_selected = index;
        update();
        emit selectedChanged();
    }
}

// Ends before lines, and the line chosen before the others.
StraightenOverlay::Hit StraightenOverlay::hitAt(QPointF itemPoint) const {
    Hit best;
    double nearest = kReach;
    const auto consider = [&](int i) {
        const QPointF a = toItem(m_lines[static_cast<std::size_t>(i)].a);
        const QPointF b = toItem(m_lines[static_cast<std::size_t>(i)].b);
        for (int end = 0; end < 2; ++end) {
            const QPointF p = end == 0 ? a : b;
            const double d = std::hypot(itemPoint.x() - p.x(), itemPoint.y() - p.y());
            if (d <= nearest) {
                nearest = d;
                best = {i, end};
            }
        }
    };
    if (m_selected >= 0) {
        consider(m_selected);
    }
    for (int i = 0; i < static_cast<int>(m_lines.size()) && best.line < 0; ++i) {
        consider(i);
    }
    if (best.line >= 0) {
        return best;
    }
    nearest = kReach * 0.75;
    for (int i = 0; i < static_cast<int>(m_lines.size()); ++i) {
        const double d = distanceToSegment(itemPoint, toItem(m_lines[static_cast<std::size_t>(i)].a),
                                           toItem(m_lines[static_cast<std::size_t>(i)].b));
        if (d <= nearest) {
            nearest = d;
            best = {i, -1};
        }
    }
    return best;
}

void StraightenOverlay::pressed(QPointF itemPoint) {
    m_pressImage = toImage(itemPoint);
    m_hit = hitAt(itemPoint);
    if (m_hit.line >= 0) {
        select(m_hit.line);
        m_atPress = m_lines[static_cast<std::size_t>(m_hit.line)];
        m_drag = m_hit.end >= 0 ? Drag::End : Drag::Line;
    } else {
        select(-1);
        m_drag = Drag::Draw;
    }
}

void StraightenOverlay::dragged(QPointF itemPoint, bool moved) {
    const PointD now = toImage(itemPoint);
    const PointD delta{now.x - m_pressImage.x, now.y - m_pressImage.y};
    switch (m_drag) {
    case Drag::None:
        return;
    case Drag::End: {
        GuideLine& line = m_lines[static_cast<std::size_t>(m_hit.line)];
        PointD& end = m_hit.end == 0 ? line.a : line.b;
        const PointD from = m_hit.end == 0 ? m_atPress.a : m_atPress.b;
        end = {from.x + delta.x, from.y + delta.y};
        line.meant = true;
        break;
    }
    case Drag::Line: {
        GuideLine& line = m_lines[static_cast<std::size_t>(m_hit.line)];
        line = {{m_atPress.a.x + delta.x, m_atPress.a.y + delta.y}, {m_atPress.b.x + delta.x, m_atPress.b.y + delta.y}, true};
        break;
    }
    case Drag::Draw:
        // A click, or a hand that shakes a little, draws nothing.
        if (!moved) {
            return;
        }
        m_lines.push_back({m_pressImage, now, true});
        m_atPress = m_lines.back();
        m_hit = {static_cast<int>(m_lines.size()) - 1, 1};
        m_drag = Drag::End;
        select(m_hit.line);
        break;
    }
    m_used.clear();
    update();
    emit linesChanged();
    emit linesEdited();
}

void StraightenOverlay::released(QPointF) {
    // A new line too short to mean anything goes again.
    if (m_drag == Drag::End && m_selected >= 0) {
        const GuideLine& line = m_lines[static_cast<std::size_t>(m_selected)];
        const QPointF a = toItem(line.a);
        const QPointF b = toItem(line.b);
        if (std::hypot(a.x() - b.x(), a.y() - b.y()) < kMinLength) {
            removeSelected();
        }
    }
    m_drag = Drag::None;
}

Qt::CursorShape StraightenOverlay::cursorAt(QPointF itemPoint) const {
    const Hit hit = hitAt(itemPoint);
    return hit.line < 0 ? Qt::CrossCursor : hit.end >= 0 ? Qt::PointingHandCursor : Qt::SizeAllCursor;
}

bool StraightenOverlay::keyPressed(QKeyEvent* event) {
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && m_selected >= 0) {
        removeSelected();
        return true;
    }
    if (event->key() == Qt::Key_V && !event->isAutoRepeat()) {
        setShowResult(!m_showResult);
        return true;
    }
    return false;
}

void StraightenOverlay::paint(QPainter* painter) {
    if (!view() || view()->imageWidth() <= 0) {
        return;
    }
    const double thin = devicePixel();
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (m_showResult) {
        // The straightened picture over the whole view, fitted, with a grid to judge it by.
        painter->fillRect(boundingRect(), QColor(32, 32, 32));
        if (!m_previewValid) {
            m_preview = view()->straightenPreview(m_tilt, m_focal35mm, m_keepRatio);
            m_previewValid = true;
        }
        if (m_preview.isNull()) {
            return;
        }
        const QSizeF room = boundingRect().size() * 0.96;
        const QSizeF size = QSizeF(m_preview.size()).scaled(room, Qt::KeepAspectRatio);
        const QRectF target(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawImage(target, m_preview);
        painter->setPen(QPen(QColor(255, 255, 255, 70), thin));
        constexpr int kCells = 8;
        for (int i = 1; i < kCells; ++i) {
            const double x = target.left() + target.width() * i / kCells;
            const double y = target.top() + target.height() * i / kCells;
            painter->drawLine(QPointF(x, target.top()), QPointF(x, target.bottom()));
            painter->drawLine(QPointF(target.left(), y), QPointF(target.right(), y));
        }
        return;
    }

    // What will be kept, unless nothing is to change.
    if (!m_tilt.isZero()) {
        const auto kept = view()->straightenKept(m_tilt, m_focal35mm, m_keepRatio);
        QPolygonF outline;
        for (const QPointF& p : kept) {
            outline << toItem({p.x(), p.y()});
        }
        QPainterPath outside;
        outside.addRect(boundingRect());
        QPainterPath inside;
        inside.addPolygon(outline);
        inside.closeSubpath();
        painter->fillPath(outside.subtracted(inside), style().dim());
        QPen dashed(style().line(), thin);
        dashed.setDashPattern({4, 4});
        painter->setPen(dashed);
        painter->setBrush(Qt::NoBrush);
        painter->drawPolygon(outline);
    }

    // Dark under light, so that lines show on any picture. Those the fit left out are dashed,
    // with hollow ends, still plain enough to find and drag into place or remove.
    for (int i = 0; i < static_cast<int>(m_lines.size()); ++i) {
        const GuideLine& line = m_lines[static_cast<std::size_t>(i)];
        const QPointF a = toItem(line.a);
        const QPointF b = toItem(line.b);
        const bool left = i < m_used.size() && !m_used[i].toBool();
        const QColor colour = line.isVertical() ? m_verticalColor : m_horizontalColor;
        const bool chosen = i == m_selected;
        strokeLine(painter, a, b, colour, {.chosen = chosen, .dashed = left});
        drawGrip(painter, a, colour, {.chosen = chosen, .hollow = left});
        drawGrip(painter, b, colour, {.chosen = chosen, .hollow = left});
    }
}

}  // namespace iv
