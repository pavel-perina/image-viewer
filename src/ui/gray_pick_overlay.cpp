#include "ui/gray_pick_overlay.h"


#include <algorithm>
#include <cmath>

namespace iv {

GrayPickOverlay::GrayPickOverlay(QQuickItem* parent) : ImageOverlay(parent) {}

void GrayPickOverlay::setPatchSize(int size) {
    size = std::max(1, size | 1);
    if (size != m_patchSize) {
        m_patchSize = size;
        emit patchSizeChanged();
    }
}

void GrayPickOverlay::clear() {
    m_dragging = false;
    setPatch({});
}

void GrayPickOverlay::setPatch(const QRectF& patch) {
    if (patch != m_patch) {
        m_patch = patch;
        update();
        emit patchChanged();
    }
}

QRectF GrayPickOverlay::dragRect(QPointF itemPoint) const {
    const PointD at = toImage(itemPoint);
    const double left = std::floor(std::min(m_press.x, at.x));
    const double top = std::floor(std::min(m_press.y, at.y));
    const double right = std::floor(std::max(m_press.x, at.x)) + 1;
    const double bottom = std::floor(std::max(m_press.y, at.y)) + 1;
    return {left, top, right - left, bottom - top};
}

void GrayPickOverlay::pressed(QPointF itemPoint) {
    m_press = toImage(itemPoint);
    m_dragging = false;
}

void GrayPickOverlay::dragged(QPointF itemPoint, bool moved) {
    if (moved) {
        m_dragging = true;
        setPatch(dragRect(itemPoint));
    }
}

void GrayPickOverlay::released(QPointF itemPoint) {
    if (!view()) {
        return;
    }
    if (m_dragging) {
        setPatch(dragRect(itemPoint));
    } else {
        const double half = (m_patchSize - 1) / 2.0;
        setPatch({std::floor(m_press.x) - half, std::floor(m_press.y) - half, double(m_patchSize), double(m_patchSize)});
    }
    m_dragging = false;
    view()->pickGray(m_patch);
}

void GrayPickOverlay::paint(QPainter* painter) {
    if (m_patch.isEmpty() || !view()) {
        return;
    }
    drawFrame(painter, m_patch);
}

}  // namespace iv
