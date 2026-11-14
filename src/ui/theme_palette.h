#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "core/palette.h"

namespace iv {

// The choices a palette is worked out from (core/palette.h), and the colours they give, for
// Theme to bind to. Changing a choice changes every colour bound to it, at once.
class ThemePalette : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(double hue READ hue WRITE setHue NOTIFY changed)
    Q_PROPERTY(double accentChroma READ accentChroma WRITE setAccentChroma NOTIFY changed)
    Q_PROPERTY(double surfaceChroma READ surfaceChroma WRITE setSurfaceChroma NOTIFY changed)
    // Every role by name: "window", "accent", ...
    Q_PROPERTY(QVariantMap colours READ colours NOTIFY changed)

public:
    explicit ThemePalette(QObject* parent = nullptr);

    double hue() const { return m_choices.hue; }
    void setHue(double hue);
    double accentChroma() const { return m_choices.accentChroma; }
    void setAccentChroma(double chroma);
    double surfaceChroma() const { return m_choices.surfaceChroma; }
    void setSurfaceChroma(double chroma);
    QVariantMap colours() const { return m_colours; }

signals:
    void changed();

private:
    void update();

    PaletteChoices m_choices;
    QVariantMap m_colours;
};

}  // namespace iv
