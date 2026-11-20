#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace iv {

// How a slider spreads values over its length, so that the same distance makes about the same
// change everywhere: evenly; evenly in their reciprocal, as temperature is in mireds; or evenly
// in their logarithm, as an exponent from 1/4 to 4 is either side of 1. The slider runs from 0
// at `from` to 1 at `to`; values it gives are rounded to `step`.
class SliderScale : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Kind kind READ kind WRITE setKind NOTIFY changed)
    Q_PROPERTY(double from READ from WRITE setFrom NOTIFY changed)
    Q_PROPERTY(double to READ to WRITE setTo NOTIFY changed)
    Q_PROPERTY(double step READ step WRITE setStep NOTIFY changed)
    // A value to place, and where it lies, for binding a slider to.
    Q_PROPERTY(double value READ value WRITE setValue NOTIFY changed)
    Q_PROPERTY(double position READ position NOTIFY changed)

public:
    enum class Kind { Linear, Reciprocal, Logarithmic };
    Q_ENUM(Kind)

    explicit SliderScale(QObject* parent = nullptr);

    Kind kind() const { return m_kind; }
    void setKind(Kind kind);
    double from() const { return m_from; }
    void setFrom(double from);
    double to() const { return m_to; }
    void setTo(double to);
    double step() const { return m_step; }
    void setStep(double step);
    double value() const { return m_value; }
    void setValue(double value);
    double position() const { return positionOf(m_value); }

    // Where a value lies, from 0 to 1, clamped.
    Q_INVOKABLE double positionOf(double value) const;
    // The value at a place, rounded to the step and kept within from and to.
    Q_INVOKABLE double valueAt(double position) const;

signals:
    void changed();

private:
    double warp(double value) const;
    double unwarp(double warped) const;

    Kind m_kind = Kind::Linear;
    double m_from = 0;
    double m_to = 1;
    double m_step = 0;
    double m_value = 0;
};

}  // namespace iv
