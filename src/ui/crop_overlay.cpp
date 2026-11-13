#include "ui/crop_overlay.h"

#include <QPainter>

#include <cmath>

namespace iv {

namespace {

constexpr double kHandleSize = 8;  // drawn handles, in item units

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

CropOverlay::CropOverlay(QQuickItem* parent) : ImageOverlay(parent) {}

void CropOverlay::viewAttached() {
    if (view()) {
        connect(view(), &ImageView::statusChanged, this, &CropOverlay::imageChanged);
        connect(view(), &ImageView::pageChanged, this, &CropOverlay::imageChanged);
    }
    imageChanged();
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
    return view() ? Size{view()->imageWidth(), view()->imageHeight()} : Size{};
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

void CropOverlay::pressed(QPointF itemPoint) {
    m_pressImage = toImage(itemPoint);
    m_atPress = m_rect;
    m_handle = cropHandleAt(m_rect, m_pressImage, kReach / itemPerPixel());
    const bool outsideImage = m_pressImage.x < 0 || m_pressImage.y < 0 || m_pressImage.x > m_image.width ||
                              m_pressImage.y > m_image.height;
    if (m_handle == CropHandle::None && outsideImage) {
        m_handle = cropHandleToward(m_rect, m_pressImage);  // nothing to draw on out there
    }
    m_drag = m_handle == CropHandle::None ? Drag::Draw : Drag::Handle;
}

void CropOverlay::dragged(QPointF itemPoint, bool moved) {
    const PointD now = toImage(itemPoint);
    if (m_drag == Drag::Handle) {
        changeRect(dragCrop(m_atPress, m_handle, {now.x - m_pressImage.x, now.y - m_pressImage.y}, m_image, m_aspect));
    } else if (m_drag == Drag::Draw && moved) {
        // A click, or a hand that shakes a little, leaves the rectangle alone.
        changeRect(drawCrop(m_pressImage, now, m_image, m_aspect));
    }
}

void CropOverlay::released(QPointF) {
    m_drag = Drag::None;
}

Qt::CursorShape CropOverlay::cursorAt(QPointF itemPoint) const {
    return cursorFor(cropHandleAt(m_rect, toImage(itemPoint), kReach / itemPerPixel()));
}

void CropOverlay::paint(QPainter* painter) {
    if (!view() || m_image.width <= 0 || m_rect.width <= 0) {
        return;
    }
    // Edges on device pixels, so that at a high zoom they sit exactly between image pixels.
    const auto snap = [this](double v) { return snapped(v); };
    const auto corner = [this](int x, int y) { return toItemSnapped({double(x), double(y)}); };
    const QRectF crop(corner(m_rect.x, m_rect.y), corner(m_rect.right(), m_rect.bottom()));
    const QRectF all = boundingRect();

    painter->setRenderHint(QPainter::Antialiasing, false);
    const QColor dim = style().dim();
    painter->fillRect(QRectF(all.left(), all.top(), all.width(), crop.top() - all.top()), dim);
    painter->fillRect(QRectF(all.left(), crop.bottom(), all.width(), all.bottom() - crop.bottom()), dim);
    painter->fillRect(QRectF(all.left(), crop.top(), crop.left() - all.left(), crop.height()), dim);
    painter->fillRect(QRectF(crop.right(), crop.top(), all.right() - crop.right(), crop.height()), dim);

    const double line = devicePixel();
    if (m_grid) {
        QColor faint = style().line();
        faint.setAlphaF(faint.alphaF() * 0.43F);
        painter->setPen(QPen(faint, line));
        for (int i = 1; i < 3; ++i) {
            const double x = snap(crop.left() + crop.width() * i / 3);
            const double y = snap(crop.top() + crop.height() * i / 3);
            painter->drawLine(QPointF(x, crop.top()), QPointF(x, crop.bottom()));
            painter->drawLine(QPointF(crop.left(), y), QPointF(crop.right(), y));
        }
    }

    // Light inside, dark outside, so that the edge shows on any picture.
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(style().outline(), line));
    painter->drawRect(crop.adjusted(-line, -line, line, line));
    painter->setPen(QPen(style().line(), line));
    painter->drawRect(crop.adjusted(0, 0, -line, -line));

    // Handles where there is room for them; a tiny rectangle keeps its corners only.
    const bool roomy = crop.width() > 4 * kHandleSize && crop.height() > 4 * kHandleSize;
    const double h = kHandleSize;
    painter->setPen(QPen(style().outline(), line));
    painter->setBrush(style().line());
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
