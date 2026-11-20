#pragma once

#include <QRectF>
#include <QtQml/qqmlregistration.h>

#include "ui/image_overlay.h"

namespace iv {

// Picks something that should be neutral, for white balance: a click takes a square patch of
// patchSize pixels centred on the pixel under it, a drag the rectangle dragged. The patch is
// outlined while dragging and after, and handed to ImageView::pickGray, which reports what it
// found. The probe stays as it is, reading one pixel.
class GrayPickOverlay : public ImageOverlay {
    Q_OBJECT
    QML_ELEMENT
    // Across, in pixels of the image as shown: odd, so that the patch is centred on a pixel.
    Q_PROPERTY(int patchSize READ patchSize WRITE setPatchSize NOTIFY patchSizeChanged)
    // The patch picked or being dragged, in pixels of the image as shown; empty for none.
    Q_PROPERTY(QRectF patch READ patch NOTIFY patchChanged)

public:
    explicit GrayPickOverlay(QQuickItem* parent = nullptr);

    int patchSize() const { return m_patchSize; }
    void setPatchSize(int size);
    QRectF patch() const { return m_patch; }
    // Forgets the patch, which may be elsewhere in another image.
    Q_INVOKABLE void clear();

    void paint(QPainter* painter) override;

signals:
    void patchSizeChanged();
    void patchChanged();

protected:
    void pressed(QPointF itemPoint) override;
    void dragged(QPointF itemPoint, bool moved) override;
    void released(QPointF itemPoint) override;

private:
    void setPatch(const QRectF& patch);
    // The whole pixels from where the press was to a point, both included.
    QRectF dragRect(QPointF itemPoint) const;

    int m_patchSize = 5;
    QRectF m_patch;
    PointD m_press;
    bool m_dragging = false;
};

}  // namespace iv
