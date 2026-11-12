#pragma once

#include <QPointF>
#include <QRect>
#include <QtQml/qqmlregistration.h>

#include "core/crop.h"
#include "ui/image_overlay.h"

namespace iv {

// The crop rectangle drawn over the image, with its handles, a rule of thirds grid and the rest
// dimmed. The rectangle is in whole pixels of the image as shown. The left button moves or
// resizes it, or draws it anew inside the image; a press outside the image takes the edge or
// corner on that side.
class CropOverlay : public ImageOverlay {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QRect cropRect READ cropRect WRITE setCropRect NOTIFY cropRectChanged)
    // Width to height, such as 3 and 2; zero in either is free.
    Q_PROPERTY(int aspectWidth READ aspectWidth NOTIFY aspectChanged)
    Q_PROPERTY(int aspectHeight READ aspectHeight NOTIFY aspectChanged)
    Q_PROPERTY(bool grid READ grid WRITE setGrid NOTIFY gridChanged)

public:
    explicit CropOverlay(QQuickItem* parent = nullptr);

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
    void cropRectChanged();
    void aspectChanged();
    void gridChanged();

protected:
    bool usable() const override { return m_image.width > 0; }
    void viewAttached() override;
    void pressed(QPointF itemPoint) override;
    void dragged(QPointF itemPoint, bool moved) override;
    void released(QPointF itemPoint) override;
    Qt::CursorShape cursorAt(QPointF itemPoint) const override;

private:
    Size imageSize() const;
    void changeRect(const PixelRect& rect);
    void imageChanged();

    Size m_image;
    PixelRect m_rect;
    AspectRatio m_aspect;
    bool m_grid = true;

    enum class Drag { None, Handle, Draw };
    Drag m_drag = Drag::None;
    CropHandle m_handle = CropHandle::None;
    PixelRect m_atPress;
    PointD m_pressImage;
};

}  // namespace iv
