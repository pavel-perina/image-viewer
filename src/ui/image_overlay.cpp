#include "ui/image_overlay.h"

#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>

#include <cmath>

namespace iv {

namespace {

constexpr double kDragAfter = 4;  // how far the mouse moves before it is a drag, in item units

}  // namespace

ImageOverlay::ImageOverlay(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setCursor(Qt::CrossCursor);
}

void ImageOverlay::setView(ImageView* view) {
    if (view == m_view) {
        return;
    }
    if (m_view) {
        disconnect(m_view, nullptr, this, nullptr);
    }
    m_view = view;
    if (m_view) {
        connect(m_view, &ImageView::zoomChanged, this, [this] { update(); });
    }
    viewAttached();
    emit viewChanged();
}

PointD ImageOverlay::toImage(QPointF itemPoint) const {
    const QPointF p = m_view ? m_view->itemToImage(mapToItem(m_view, itemPoint)) : itemPoint;
    return {p.x(), p.y()};
}

QPointF ImageOverlay::toItem(PointD imagePoint) const {
    const QPointF p(imagePoint.x, imagePoint.y);
    return m_view ? mapFromItem(m_view, m_view->imageToItem(p)) : p;
}

QPointF ImageOverlay::toItemSnapped(PointD imagePoint) const {
    const QPointF p = toItem(imagePoint);
    return {snapped(p.x()), snapped(p.y())};
}

double ImageOverlay::snapped(double itemLength) const {
    const double pixel = devicePixel();
    return std::round(itemLength / pixel) * pixel;
}

double ImageOverlay::itemPerPixel() const {
    return m_view ? m_view->imageToItem({1, 0}).x() - m_view->imageToItem({0, 0}).x() : 1;
}

double ImageOverlay::devicePixel() const {
    return window() ? 1 / window()->effectiveDevicePixelRatio() : 1.0;
}

void ImageOverlay::mousePressEvent(QMouseEvent* event) {
    if (!m_view || !usable()) {
        event->ignore();
        return;
    }
    forceActiveFocus();  // for Space and the tool's keys
    m_lastItem = event->position();
    if (event->button() != Qt::LeftButton || m_spaceHeld) {
        m_mouse = Mouse::Pan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    m_mouse = Mouse::Tool;
    m_pressItem = event->position();
    m_moved = false;
    pressed(event->position());
}

void ImageOverlay::mouseMoveEvent(QMouseEvent* event) {
    const QPointF item = event->position();
    if (m_mouse == Mouse::Pan && m_view) {
        m_view->panBy(item.x() - m_lastItem.x(), item.y() - m_lastItem.y());
    } else if (m_mouse == Mouse::Tool) {
        m_moved = m_moved || std::hypot(item.x() - m_pressItem.x(), item.y() - m_pressItem.y()) >= kDragAfter;
        dragged(item, m_moved);
    }
    m_lastItem = item;
}

void ImageOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (m_mouse == Mouse::Tool) {
        released(event->position());
    }
    m_mouse = Mouse::Up;
    setCursor(m_spaceHeld ? Qt::OpenHandCursor : cursorAt(event->position()));
}

void ImageOverlay::hoverMoveEvent(QHoverEvent* event) {
    if (m_mouse == Mouse::Up && m_view && usable()) {
        setCursor(m_spaceHeld ? Qt::OpenHandCursor : cursorAt(event->position()));
    }
    event->ignore();  // the page underneath reveals its panel at the edge
}

void ImageOverlay::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = true;
        setCursor(Qt::OpenHandCursor);
        return;
    }
    if (!keyPressed(event)) {
        event->ignore();
    }
}

void ImageOverlay::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        setCursor(Qt::CrossCursor);
        return;
    }
    event->ignore();
}

}  // namespace iv
