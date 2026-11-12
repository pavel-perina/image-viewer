#include "ui/probe_overlay.h"

#include <QKeyEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace iv {

namespace {

constexpr double kSmallestFrame = 7;  // in item units, for a pixel shown smaller than that

QPoint positionOf(const QVariantMap& pixel) {
    return {pixel.value(QStringLiteral("x")).toInt(), pixel.value(QStringLiteral("y")).toInt()};
}

}  // namespace

ProbeOverlay::ProbeOverlay(QQuickItem* parent) : ImageOverlay(parent) {}

QVariant ProbeOverlay::pixel() const {
    return m_pixel.isEmpty() ? QVariant::fromValue(nullptr) : QVariant(m_pixel);
}

void ProbeOverlay::clear() {
    if (!m_pixel.isEmpty()) {
        m_pixel.clear();
        m_pickWhenRead = false;
        update();
        emit pixelChanged();
    }
}

void ProbeOverlay::viewAttached() {
    if (view()) {
        connect(view(), &ImageView::probed, this, [this](const QVariantMap& pixel) {
            m_pixel = pixel;
            update();
            emit pixelChanged();
            pickIfRead();
        });
    }
}

void ProbeOverlay::probeAt(PointD imagePoint) {
    m_requested = {static_cast<int>(std::floor(imagePoint.x)), static_cast<int>(std::floor(imagePoint.y))};
    view()->probe({imagePoint.x, imagePoint.y});
}

// Reading takes a moment, so a click picks once the pixel it asked for has been read.
void ProbeOverlay::pickIfRead() {
    if (m_pickWhenRead && !m_pixel.isEmpty() && positionOf(m_pixel) == m_requested) {
        m_pickWhenRead = false;
        emit picked(m_pixel);
    }
}

void ProbeOverlay::pressed(QPointF itemPoint) {
    m_pickWhenRead = false;
    probeAt(toImage(itemPoint));
}

void ProbeOverlay::dragged(QPointF itemPoint, bool) {
    probeAt(toImage(itemPoint));
}

void ProbeOverlay::released(QPointF) {
    m_pickWhenRead = true;
    pickIfRead();
}

bool ProbeOverlay::keyPressed(QKeyEvent* event) {
    if (m_pixel.isEmpty() || !view()) {
        return false;
    }
    QPoint step;
    switch (event->key()) {
    case Qt::Key_Left: step = {-1, 0}; break;
    case Qt::Key_Right: step = {1, 0}; break;
    case Qt::Key_Up: step = {0, -1}; break;
    case Qt::Key_Down: step = {0, 1}; break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit picked(m_pixel);
        return true;
    default:
        return false;
    }
    const QPoint next = positionOf(m_pixel) + step;
    m_pickWhenRead = false;
    probeAt({next.x() + 0.5, next.y() + 0.5});
    return true;
}

void ProbeOverlay::paint(QPainter* painter) {
    if (m_pixel.isEmpty() || !view()) {
        return;
    }
    const QPoint at = positionOf(m_pixel);
    const QPointF a = toItem({double(at.x()), double(at.y())});
    const QPointF b = toItem({at.x() + 1.0, at.y() + 1.0});
    const double size = std::max(kSmallestFrame, b.x() - a.x());
    const QRectF frame(snapped((a.x() + b.x() - size) / 2), snapped((a.y() + b.y() - size) / 2), snapped(size), snapped(size));

    // Light around the pixel and dark around that, each a device pixel wide, outside the
    // pixel itself so that its colour stays in view.
    const double line = devicePixel();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(style().line(), line));
    painter->drawRect(frame.adjusted(-line, -line, 0, 0));
    painter->setPen(QPen(style().outline(), line));
    painter->drawRect(frame.adjusted(-2 * line, -2 * line, line, line));
}

}  // namespace iv
