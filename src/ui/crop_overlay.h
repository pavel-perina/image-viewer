#pragma once

#include <QPointer>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QRect>
#include <QtQml/qqmlregistration.h>

#include "core/crop.h"
#include "ui/image_view.h"

namespace iv {

// The crop rectangle drawn over an ImageView of the same geometry, with its handles, a rule of
// thirds grid and the rest dimmed. The rectangle is in whole pixels of the image as shown.
// The left button moves or resizes it, or draws it anew inside the image; a press outside the
// image takes the edge or corner on that side. The right or middle button, or the left one with
// Space held, pans; the wheel goes to the view underneath.
class CropOverlay : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(iv::ImageView* view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(QRect cropRect READ cropRect WRITE setCropRect NOTIFY cropRectChanged)
    // Width to height, such as 3 and 2; zero in either is free.
    Q_PROPERTY(int aspectWidth READ aspectWidth NOTIFY aspectChanged)
    Q_PROPERTY(int aspectHeight READ aspectHeight NOTIFY aspectChanged)
    Q_PROPERTY(bool grid READ grid WRITE setGrid NOTIFY gridChanged)

public:
    explicit CropOverlay(QQuickItem* parent = nullptr);

    ImageView* view() const { return m_view; }
    void setView(ImageView* view);
    QRect cropRect() const;
    void setCropRect(const QRect& rect);
    int aspectWidth() const { return m_aspect.width; }
    int aspectHeight() const { return m_aspect.height; }
    bool grid() const { return m_grid; }
    void setGrid(bool grid);

    // Fits the largest rectangle of the ratio inside the current one.
    Q_INVOKABLE void setAspect(int width, int height);
    // The whole image, or the largest part of it with the ratio, centred.
    Q_INVOKABLE void reset();
    Q_INVOKABLE void setPosition(int x, int y);
    Q_INVOKABLE void setWidth(int width);
    Q_INVOKABLE void setHeight(int height);

    void paint(QPainter* painter) override;

signals:
    void viewChanged();
    void cropRectChanged();
    void aspectChanged();
    void gridChanged();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    Size imageSize() const;
    double itemPerPixel() const;
    PointD toImage(QPointF itemPoint) const;
    void changeRect(const PixelRect& rect);
    void imageChanged();
    void updateCursor(CropHandle handle);

    QPointer<ImageView> m_view;
    Size m_image;
    PixelRect m_rect;
    AspectRatio m_aspect;
    bool m_grid = true;

    enum class Drag { None, Handle, Draw, Pan };
    Drag m_drag = Drag::None;
    CropHandle m_handle = CropHandle::None;
    PixelRect m_atPress;
    PointD m_pressImage;
    QPointF m_lastItem;
    QPointF m_pressItem;
    bool m_spaceHeld = false;
};

}  // namespace iv
