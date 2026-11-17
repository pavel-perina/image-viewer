#include "ui/color_settings.h"

#include <QFile>
#include <QJSEngine>
#include <QQmlEngine>
#include <QScopedValueRollback>
#include <QtDebug>

#include <atomic>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <utility>

#include "ui/preferences.h"

namespace iv {

namespace {

std::mutex targetMutex;
ColorTarget currentTarget;
std::atomic<int> currentGeneration = 0;

struct LoadedProfile {
    std::shared_ptr<const IccBytes> bytes;
    QString name;
    QString problem;
};

LoadedProfile loadProfile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {{}, {}, QStringLiteral("Cannot read this file.")};
    }
    const QByteArray data = file.readAll();
    const std::span bytes(reinterpret_cast<const std::uint8_t*>(data.constData()), static_cast<std::size_t>(data.size()));
    const std::string problem = displayProfileProblem(bytes);
    if (!problem.empty()) {
        return {{}, {}, QString::fromStdString(problem)};
    }
    const QString name = QString::fromStdString(iccDescription(bytes));
    return {std::make_shared<const IccBytes>(bytes.begin(), bytes.end()), name.isEmpty() ? file.fileName() : name, {}};
}

}  // namespace

ColorSettings* ColorSettings::instance() {
    static ColorSettings* const settings = new ColorSettings;
    return settings;
}

ColorSettings* ColorSettings::create(QQmlEngine*, QJSEngine*) {
    ColorSettings* const settings = instance();
    QJSEngine::setObjectOwnership(settings, QJSEngine::CppOwnership);
    return settings;
}

ColorSettings::ColorSettings() {
    auto* preferences = Preferences::instance();
    connect(preferences, &Preferences::colorManagedChanged, this, &ColorSettings::refreshPreferences);
    connect(preferences, &Preferences::displayProfileChanged, this, &ColorSettings::refreshPreferences);
    refreshPreferences();
}

void ColorSettings::refreshPreferences() {
    if (m_settingProfile) return;
    auto* preferences = Preferences::instance();
    ColorTarget changedTarget = target();
    const QString path = preferences->displayProfile();
    if (changedTarget.enabled == preferences->colorManaged() && path == m_profilePath) return;
    changedTarget.enabled = preferences->colorManaged();
    if (path != m_profilePath) {
        changedTarget.displayProfile.reset();
        m_name.clear();
        if (!path.isEmpty()) {
            LoadedProfile loaded = loadProfile(path);
            if (loaded.bytes) {
                changedTarget.displayProfile = std::move(loaded.bytes);
                m_name = loaded.name;
            } else {
                qWarning() << "Using sRGB, because the display profile" << path << "cannot be used:" << loaded.problem;
            }
        }
        m_profilePath = path;
    }
    publish(std::move(changedTarget), m_name);
}

ColorTarget ColorSettings::target() {
    const std::scoped_lock lock(targetMutex);
    return currentTarget;
}

int ColorSettings::generation() { return currentGeneration.load(std::memory_order_relaxed); }

bool ColorSettings::enabled() const { return target().enabled; }

void ColorSettings::setEnabled(bool enabled) {
    Preferences::instance()->setColorManaged(enabled);
}

QString ColorSettings::displayProfileName() const { return m_name.isEmpty() ? QStringLiteral("sRGB") : m_name; }

bool ColorSettings::hasDisplayProfile() const { return target().displayProfile != nullptr; }

QString ColorSettings::useDisplayProfile(const QUrl& file) {
    const QString path = file.toLocalFile();
    LoadedProfile loaded = loadProfile(path);
    if (!loaded.bytes) {
        return loaded.problem;
    }
    ColorTarget changedTarget = target();
    changedTarget.displayProfile = std::move(loaded.bytes);
    {
        QScopedValueRollback guard(m_settingProfile, true);
        Preferences::instance()->setDisplayProfile(path);
    }
    m_profilePath = path;
    publish(std::move(changedTarget), loaded.name);
    return {};
}

void ColorSettings::useSrgbDisplay() {
    Preferences::instance()->setDisplayProfile({});
}

void ColorSettings::publish(ColorTarget target, const QString& name) {
    m_name = name;
    {
        const std::scoped_lock lock(targetMutex);
        currentTarget = std::move(target);
    }
    currentGeneration.fetch_add(1, std::memory_order_relaxed);
    emit changed();
}

}  // namespace iv
