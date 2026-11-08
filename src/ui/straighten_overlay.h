#pragma once

#include <QImage>
#include <QPointer>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "core/perspective.h"
#include "ui/image_view.h"

namespace iv {

// The lines that straightening makes vertical or horizontal, drawn over an ImageView of the
// same geometry, with the outline of what will be kept, or the straightened picture instead.
// The left button drags a line's end or the whole line, or draws a new one; Delete removes the
// line chosen. The right or middle button, or the left one with Space held, pans; the wheel
// goes to the view underneath. V switches between the lines and the result.
class StraightenOverlay : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(iv::ImageView* view READ view WRITE setView NOTIFY viewChanged)
    // {x1, y1, x2, y2} in pixels of the image as shown, and drawn for one the user drew or moved.
    Q_PROPERTY(QVariantList lines READ lines WRITE setLines NOTIFY linesChanged)
    // Which lines the angles were fitted to; those left out are drawn faint.
    Q_PROPERTY(QVariantList used READ used WRITE setUsed NOTIFY usedChanged)
    Q_PROPERTY(int selected READ selected NOTIFY selectedChanged)
    // The straightening to outline or show.
    Q_PROPERTY(double roll READ roll WRITE setRoll NOTIFY tiltChanged)
    Q_PROPERTY(double pitch READ pitch WRITE setPitch NOTIFY tiltChanged)
    Q_PROPERTY(double yaw READ yaw WRITE setYaw NOTIFY tiltChanged)
    Q_PROPERTY(double focal35mm READ focal35mm WRITE setFocal35mm NOTIFY tiltChanged)
    Q_PROPERTY(bool keepRatio READ keepRatio WRITE setKeepRatio NOTIFY tiltChanged)
    Q_PROPERTY(bool showResult READ showResult WRITE setShowResult NOTIFY showResultChanged)

public:
    explicit StraightenOverlay(QQuickItem* parent = nullptr);

    ImageView* view() const { return m_view; }
    void setView(ImageView* view);
    QVariantList lines() const;
    void setLines(const QVariantList& lines);
    QVariantList used() const { return m_used; }
    void setUsed(const QVariantList& used);
    int selected() const { return m_selected; }
    double roll() const { return m_tilt.roll; }
    void setRoll(double value);
    double pitch() const { return m_tilt.pitch; }
    void setPitch(double value);
    double yaw() const { return m_tilt.yaw; }
    void setYaw(double value);
    double focal35mm() const { return m_focal35mm; }
    void setFocal35mm(double value);
    bool keepRatio() const { return m_keepRatio; }
    void setKeepRatio(bool value);
    bool showResult() const { return m_showResult; }
    void setShowResult(bool value);

    Q_INVOKABLE void removeSelected();

    void paint(QPainter* painter) override;

signals:
    void viewChanged();
    void linesChanged();
    void usedChanged();
    void selectedChanged();
    void tiltChanged();
    void showResultChanged();
    // The user drew, moved or removed a line, as opposed to lines being set.
    void linesEdited();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    struct Hit {
        int line = -1;
        int end = -1;  // 0 or 1 for an end, -1 for the line itself
    };
    Hit hitAt(QPointF itemPoint) const;
    QPointF toItem(PointD imagePoint) const;
    PointD toImage(QPointF itemPoint) const;
    void select(int index);
    void tiltChange();

    QPointer<ImageView> m_view;
    std::vector<GuideLine> m_lines;
    QVariantList m_used;
    int m_selected = -1;
    Tilt m_tilt;
    double m_focal35mm = 0;
    bool m_keepRatio = true;
    bool m_showResult = false;
    mutable QImage m_preview;
    mutable bool m_previewValid = false;

    enum class Drag { None, End, Line, Draw, Pan };
    Drag m_drag = Drag::None;
    Hit m_hit;
    GuideLine m_atPress;
    PointD m_pressImage;
    QPointF m_pressItem;
    QPointF m_lastItem;
    bool m_spaceHeld = false;
};

}  // namespace iv
