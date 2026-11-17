#include "ui/theme_palette.h"

#include <QColor>
#include <QGuiApplication>

#include "ui/preferences.h"
#include "core/palette.h"

namespace iv {

ThemePalette::ThemePalette(QObject* parent) : ThemePalette(*Preferences::instance(), parent) {}

ThemePalette::ThemePalette(Preferences& preferences, QObject* parent) : QObject(parent), m_preferences(&preferences) {
    connect(&preferences, &Preferences::hueChanged, this, &ThemePalette::update);
    connect(&preferences, &Preferences::accentChromaChanged, this, &ThemePalette::update);
    connect(&preferences, &Preferences::surfaceChromaChanged, this, &ThemePalette::update);
    update();
}

double ThemePalette::hue() const { return m_preferences->hue(); }
double ThemePalette::accentChroma() const { return m_preferences->accentChroma(); }
double ThemePalette::surfaceChroma() const { return m_preferences->surfaceChroma(); }

void ThemePalette::setHue(double hue) {
    m_preferences->setHue(hue);
}

void ThemePalette::setAccentChroma(double chroma) {
    m_preferences->setAccentChroma(chroma);
}

void ThemePalette::setSurfaceChroma(double chroma) {
    m_preferences->setSurfaceChroma(chroma);
}

void ThemePalette::update() {
    m_colours.clear();
    for (const PaletteRole& role : derivePalette({.hue = hue(), .accentChroma = accentChroma(), .surfaceChroma = surfaceChroma()})) {
        m_colours.insert(QString::fromUtf8(role.name.data(), static_cast<qsizetype>(role.name.size())),
                         QColor(role.colour.r, role.colour.g, role.colour.b, role.colour.a));
    }
    QGuiApplication::setPalette(palette());
    emit changed();
}

QPalette ThemePalette::palette() const {
    const auto colour = [this](const char* name) { return m_colours.value(QLatin1StringView(name)).value<QColor>(); };
    QPalette result;
    const auto both = [&result](QPalette::ColorRole role, const QColor& value) {
        result.setColor(QPalette::Active, role, value);
        result.setColor(QPalette::Inactive, role, value);
        result.setColor(QPalette::Disabled, role, value);
    };
    both(QPalette::Window, colour("window"));
    both(QPalette::WindowText, colour("text"));
    both(QPalette::Base, colour("base"));
    both(QPalette::AlternateBase, colour("alternateBase"));
    both(QPalette::Text, colour("text"));
    both(QPalette::Button, colour("button"));
    both(QPalette::ButtonText, colour("text"));
    both(QPalette::BrightText, colour("highlightedText"));
    both(QPalette::Highlight, colour("highlight"));
    both(QPalette::HighlightedText, colour("highlightedText"));
    both(QPalette::Accent, colour("accent"));
    both(QPalette::Light, colour("light"));
    both(QPalette::Midlight, colour("midlight"));
    both(QPalette::Mid, colour("mid"));
    both(QPalette::Dark, colour("dark"));
    both(QPalette::Shadow, colour("shadow"));
    both(QPalette::Link, colour("link"));
    both(QPalette::LinkVisited, colour("linkVisited"));
    both(QPalette::ToolTipBase, colour("toolTip"));
    both(QPalette::ToolTipText, colour("text"));
    both(QPalette::PlaceholderText, colour("placeholderText"));
    // Disabled, the words and the selection are dimmed.
    result.setColor(QPalette::Disabled, QPalette::WindowText, colour("textDisabled"));
    result.setColor(QPalette::Disabled, QPalette::Text, colour("textDisabled"));
    result.setColor(QPalette::Disabled, QPalette::ButtonText, colour("textDisabled"));
    result.setColor(QPalette::Disabled, QPalette::Button, colour("buttonDisabled"));
    result.setColor(QPalette::Disabled, QPalette::Highlight, colour("highlightDisabled"));
    result.setColor(QPalette::Disabled, QPalette::HighlightedText, colour("textMuted"));
    return result;
}

}  // namespace iv
