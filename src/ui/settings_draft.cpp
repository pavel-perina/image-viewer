#include "ui/settings_draft.h"

#include <QMetaObject>
#include <array>
#include <algorithm>
#include "core/work_queue.h"
#include "ui/appearance_draft.h"
#include "ui/color_settings.h"
#include "ui/preferences.h"
#include "ui/theme_palette.h"

namespace iv {
struct SettingsDraft::Impl {
    struct Entry { const char* name; QVariant defaults; QVariant saved; QVariant value; };
    std::array<Entry, 8> entries{{
        {"transparency", QStringLiteral("dark"), {}, {}},
        {"colorManaged", true, {}, {}},
        {"displayProfile", QString(), {}, {}},
        {"outputBits", 8, {}, {}},
        {"thumbnailSize", QSize(164, 164), {}, {}},
        {"thumbnailCacheMiB", 512, {}, {}},
        {"colorTablesFolder", QString(), {}, {}},
        {"escapeAction", QStringLiteral("auto"), {}, {}}
    }};
    Preferences& preferences;
    AppearanceDraft appearance;
    ColorSettings* backend;
    WorkQueue& queue = WorkQueue::global();
    WorkQueue::GroupId group = queue.createGroup();
    quint64 epoch = 0;
    bool active = false;
    bool busy = false;
    bool applying = false;
    bool reloadProfile = false;
    QString problem;
    Impl(Preferences& preferences, ThemePalette& theme, ColorSettings* backend, QObject* owner)
        : preferences(preferences), appearance(preferences, theme, owner), backend(backend) {
        appearance.setObjectName(QStringLiteral("appearanceDraft"));
    }
};

SettingsDraft::SettingsDraft(QObject* parent)
    : SettingsDraft(*Preferences::instance(), *ThemePalette::instance(), ColorSettings::instance(), parent) {}
SettingsDraft::SettingsDraft(Preferences& preferences, ThemePalette& theme, QObject* parent)
    : SettingsDraft(preferences, theme, nullptr, parent) {}
SettingsDraft::SettingsDraft(Preferences& preferences, ThemePalette& theme, ColorSettings* backend, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(preferences, theme, backend, this)) {
    connect(&m_impl->appearance, &AppearanceDraft::changed, this, &SettingsDraft::changed);
    connect(&m_impl->appearance, &AppearanceDraft::activationRequested, this, &SettingsDraft::activationRequested);
    connect(&preferences, &Preferences::transparencyChanged, this, &SettingsDraft::refresh);
    connect(&preferences, &Preferences::colorManagedChanged, this, &SettingsDraft::refresh);
    connect(&preferences, &Preferences::displayProfileChanged, this, &SettingsDraft::refresh);
    connect(&preferences, &Preferences::outputBitsChanged, this, &SettingsDraft::refresh);
    connect(&preferences, &Preferences::thumbnailSizeChanged, this, &SettingsDraft::refresh);
    connect(&preferences, &Preferences::thumbnailCacheMiBChanged, this, &SettingsDraft::refresh);
    connect(&preferences, &Preferences::colorTablesFolderChanged, this, &SettingsDraft::refresh);
    connect(&preferences, &Preferences::escapeActionChanged, this, &SettingsDraft::refresh);
    refresh();
}
SettingsDraft::~SettingsDraft() {
    cancel();
    m_impl->queue.cancelGroup(m_impl->group);
    m_impl->queue.waitGroup(m_impl->group);
}

AppearanceDraft* SettingsDraft::appearance() const { return &m_impl->appearance; }
QString SettingsDraft::transparency() const { return m_impl->entries[Transparency].value.toString(); }
bool SettingsDraft::colorManaged() const { return m_impl->entries[ColorManaged].value.toBool(); }
QString SettingsDraft::displayProfile() const { return m_impl->entries[Profile].value.toString(); }
int SettingsDraft::outputBits() const { return m_impl->entries[OutputBits].value.toInt(); }
QSize SettingsDraft::thumbnailSize() const { return m_impl->entries[ThumbnailSize].value.toSize(); }
int SettingsDraft::thumbnailCacheMiB() const { return m_impl->entries[CacheMiB].value.toInt(); }
QString SettingsDraft::colorTablesFolder() const { return m_impl->entries[TablesFolder].value.toString(); }
QString SettingsDraft::escapeAction() const { return m_impl->entries[EscapeAction].value.toString(); }
void SettingsDraft::setEscapeAction(const QString& value) { if (Preferences::validEscapeAction(value)) set(EscapeAction, value); }
void SettingsDraft::chooseColorTablesFolder(const QUrl& folder) { set(TablesFolder, folder.isEmpty() ? QString() : folder.toLocalFile()); }
bool SettingsDraft::busy() const { return m_impl->busy; }
QString SettingsDraft::problem() const { return m_impl->problem; }
bool SettingsDraft::dirty() const {
    return appearance()->dirty() || m_impl->reloadProfile || std::ranges::any_of(m_impl->entries, [](const auto& entry) { return entry.value != entry.saved; });
}
bool SettingsDraft::profileEdited() const {
    return m_impl->reloadProfile || m_impl->entries[Profile].value != m_impl->entries[Profile].saved;
}

void SettingsDraft::refresh() {
    if (m_impl->applying) return;
    for (auto& entry : m_impl->entries) {
        const QVariant next = m_impl->preferences.property(entry.name);
        const bool preserveProfile = &entry == &m_impl->entries[Profile] && m_impl->reloadProfile;
        if (!m_impl->active || (entry.value == entry.saved && !preserveProfile)) entry.value = next;
        entry.saved = next;
    }
    emit changed();
}
void SettingsDraft::set(Field field, QVariant value) {
    if (!m_impl->active || busy() || m_impl->entries[field].value == value) return;
    m_impl->entries[field].value = std::move(value);
    m_impl->problem.clear();
    emit changed();
}
void SettingsDraft::setTransparency(const QString& value) { if (Preferences::validTransparency(value)) set(Transparency, value); }
void SettingsDraft::setColorManaged(bool value) { set(ColorManaged, value); }
void SettingsDraft::setOutputBits(int value) { if (Preferences::validOutputBits(value)) set(OutputBits, value); }
void SettingsDraft::setThumbnailSize(QSize value) { if (Preferences::validThumbnailSize(value)) set(ThumbnailSize, value); }
void SettingsDraft::setThumbnailCacheMiB(int value) { if (Preferences::validCacheMiB(value)) set(CacheMiB, value); }
void SettingsDraft::useSrgb() {
    if (!m_impl->active || busy()) return;
    m_impl->reloadProfile = false;
    set(Profile, QString());
    emit changed();
}

bool SettingsDraft::begin() {
    if (!appearance()->begin()) return false;
    ++m_impl->epoch;
    refresh();
    m_impl->active = true;
    m_impl->problem.clear();
    emit changed();
    return true;
}
void SettingsDraft::cancel() {
    ++m_impl->epoch;
    m_impl->queue.cancelGroup(m_impl->group);
    m_impl->active = false;
    m_impl->busy = false;
    m_impl->reloadProfile = false;
    m_impl->problem.clear();
    appearance()->cancel();
    refresh();
}
void SettingsDraft::resetViewing() {
    if (!m_impl->active || busy()) return;
    useSrgb();
    for (Field field : {Transparency, ColorManaged, Profile, OutputBits}) set(field, m_impl->entries[field].defaults);
}
void SettingsDraft::resetThumbnails() {
    for (Field field : {ThumbnailSize, CacheMiB}) set(field, m_impl->entries[field].defaults);
}
void SettingsDraft::resetBehaviour() { set(EscapeAction, m_impl->entries[EscapeAction].defaults); }

void SettingsDraft::chooseProfile(const QUrl& file) {
    if (m_impl->active && !busy()) readProfile(file.toLocalFile(), false, false);
}
void SettingsDraft::apply(bool closeAfter) {
    if (!m_impl->active || busy()) return;
    if (profileEdited() && !displayProfile().isEmpty()) readProfile(displayProfile(), true, closeAfter);
    else commit(closeAfter);
}

void SettingsDraft::readProfile(const QString& path, bool applying, bool closeAfter) {
    m_impl->busy = true;
    m_impl->problem.clear();
    emit changed();
    const auto epoch = m_impl->epoch;
    m_impl->queue.submit(m_impl->group, 100, [this, epoch, path, applying, closeAfter](std::stop_token stop) {
        if (stop.stop_requested()) return;
        auto profile = ColorSettings::readDisplayProfile(path);
        QMetaObject::invokeMethod(this, [this, epoch, applying, closeAfter, profile = std::move(profile)] {
            if (epoch != m_impl->epoch || !m_impl->active) return;
            m_impl->busy = false;
            if (!profile.bytes) {
                m_impl->problem = QStringLiteral("Cannot use the display profile: ") + profile.problem;
                emit changed();
            } else if (applying) {
                commit(closeAfter, &profile);
            } else {
                set(Profile, profile.path);
                m_impl->reloadProfile = true;
                emit changed();
            }
        });
    });
}

void SettingsDraft::commit(bool closeAfter, const DisplayProfileChoice* profile) {
    m_impl->applying = true;
    if (m_impl->backend && profileEdited()) {
        if (profile) m_impl->backend->usePreparedDisplayProfile(*profile);
        else m_impl->backend->useSrgbDisplay();
    }
    for (auto& entry : m_impl->entries) m_impl->preferences.setProperty(entry.name, entry.value);
    appearance()->apply();
    for (auto& entry : m_impl->entries) entry.value = entry.saved = m_impl->preferences.property(entry.name);
    m_impl->applying = false;
    m_impl->reloadProfile = false;
    m_impl->problem.clear();
    emit changed();
    emit applied(closeAfter);
}
} // namespace iv
