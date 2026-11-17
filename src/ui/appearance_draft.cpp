#include "ui/appearance_draft.h"

#include <QHash>

#include "ui/preferences.h"
#include "ui/theme_palette.h"

namespace iv {
namespace {
// Keyed by palette so isolated test instances do not interfere with the application.
QHash<ThemePalette*, QPointer<AppearanceDraft>> editors;
PaletteChoices savedChoices(const Preferences& preferences) {
    return {preferences.hue(), preferences.accentChroma(), preferences.surfaceChroma()};
}
}

AppearanceDraft::AppearanceDraft(QObject* parent)
    : AppearanceDraft(*Preferences::instance(), *ThemePalette::instance(), parent) {}

AppearanceDraft::AppearanceDraft(Preferences& preferences, ThemePalette& theme, QObject* parent)
    : QObject(parent), m_preferences(preferences), m_theme(theme) {
    connect(&preferences, &Preferences::hueChanged, this, &AppearanceDraft::refresh);
    connect(&preferences, &Preferences::accentChromaChanged, this, &AppearanceDraft::refresh);
    connect(&preferences, &Preferences::surfaceChromaChanged, this, &AppearanceDraft::refresh);
    refresh();
}

AppearanceDraft::~AppearanceDraft() { cancel(); }

bool AppearanceDraft::begin() {
    if (auto other = editors.value(&m_theme)) {
        emit other->activationRequested();
        return false;
    }
    refresh();
    m_active = true;
    editors.insert(&m_theme, this);
    publish();
    return true;
}

bool AppearanceDraft::dirty() const {
    return m_choices.hue != m_saved.hue || m_choices.accentChroma != m_saved.accentChroma
        || m_choices.surfaceChroma != m_saved.surfaceChroma;
}

void AppearanceDraft::refresh() {
    if (m_applying) return;
    const auto next = savedChoices(m_preferences);
    // Another window can still change saved choices. Follow fields we haven't edited,
    // preserving explicit edits; Cancel always returns to the latest saved palette.
    if (!m_active || m_choices.hue == m_saved.hue) m_choices.hue = next.hue;
    if (!m_active || m_choices.accentChroma == m_saved.accentChroma) m_choices.accentChroma = next.accentChroma;
    if (!m_active || m_choices.surfaceChroma == m_saved.surfaceChroma) m_choices.surfaceChroma = next.surfaceChroma;
    m_saved = next;
    publish();
}

void AppearanceDraft::publish() {
    if (m_active) m_theme.preview(m_choices);
    emit changed();
}

void AppearanceDraft::setHue(double value) {
    if (!m_active || !Preferences::validHue(value) || value == m_choices.hue) return;
    m_choices.hue = value;
    publish();
}

void AppearanceDraft::setAccentChroma(double value) {
    if (!m_active || !Preferences::validChroma(value) || value == m_choices.accentChroma) return;
    m_choices.accentChroma = value;
    publish();
}

void AppearanceDraft::setSurfaceChroma(double value) {
    if (!m_active || !Preferences::validChroma(value) || value == m_choices.surfaceChroma) return;
    m_choices.surfaceChroma = value;
    publish();
}

void AppearanceDraft::apply() {
    if (!m_active) return;
    m_applying = true;
    m_preferences.setHue(m_choices.hue);
    m_preferences.setAccentChroma(m_choices.accentChroma);
    m_preferences.setSurfaceChroma(m_choices.surfaceChroma);
    m_applying = false;
    m_saved = savedChoices(m_preferences);
    publish();
}

void AppearanceDraft::cancel() {
    if (!m_active) return;
    m_active = false;
    editors.remove(&m_theme);
    m_theme.clearPreview();
    refresh();
}

void AppearanceDraft::resetDefaults() {
    if (!m_active) return;
    m_choices = PaletteChoices{};
    publish();
}
} // namespace iv
