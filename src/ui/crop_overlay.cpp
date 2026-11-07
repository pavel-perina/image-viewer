#include "ui/crop_overlay.h"

#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QQuickWindow>

#include <cmath>

#include "ui/image_view.h"

namespace iv {

namespace {

constexpr double kReach = 8;       // how far from an edge grabs it, in item units
constexpr double kHandleSize = 8;  // drawn handles, in item units
constexpr double kDrawAfter = 4;   // how far the mouse moves before a new rectangle is drawn

Qt::CursorShape cursorFor(CropHandle handle) {
    switch (handle) {
    case CropHandle::Left:
    case CropHandle::Right: return Qt::SizeHorCursor;
    case CropHandle::Top:
    case CropHandle::Bottom: return Qt::SizeVerCursor;
    case CropHandle::TopLeft:
    case CropHandle::BottomRight: return Qt::SizeFDiagCursor;
    case CropHandle::TopRight:
    case CropHandle::BottomLeft: return Qt::SizeBDiagCursor;
    case CropHandle::Inside: return Qt::SizeAllCursor;
    case CropHandle::None: break;
    }
    return Qt::CrossCursor;
}

}  // namespace

CropOverlay::CropOverlay(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setFlag(ItemIsFocusScope, false);
    setActiveFocusOnTab(false);
    setCursor(Qt::CrossCursor);
}

void CropOverlay::setView(ImageView* view) {
    if (view == m_view) {
        return;
    }
    if (m_view) {
        disconnect(m_view, nullptr, this, nullptr);
    }
    m_view = view;
    if (m_view) {
        connect(m_view, &ImageView::zoomChanged, this, [this] { update(); });
        connect(m_view, &ImageView::statusChanged, this, &CropOverlay::imageChanged);
        connect(m_view, &ImageView::pageChanged, this, &CropOverlay::imageChanged);
    }
    imageChanged();
    emit viewChanged();
}

QRect CropOverlay::cropRect() const {
    return {m_rect.x, m_rect.y, m_rect.width, m_rect.height};
}

void CropOverlay::setCropRect(const QRect& rect) {
    const Size image = imageSize();
    PixelRect r{rect.x(), rect.y(), rect.width(), rect.height()};
    r = withWidth(r, r.width, image, {});
    r = withHeight(r, r.height, image, {});
    changeRect(withPosition(r, r.x, r.y, image));
}

void CropOverlay::setGrid(bool grid) {
    if (grid != m_grid) {
        m_grid = grid;
        update();
        emit gridChanged();
    }
}

void CropOverlay::setAspect(int width, int height) {
    const AspectRatio aspect = width > 0 && height > 0 ? AspectRatio{width, height} : AspectRatio{};
    if (aspect.width == m_aspect.width && aspect.height == m_aspect.height) {
        return;
    }
    m_aspect = aspect;
    emit aspectChanged();
    changeRect(fitAspect(m_rect, m_aspect));
}

void CropOverlay::reset() {
    const Size image = imageSize();
    changeRect(fitAspect({0, 0, image.width, image.height}, m_aspect));
}

void CropOverlay::setPosition(int x, int y) {
    changeRect(withPosition(m_rect, x, y, imageSize()));
}

void CropOverlay::setWidth(int width) {
    changeRect(withWidth(m_rect, width, imageSize(), m_aspect));
}

void CropOverlay::setHeight(int height) {
    changeRect(withHeight(m_rect, height, imageSize(), m_aspect));
}

Size CropOverlay::imageSize() const {
    return m_view ? Size{m_view->imageWidth(), m_view->imageHeight()} : Size{};
}

double CropOverlay::itemPerPixel() const {
    return m_view ? m_view->imageToItem({1, 0}).x() - m_view->imageToItem({0, 0}).x() : 1;
}

PointD CropOverlay::toImage(QPointF itemPoint) const {
    const QPointF p = m_view ? m_view->itemToImage(mapToItem(m_view, itemPoint)) : itemPoint;
    return {p.x(), p.y()};
}

void CropOverlay::changeRect(const PixelRect& rect) {
    if (rect == m_rect) {
        return;
    }
    m_rect = rect;
    update();
    emit cropRectChanged();
}

// A new image, or the same one edited, starts with all of it.
void CropOverlay::imageChanged() {
    const Size image = imageSize();
    if (image == m_image) {
        update();
        return;
    }
    m_image = image;
    m_drag = Drag::None;
    reset();
    update();
}

void CropOverlay::updateCursor(CropHandle handle) {
    setCursor(cursorFor(handle));
}

void CropOverlay::mousePressEvent(QMouseEvent* event) {
    if (!m_view || m_image.width <= 0) {
        event->ignore();
        return;
    }
    forceActiveFocus();  // for Space
    m_lastItem = event->position();
    if (event->button() != Qt::LeftButton || m_spaceHeld) {
        m_drag = Drag::Pan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    m_pressImage = toImage(event->position());
    m_atPress = m_rect;
    m_pressItem = event->position();
    m_handle = cropHandleAt(m_rect, m_pressImage, kReach / itemPerPixel());
    const bool outsideImage = m_pressImage.x < 0 || m_pressImage.y < 0 || m_pressImage.x > m_image.width ||
                              m_pressImage.y > m_image.height;
    if (m_handle == CropHandle::None && outsideImage) {
        m_handle = cropHandleToward(m_rect, m_pressImage);  // nothing to draw on out there
    }
    m_drag = m_handle == CropHandle::None ? Drag::Draw : Drag::Handle;
}

void CropOverlay::mouseMoveEvent(QMouseEvent* event) {
    const QPointF item = event->position();
    switch (m_drag) {
    case Drag::None:
        return;
    case Drag::Pan:
        m_view->panBy(item.x() - m_lastItem.x(), item.y() - m_lastItem.y());
        break;
    case Drag::Handle: {
        const PointD now = toImage(item);
        changeRect(dragCrop(m_atPress, m_handle, {now.x - m_pressImage.x, now.y - m_pressImage.y}, m_image, m_aspect));
        break;
    }
    case Drag::Draw:
        // A click, or a hand that shakes a little, leaves the rectangle alone.
        if (std::hypot(item.x() - m_pressItem.x(), item.y() - m_pressItem.y()) >= kDrawAfter || m_rect != m_atPress) {
            changeRect(drawCrop(m_pressImage, toImage(item), m_image, m_aspect));
        }
        break;
    }
    m_lastItem = item;
}

// A click without a drag changes nothing, as only moving draws.
void CropOverlay::mouseReleaseEvent(QMouseEvent* event) {
    m_drag = Drag::None;
    updateCursor(cropHandleAt(m_rect, toImage(event->position()), kReach / itemPerPixel()));
}

void CropOverlay::hoverMoveEvent(QHoverEvent* event) {
    if (m_drag == Drag::None && m_view) {
        updateCursor(m_spaceHeld ? CropHandle::None : cropHandleAt(m_rect, toImage(event->position()), kReach / itemPerPixel()));
        if (m_spaceHeld) {
            setCursor(Qt::OpenHandCursor);
        }
    }
    event->ignore();  // the page underneath reveals its panel at the edge
}

void CropOverlay::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = true;
        setCursor(Qt::OpenHandCursor);
        return;
    }
    event->ignore();
}

void CropOverlay::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        setCursor(Qt::CrossCursor);
        return;
    }
    event->ignore();
}

void CropOverlay::paint(QPainter* painter) {
    if (!m_view || m_image.width <= 0 || m_rect.width <= 0) {
        return;
    }
    // Edges on device pixels, so that at a high zoom they sit exactly between image pixels.
    const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const auto snap = [dpr](double v) { return std::round(v * dpr) / dpr; };
    const auto toItem = [&](double x, double y) {
        const QPointF p = mapFromItem(m_view, m_view->imageToItem({x, y}));
        return QPointF(snap(p.x()), snap(p.y()));
    };
    const QRectF crop(toItem(m_rect.x, m_rect.y), toItem(m_rect.right(), m_rect.bottom()));
    const QRectF all = boundingRect();

    painter->setRenderHint(QPainter::Antialiasing, false);
    const QColor dim(0, 0, 0, 150);
    painter->fillRect(QRectF(all.left(), all.top(), all.width(), crop.top() - all.top()), dim);
    painter->fillRect(QRectF(all.left(), crop.bottom(), all.width(), all.bottom() - crop.bottom()), dim);
    painter->fillRect(QRectF(all.left(), crop.top(), crop.left() - all.left(), crop.height()), dim);
    painter->fillRect(QRectF(crop.right(), crop.top(), all.right() - crop.right(), crop.height()), dim);

    const double line = 1 / dpr;
    if (m_grid) {
        painter->setPen(QPen(QColor(255, 255, 255, 110), line));
        for (int i = 1; i < 3; ++i) {
            const double x = snap(crop.left() + crop.width() * i / 3);
            const double y = snap(crop.top() + crop.height() * i / 3);
            painter->drawLine(QPointF(x, crop.top()), QPointF(x, crop.bottom()));
            painter->drawLine(QPointF(crop.left(), y), QPointF(crop.right(), y));
        }
    }

    // Light inside, dark outside, so that the edge shows on any picture.
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor(0, 0, 0, 200), line));
    painter->drawRect(crop.adjusted(-line, -line, line, line));
    painter->setPen(QPen(Qt::white, line));
    painter->drawRect(crop.adjusted(0, 0, -line, -line));

    // Handles where there is room for them; a tiny rectangle keeps its corners only.
    const bool roomy = crop.width() > 4 * kHandleSize && crop.height() > 4 * kHandleSize;
    const double h = kHandleSize;
    painter->setPen(QPen(QColor(0, 0, 0, 200), line));
    painter->setBrush(Qt::white);
    const auto handle = [&](double x, double y) { painter->drawRect(QRectF(snap(x - h / 2), snap(y - h / 2), h, h)); };
    handle(crop.left(), crop.top());
    handle(crop.right(), crop.top());
    handle(crop.left(), crop.bottom());
    handle(crop.right(), crop.bottom());
    if (roomy) {
        handle(crop.center().x(), crop.top());
        handle(crop.center().x(), crop.bottom());
        handle(crop.left(), crop.center().y());
        handle(crop.right(), crop.center().y());
    }
}

}  // namespace iv
