#include "ui/preferences.h"

#include <QCoreApplication>
#include <QJSEngine>
#include <QSettings>

#include <atomic>
#include <cmath>
#include <utility>

namespace iv {

namespace {
constexpr std::uint64_t kMiB = 1024ull * 1024;
std::atomic<std::uint64_t> cacheBudget{512 * kMiB};
}

struct Preferences::Impl {
    std::unique_ptr<QSettings> settings;
    bool processWide = false;
    double hue = 150;
    double accentChroma = 0.075;
    double surfaceChroma = 0;
    bool keepAutoContrast = false;
    bool showHiddenFolders = false;
    QString transparency = QStringLiteral("dark");
    bool colorManaged = true;
    QString displayProfile = QString();
    int sortKey = 0;
    bool sortDescending = false;
    bool askBeforeEditing = true;
    bool trimEdges = false;
    bool trashAfterJxl = false;
    bool panelPinned = false;
    bool panelAnimated = true;
    bool histogramLogScale = false;
    bool histogramFitRange = false;
    int thumbnailCacheMiB = 512;
    int outputBits = 8;
};

Preferences::Preferences(QObject* parent) : Preferences(std::make_unique<QSettings>(), parent) {}

Preferences::Preferences(const QString& file, QObject* parent)
    : Preferences(std::make_unique<QSettings>(file, QSettings::IniFormat), parent) {}

Preferences::Preferences(std::unique_ptr<QSettings> settings, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>()) {
    m_impl->settings = std::move(settings);
    {
        const double value = m_impl->settings->value(QStringLiteral("Appearance/hue"), 150).toDouble();
        if (std::isfinite(value) && value >= 0 && value <= 360) m_impl->hue = value;
    }
    {
        const double value = m_impl->settings->value(QStringLiteral("Appearance/accentChroma"), 0.075).toDouble();
        if (std::isfinite(value) && value >= 0 && value <= 0.4) m_impl->accentChroma = value;
    }
    {
        const double value = m_impl->settings->value(QStringLiteral("Appearance/surfaceChroma"), 0).toDouble();
        if (std::isfinite(value) && value >= 0 && value <= 0.4) m_impl->surfaceChroma = value;
    }
    {
        const bool value = m_impl->settings->value(QStringLiteral("View/keepAutoContrast"), false).toBool();
        m_impl->keepAutoContrast = value;
    }
    {
        const bool value = m_impl->settings->value(QStringLiteral("View/showHiddenFolders"), false).toBool();
        m_impl->showHiddenFolders = value;
    }
    {
        const QString value = m_impl->settings->value(QStringLiteral("View/transparency"), QStringLiteral("dark")).toString();
        if (value == "dark" || value == "light" || value == "plain") m_impl->transparency = value;
    }
    {
        const bool value = m_impl->settings->value(QStringLiteral("Color/enabled"), true).toBool();
        m_impl->colorManaged = value;
    }
    {
        const QString value = m_impl->settings->value(QStringLiteral("Color/displayProfile"), QString()).toString();
        m_impl->displayProfile = value;
    }
    {
        const int value = m_impl->settings->value(QStringLiteral("Browser/sortKey"), 0).toInt();
        if (value >= 0 && value <= 3) m_impl->sortKey = value;
    }
    {
        const bool value = m_impl->settings->value(QStringLiteral("Browser/sortDescending"), false).toBool();
        m_impl->sortDescending = value;
    }
    {
        const bool value = m_impl->settings->value(QStringLiteral("FileOperations/askBeforeEditing"), true).toBool();
        m_impl->askBeforeEditing = value;
    }
    {
        const bool value = m_impl->settings->value(QStringLiteral("FileOperations/trimEdges"), false).toBool();
        m_impl->trimEdges = value;
    }
    {
        const bool value = m_impl->settings->value(QStringLiteral("FileOperations/trashAfterJxl"), false).toBool();
        m_impl->trashAfterJxl = value;
    }
    {
        const bool value = m_impl->settings->value(QStringLiteral("InfoPanel/panelPinned"), false).toBool();
        m_impl->panelPinned = value;
    }
    {
        const bool value = m_impl->settings->value(QStringLiteral("InfoPanel/panelAnimated"), true).toBool();
        m_impl->panelAnimated = value;
    }
    {
        const bool value = m_impl->settings->value(QStringLiteral("InfoPanel/logScale"), false).toBool();
        m_impl->histogramLogScale = value;
    }
    {
        const bool value = m_impl->settings->value(QStringLiteral("InfoPanel/fitRange"), false).toBool();
        m_impl->histogramFitRange = value;
    }
    {
        const int value = m_impl->settings->value(QStringLiteral("Thumbnails/cacheMiB"), 512).toInt();
        if (value >= 1 && value <= 65536) m_impl->thumbnailCacheMiB = value;
    }
    {
        const int value = m_impl->settings->value(QStringLiteral("View/outputBits"), 8).toInt();
        if (value == 8 || value == 10) m_impl->outputBits = value;
    }
}

Preferences::~Preferences() = default;

Preferences* Preferences::instance() {
    static Preferences* const preferences = [] {
        auto* result = new Preferences(QCoreApplication::instance());
        result->m_impl->processWide = true;
        cacheBudget.store(static_cast<std::uint64_t>(result->thumbnailCacheMiB()) * kMiB, std::memory_order_relaxed);
        return result;
    }();
    return preferences;
}

Preferences* Preferences::create(QQmlEngine*, QJSEngine*) {
    auto* result = instance();
    QJSEngine::setObjectOwnership(result, QJSEngine::CppOwnership);
    return result;
}

int Preferences::savedOutputBits() {
    const int value = QSettings().value(QStringLiteral("View/outputBits"), 8).toInt();
    return value == 10 ? 10 : 8;
}

std::uint64_t Preferences::thumbnailCacheBudgetBytes() {
    return cacheBudget.load(std::memory_order_relaxed);
}

double Preferences::hue() const { return m_impl->hue; }

void Preferences::setHue(double value) {
    if (!(std::isfinite(value) && value >= 0 && value <= 360)) return;
    if (m_impl->hue == value) return;
    m_impl->hue = value;
    m_impl->settings->setValue(QStringLiteral("Appearance/hue"), value);
    emit hueChanged();
}

double Preferences::accentChroma() const { return m_impl->accentChroma; }

void Preferences::setAccentChroma(double value) {
    if (!(std::isfinite(value) && value >= 0 && value <= 0.4)) return;
    if (m_impl->accentChroma == value) return;
    m_impl->accentChroma = value;
    m_impl->settings->setValue(QStringLiteral("Appearance/accentChroma"), value);
    emit accentChromaChanged();
}

double Preferences::surfaceChroma() const { return m_impl->surfaceChroma; }

void Preferences::setSurfaceChroma(double value) {
    if (!(std::isfinite(value) && value >= 0 && value <= 0.4)) return;
    if (m_impl->surfaceChroma == value) return;
    m_impl->surfaceChroma = value;
    m_impl->settings->setValue(QStringLiteral("Appearance/surfaceChroma"), value);
    emit surfaceChromaChanged();
}

bool Preferences::keepAutoContrast() const { return m_impl->keepAutoContrast; }

void Preferences::setKeepAutoContrast(bool value) {
    if (m_impl->keepAutoContrast == value) return;
    m_impl->keepAutoContrast = value;
    m_impl->settings->setValue(QStringLiteral("View/keepAutoContrast"), value);
    emit keepAutoContrastChanged();
}

bool Preferences::showHiddenFolders() const { return m_impl->showHiddenFolders; }

void Preferences::setShowHiddenFolders(bool value) {
    if (m_impl->showHiddenFolders == value) return;
    m_impl->showHiddenFolders = value;
    m_impl->settings->setValue(QStringLiteral("View/showHiddenFolders"), value);
    emit showHiddenFoldersChanged();
}

QString Preferences::transparency() const { return m_impl->transparency; }

void Preferences::setTransparency(QString value) {
    if (!(value == "dark" || value == "light" || value == "plain")) return;
    if (m_impl->transparency == value) return;
    m_impl->transparency = value;
    m_impl->settings->setValue(QStringLiteral("View/transparency"), value);
    emit transparencyChanged();
}

bool Preferences::colorManaged() const { return m_impl->colorManaged; }

void Preferences::setColorManaged(bool value) {
    if (m_impl->colorManaged == value) return;
    m_impl->colorManaged = value;
    m_impl->settings->setValue(QStringLiteral("Color/enabled"), value);
    emit colorManagedChanged();
}

QString Preferences::displayProfile() const { return m_impl->displayProfile; }

void Preferences::setDisplayProfile(QString value) {
    if (m_impl->displayProfile == value) return;
    m_impl->displayProfile = value;
    m_impl->settings->setValue(QStringLiteral("Color/displayProfile"), value);
    emit displayProfileChanged();
}

int Preferences::sortKey() const { return m_impl->sortKey; }

void Preferences::setSortKey(int value) {
    if (!(value >= 0 && value <= 3)) return;
    if (m_impl->sortKey == value) return;
    m_impl->sortKey = value;
    m_impl->settings->setValue(QStringLiteral("Browser/sortKey"), value);
    emit sortKeyChanged();
}

bool Preferences::sortDescending() const { return m_impl->sortDescending; }

void Preferences::setSortDescending(bool value) {
    if (m_impl->sortDescending == value) return;
    m_impl->sortDescending = value;
    m_impl->settings->setValue(QStringLiteral("Browser/sortDescending"), value);
    emit sortDescendingChanged();
}

bool Preferences::askBeforeEditing() const { return m_impl->askBeforeEditing; }

void Preferences::setAskBeforeEditing(bool value) {
    if (m_impl->askBeforeEditing == value) return;
    m_impl->askBeforeEditing = value;
    m_impl->settings->setValue(QStringLiteral("FileOperations/askBeforeEditing"), value);
    emit askBeforeEditingChanged();
}

bool Preferences::trimEdges() const { return m_impl->trimEdges; }

void Preferences::setTrimEdges(bool value) {
    if (m_impl->trimEdges == value) return;
    m_impl->trimEdges = value;
    m_impl->settings->setValue(QStringLiteral("FileOperations/trimEdges"), value);
    emit trimEdgesChanged();
}

bool Preferences::trashAfterJxl() const { return m_impl->trashAfterJxl; }

void Preferences::setTrashAfterJxl(bool value) {
    if (m_impl->trashAfterJxl == value) return;
    m_impl->trashAfterJxl = value;
    m_impl->settings->setValue(QStringLiteral("FileOperations/trashAfterJxl"), value);
    emit trashAfterJxlChanged();
}

bool Preferences::panelPinned() const { return m_impl->panelPinned; }

void Preferences::setPanelPinned(bool value) {
    if (m_impl->panelPinned == value) return;
    m_impl->panelPinned = value;
    m_impl->settings->setValue(QStringLiteral("InfoPanel/panelPinned"), value);
    emit panelPinnedChanged();
}

bool Preferences::panelAnimated() const { return m_impl->panelAnimated; }

void Preferences::setPanelAnimated(bool value) {
    if (m_impl->panelAnimated == value) return;
    m_impl->panelAnimated = value;
    m_impl->settings->setValue(QStringLiteral("InfoPanel/panelAnimated"), value);
    emit panelAnimatedChanged();
}

bool Preferences::histogramLogScale() const { return m_impl->histogramLogScale; }

void Preferences::setHistogramLogScale(bool value) {
    if (m_impl->histogramLogScale == value) return;
    m_impl->histogramLogScale = value;
    m_impl->settings->setValue(QStringLiteral("InfoPanel/logScale"), value);
    emit histogramLogScaleChanged();
}

bool Preferences::histogramFitRange() const { return m_impl->histogramFitRange; }

void Preferences::setHistogramFitRange(bool value) {
    if (m_impl->histogramFitRange == value) return;
    m_impl->histogramFitRange = value;
    m_impl->settings->setValue(QStringLiteral("InfoPanel/fitRange"), value);
    emit histogramFitRangeChanged();
}

int Preferences::thumbnailCacheMiB() const { return m_impl->thumbnailCacheMiB; }

void Preferences::setThumbnailCacheMiB(int value) {
    if (!(value >= 1 && value <= 65536)) return;
    if (m_impl->thumbnailCacheMiB == value) return;
    m_impl->thumbnailCacheMiB = value;
    m_impl->settings->setValue(QStringLiteral("Thumbnails/cacheMiB"), value);
    if (m_impl->processWide) cacheBudget.store(static_cast<std::uint64_t>(value) * kMiB, std::memory_order_relaxed);
    emit thumbnailCacheMiBChanged();
}

int Preferences::outputBits() const { return m_impl->outputBits; }

void Preferences::setOutputBits(int value) {
    if (!(value == 8 || value == 10)) return;
    if (m_impl->outputBits == value) return;
    m_impl->outputBits = value;
    m_impl->settings->setValue(QStringLiteral("View/outputBits"), value);
    emit outputBitsChanged();
}

}  // namespace iv
