#include "ui/color_ramp.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace iv {

ColorRamp::ColorRamp(QQuickItem* parent) : QQuickPaintedItem(parent) { setAntialiasing(true); }

void ColorRamp::setStops(const QVariantList& stops) {
    if (stops != m_stops) {
        m_stops = stops;
        emit stopsChanged();
        update();
    }
}

void ColorRamp::setHatch(const QColor& hatch) {
    if (hatch != m_hatch) {
        m_hatch = hatch;
        emit hatchChanged();
        update();
    }
}

void ColorRamp::setRadius(double radius) {
    if (radius != m_radius) {
        m_radius = radius;
        emit radiusChanged();
        update();
    }
}

void ColorRamp::paint(QPainter* painter) {
    const QRectF area = boundingRect();
    const auto count = m_stops.size();
    if (count < 2 || area.isEmpty()) {
        return;
    }
    QPainterPath shape;
    shape.addRoundedRect(area, m_radius, m_radius);
    painter->setClipPath(shape);

    QLinearGradient gradient(area.topLeft(), area.topRight());
    for (qsizetype i = 0; i < count; ++i) {
        gradient.setColorAt(static_cast<double>(i) / static_cast<double>(count - 1), m_stops[i].toMap().value(QStringLiteral("colour")).value<QColor>());
    }
    painter->fillRect(area, gradient);

    // Each stop stands for the stretch around it; runs of stops outside sRGB are hatched.
    const double step = area.width() / static_cast<double>(count - 1);
    QBrush hatching(m_hatch, Qt::BDiagPattern);
    for (qsizetype i = 0; i < count;) {
        if (!m_stops[i].toMap().value(QStringLiteral("outside")).toBool()) {
            ++i;
            continue;
        }
        qsizetype end = i;
        while (end < count && m_stops[end].toMap().value(QStringLiteral("outside")).toBool()) {
            ++end;
        }
        const double left = std::max(area.left(), area.left() + (static_cast<double>(i) - 0.5) * step);
        const double right = std::min(area.right(), area.left() + (static_cast<double>(end) - 0.5) * step);
        painter->fillRect(QRectF(left, area.top(), right - left, area.height()), hatching);
        i = end;
    }
}

}  // namespace iv
