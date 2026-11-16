#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace iv {

// The track of a slider that picks a colour: the colours given, left to right, blended
// between, with what lies outside sRGB hatched. Stops are maps of {colour, outside}, as
// ColorChoice gives them.
class ColorRamp : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList stops READ stops WRITE setStops NOTIFY stopsChanged)
    Q_PROPERTY(QColor hatch READ hatch WRITE setHatch NOTIFY hatchChanged)  // from Theme
    Q_PROPERTY(double radius READ radius WRITE setRadius NOTIFY radiusChanged)

public:
    explicit ColorRamp(QQuickItem* parent = nullptr);

    QVariantList stops() const { return m_stops; }
    void setStops(const QVariantList& stops);
    QColor hatch() const { return m_hatch; }
    void setHatch(const QColor& hatch);
    double radius() const { return m_radius; }
    void setRadius(double radius);

    void paint(QPainter* painter) override;

signals:
    void stopsChanged();
    void hatchChanged();
    void radiusChanged();

private:
    QVariantList m_stops;
    QColor m_hatch{0, 0, 0, 90};
    double m_radius = 0;
};

}  // namespace iv
