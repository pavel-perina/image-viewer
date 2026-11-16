#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace iv {

// The colour being chosen in a colour chooser, as R, G and B, as Oklch lightness, chroma and
// hue, and as hex, all linked: setting one sets the others. A gray keeps the hue last set, and
// lightness, chroma and hue stay as set even where sRGB cannot show them, the colour then
// having less chroma, so that a slider does not jump away from where it was dragged.
class ColorChoice : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QColor colour READ colour WRITE setColour NOTIFY changed)
    Q_PROPERTY(QString hex READ hex WRITE setHex NOTIFY changed)
    Q_PROPERTY(int red READ red WRITE setRed NOTIFY changed)
    Q_PROPERTY(int green READ green WRITE setGreen NOTIFY changed)
    Q_PROPERTY(int blue READ blue WRITE setBlue NOTIFY changed)
    Q_PROPERTY(double lightness READ lightness WRITE setLightness NOTIFY changed)  // 0 to 1
    Q_PROPERTY(double chroma READ chroma WRITE setChroma NOTIFY changed)           // 0 to maxChroma
    Q_PROPERTY(double hue READ hue WRITE setHue NOTIFY changed)                    // degrees
    Q_PROPERTY(double maxChroma READ maxChroma CONSTANT)
    // The lightness, chroma and hue set lie outside sRGB, and the colour has less chroma.
    Q_PROPERTY(bool outside READ outside NOTIFY changed)
    // For each channel, in the order of Channel, the stops of its slider's track: maps of
    // {colour, outside}, from the channel's least to its most, the others held.
    Q_PROPERTY(QVariantList ramps READ ramps NOTIFY changed)

public:
    enum Channel { Red, Green, Blue, Lightness, Chroma, Hue };
    Q_ENUM(Channel)
    static constexpr int kRampStops = 48;

    explicit ColorChoice(QObject* parent = nullptr);
    ~ColorChoice() override;

    QColor colour() const;
    void setColour(const QColor& colour);
    QString hex() const;
    void setHex(const QString& hex);  // with or without #; anything else is ignored
    int red() const;
    void setRed(int value);
    int green() const;
    void setGreen(int value);
    int blue() const;
    void setBlue(int value);
    double lightness() const;
    void setLightness(double value);
    double chroma() const;
    void setChroma(double value);
    double hue() const;
    void setHue(double value);
    double maxChroma() const;
    bool outside() const;
    QVariantList ramps() const;

signals:
    void changed();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace iv
