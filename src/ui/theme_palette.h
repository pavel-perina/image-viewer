#pragma once

#include <QObject>
#include <QPalette>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace iv {

class Preferences;

// The choices a palette is worked out from (core/palette.h), and the colours they give, for
// Theme to bind to. Changing a choice changes every colour bound to it, at once. The roles Qt's
// controls draw with are set as the application's palette, so that every window has them,
// the main one and dialogs alike, without each giving them again.
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
    explicit ThemePalette(Preferences& preferences, QObject* parent = nullptr);

    double hue() const;
    void setHue(double hue);
    double accentChroma() const;
    void setAccentChroma(double chroma);
    double surfaceChroma() const;
    void setSurfaceChroma(double chroma);
    QVariantMap colours() const { return m_colours; }
    // The palette of Qt's roles (window, base, highlight, ...) from the colours.
    QPalette palette() const;

signals:
    void changed();

private:
    void update();

    Preferences* m_preferences;
    QVariantMap m_colours;
};

}  // namespace iv
