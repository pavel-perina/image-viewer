#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "core/perspective.h"
#include "ui/image_overlay.h"

namespace iv {

// The lines that straightening makes vertical or horizontal, drawn over the image, with the
// outline of what will be kept, or the straightened picture instead. The left button drags a
// line's end or the whole line, or draws a new one; Delete removes the line chosen. V switches
// between the lines and the result.
class StraightenOverlay : public ImageOverlay {
    Q_OBJECT
    QML_ELEMENT
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
    // Off keeps the whole turned picture, with transparent corners.
    Q_PROPERTY(bool crop READ crop WRITE setCrop NOTIFY tiltChanged)
    Q_PROPERTY(bool showResult READ showResult WRITE setShowResult NOTIFY showResultChanged)
    // What the lines meant to be vertical and horizontal are drawn in.
    Q_PROPERTY(QColor verticalColor MEMBER m_verticalColor NOTIFY coloursChanged)
    Q_PROPERTY(QColor horizontalColor MEMBER m_horizontalColor NOTIFY coloursChanged)

public:
    explicit StraightenOverlay(QQuickItem* parent = nullptr);

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
    bool crop() const { return m_crop; }
    void setCrop(bool value);
    bool showResult() const { return m_showResult; }
    void setShowResult(bool value);

    Q_INVOKABLE void removeSelected();

    void paint(QPainter* painter) override;

signals:
    void linesChanged();
    void usedChanged();
    void selectedChanged();
    void tiltChanged();
    void showResultChanged();
    void coloursChanged();
    // The user drew, moved or removed a line, as opposed to lines being set.
    void linesEdited();

protected:
    bool usable() const override { return !m_showResult; }
    void viewAttached() override;
    void pressed(QPointF itemPoint) override;
    void dragged(QPointF itemPoint, bool moved) override;
    void released(QPointF itemPoint) override;
    Qt::CursorShape cursorAt(QPointF itemPoint) const override;
    bool keyPressed(QKeyEvent* event) override;

private:
    struct Hit {
        int line = -1;
        int end = -1;  // 0 or 1 for an end, -1 for the line itself
    };
    Hit hitAt(QPointF itemPoint) const;
    void select(int index);
    void tiltChange();

    std::vector<GuideLine> m_lines;
    QVariantList m_used;
    int m_selected = -1;
    Tilt m_tilt;
    double m_focal35mm = 0;
    bool m_keepRatio = true;
    bool m_crop = true;
    bool m_showResult = false;
    QColor m_verticalColor{79, 195, 247};
    QColor m_horizontalColor{255, 183, 77};
    mutable QImage m_preview;
    mutable bool m_previewValid = false;

    enum class Drag { None, End, Line, Draw };
    Drag m_drag = Drag::None;
    Hit m_hit;
    GuideLine m_atPress;
    PointD m_pressImage;
};

}  // namespace iv
