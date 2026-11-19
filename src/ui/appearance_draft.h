#pragma once

#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

#include "core/palette.h"

namespace iv {
class Preferences;
class ThemePalette;

// An Appearance editing session: preview without writing, Apply as a checkpoint,
// and rollback on close/destruction. One editor per palette, across all windows.
class AppearanceDraft : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double hue READ hue WRITE setHue NOTIFY changed)
    Q_PROPERTY(double accentChroma READ accentChroma WRITE setAccentChroma NOTIFY changed)
    Q_PROPERTY(double surfaceChroma READ surfaceChroma WRITE setSurfaceChroma NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
public:
    explicit AppearanceDraft(QObject* parent = nullptr);
    AppearanceDraft(Preferences& preferences, ThemePalette& theme, QObject* parent = nullptr);
    ~AppearanceDraft() override;
    double hue() const { return m_choices.hue; }
    double accentChroma() const { return m_choices.accentChroma; }
    double surfaceChroma() const { return m_choices.surfaceChroma; }
    void setHue(double value);
    void setAccentChroma(double value);
    void setSurfaceChroma(double value);
    bool dirty() const;
    Q_INVOKABLE bool begin();
    Q_INVOKABLE void apply();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void resetDefaults();
signals:
    void changed();
    void activationRequested();
private:
    void refresh();
    void publish();
    Preferences& m_preferences;
    ThemePalette& m_theme;
    PaletteChoices m_choices;
    PaletteChoices m_saved;
    bool m_active = false;
    bool m_applying = false;
};
} // namespace iv
