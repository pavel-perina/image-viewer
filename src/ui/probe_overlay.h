#pragma once

#include <QPoint>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "ui/image_overlay.h"

namespace iv {

// Reads single pixels of the image: the one under a press and while dragging, and the next
// one with the arrow keys. A frame marks the pixel read, never smaller than a few screen
// pixels. A click, or Enter, picks the pixel read, for the panel to remember.
class ProbeOverlay : public ImageOverlay {
    Q_OBJECT
    QML_ELEMENT
    // What ImageView::probed reported last, or null.
    Q_PROPERTY(QVariant pixel READ pixel NOTIFY pixelChanged)

public:
    explicit ProbeOverlay(QQuickItem* parent = nullptr);

    QVariant pixel() const;
    // Forgets the pixel read, which may be elsewhere in another image.
    Q_INVOKABLE void clear();

    void paint(QPainter* painter) override;

signals:
    void pixelChanged();
    void picked(const QVariantMap& pixel);

protected:
    void viewAttached() override;
    void pressed(QPointF itemPoint) override;
    void dragged(QPointF itemPoint, bool moved) override;
    void released(QPointF itemPoint) override;
    bool keyPressed(QKeyEvent* event) override;

private:
    void probeAt(PointD imagePoint);
    void pickIfRead();

    QVariantMap m_pixel;      // empty for none
    QPoint m_requested{-1, -1};  // the pixel asked for last
    bool m_pickWhenRead = false;
};

}  // namespace iv
