#pragma once

#include <QPointer>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

#include "core/geometry.h"
#include "ui/image_view.h"
#include "ui/overlay_style.h"

namespace iv {

// What every tool drawn over an ImageView of the same geometry shares: the view and its
// coordinates, moving the picture, and telling a drag from a click. The right or middle
// button, or the left one with Space held, pans, and never reaches the tool; the wheel goes
// to the view underneath. A tool keeps its own model, in pixels of the image as shown, and
// draws it in paint(), with the lines and grips here where it wants the common look.
class ImageOverlay : public QQuickPaintedItem {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(iv::ImageView* view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(iv::OverlayStyle* style READ styleObject WRITE setStyle NOTIFY styleChanged)

public:
    explicit ImageOverlay(QQuickItem* parent = nullptr);

    ImageView* view() const { return m_view; }
    void setView(ImageView* view);
    OverlayStyle* styleObject() const { return m_style; }
    void setStyle(OverlayStyle* style);

signals:
    void viewChanged();
    void styleChanged();

protected:
    // Between pixels of the image as shown and this item.
    PointD toImage(QPointF itemPoint) const;
    QPointF toItem(PointD imagePoint) const;
    // The same on a whole device pixel, for edges that sit exactly between image pixels.
    QPointF toItemSnapped(PointD imagePoint) const;
    double snapped(double itemLength) const;
    // The size of an image pixel, and of a device pixel, in item units.
    double itemPerPixel() const;
    double devicePixel() const;
    // Whether Shift was down at the last press or move of the mouse, which tools take as
    // "keep it even": a square, a circle, a line at a round angle.
    bool shiftHeld() const { return m_shiftHeld; }
    // How far from something still grabs it, in item units.
    static constexpr double kReach = 8;

    // The colours to draw with: those given, or plain ones.
    const OverlayStyle& style() const;
    // A thin line of the colour over a wider dark one. A chosen line is heavier.
    struct Stroke {
        bool chosen = false;
        bool dashed = false;
    };
    void strokeLine(QPainter* painter, QPointF a, QPointF b, const QColor& colour, Stroke stroke) const;
    // A round grip to drag: filled with the colour, with the line colour when chosen, and
    // hollow for something set aside.
    struct Grip {
        bool chosen = false;
        bool hollow = false;
    };
    void drawGrip(QPainter* painter, QPointF at, const QColor& colour, Grip grip) const;

    // For a tool to fill in.
    // Whether it takes the mouse now. Panning waits for this too.
    virtual bool usable() const { return true; }
    // The view was set, or taken away: the place to connect to more of its signals.
    virtual void viewAttached() {}
    // The left button went down, moved while down, and came up. `moved` turns true once the
    // mouse is far enough from the press to mean a drag and not a shaking hand, and stays so.
    virtual void pressed(QPointF /*itemPoint*/) {}
    virtual void dragged(QPointF /*itemPoint*/, bool /*moved*/) {}
    virtual void released(QPointF /*itemPoint*/) {}
    // The cursor over a point while no button is down.
    virtual Qt::CursorShape cursorAt(QPointF /*itemPoint*/) const { return Qt::CrossCursor; }
    // A key other than Space; true if it was the tool's.
    virtual bool keyPressed(QKeyEvent* /*event*/) { return false; }

    void mousePressEvent(QMouseEvent* event) final;
    void mouseMoveEvent(QMouseEvent* event) final;
    void mouseReleaseEvent(QMouseEvent* event) final;
    void hoverMoveEvent(QHoverEvent* event) final;
    void keyPressEvent(QKeyEvent* event) final;
    void keyReleaseEvent(QKeyEvent* event) final;

private:
    QPointer<ImageView> m_view;
    QPointer<OverlayStyle> m_style;
    enum class Mouse { Up, Tool, Pan };
    Mouse m_mouse = Mouse::Up;
    QPointF m_pressItem;
    QPointF m_lastItem;
    bool m_moved = false;
    bool m_spaceHeld = false;
    bool m_shiftHeld = false;
};

}  // namespace iv
