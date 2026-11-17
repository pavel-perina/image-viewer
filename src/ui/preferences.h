#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <memory>

class QJSEngine;
class QQmlEngine;
class QSettings;

namespace iv {

// User preferences only. Remembered tool values, dialog geometry and per-view state
// stay with their owners. This object owns persistence; rendering backends consume it.
// Construct/use on the GUI thread, except the explicit cache-budget snapshot below.
class Preferences : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(double hue READ hue WRITE setHue NOTIFY hueChanged)
    Q_PROPERTY(double accentChroma READ accentChroma WRITE setAccentChroma NOTIFY accentChromaChanged)
    Q_PROPERTY(double surfaceChroma READ surfaceChroma WRITE setSurfaceChroma NOTIFY surfaceChromaChanged)
    Q_PROPERTY(bool keepAutoContrast READ keepAutoContrast WRITE setKeepAutoContrast NOTIFY keepAutoContrastChanged)
    Q_PROPERTY(bool showHiddenFolders READ showHiddenFolders WRITE setShowHiddenFolders NOTIFY showHiddenFoldersChanged)
    Q_PROPERTY(QString transparency READ transparency WRITE setTransparency NOTIFY transparencyChanged)
    Q_PROPERTY(bool colorManaged READ colorManaged WRITE setColorManaged NOTIFY colorManagedChanged)
    Q_PROPERTY(QString displayProfile READ displayProfile WRITE setDisplayProfile NOTIFY displayProfileChanged)
    Q_PROPERTY(int sortKey READ sortKey WRITE setSortKey NOTIFY sortKeyChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY sortDescendingChanged)
    Q_PROPERTY(bool askBeforeEditing READ askBeforeEditing WRITE setAskBeforeEditing NOTIFY askBeforeEditingChanged)
    Q_PROPERTY(bool trimEdges READ trimEdges WRITE setTrimEdges NOTIFY trimEdgesChanged)
    Q_PROPERTY(bool trashAfterJxl READ trashAfterJxl WRITE setTrashAfterJxl NOTIFY trashAfterJxlChanged)
    Q_PROPERTY(bool panelPinned READ panelPinned WRITE setPanelPinned NOTIFY panelPinnedChanged)
    Q_PROPERTY(bool panelAnimated READ panelAnimated WRITE setPanelAnimated NOTIFY panelAnimatedChanged)
    Q_PROPERTY(bool histogramLogScale READ histogramLogScale WRITE setHistogramLogScale NOTIFY histogramLogScaleChanged)
    Q_PROPERTY(bool histogramFitRange READ histogramFitRange WRITE setHistogramFitRange NOTIFY histogramFitRangeChanged)
    Q_PROPERTY(int thumbnailCacheMiB READ thumbnailCacheMiB WRITE setThumbnailCacheMiB NOTIFY thumbnailCacheMiBChanged)
    Q_PROPERTY(int outputBits READ outputBits WRITE setOutputBits NOTIFY outputBitsChanged)

public:
    // An isolated INI store for tests; it does not change the process-wide preferences.
    explicit Preferences(const QString& settingsFile, QObject* parent = nullptr);
    ~Preferences() override;

    static Preferences* instance();
    static Preferences* create(QQmlEngine*, QJSEngine*);
    // Before QGuiApplication: application/organization names must already be set.
    static int savedOutputBits();
    // Safe on workers and during shutdown. Initialize instance() on the GUI thread first.
    static std::uint64_t thumbnailCacheBudgetBytes();

    double hue() const;
    void setHue(double value);
    double accentChroma() const;
    void setAccentChroma(double value);
    double surfaceChroma() const;
    void setSurfaceChroma(double value);
    bool keepAutoContrast() const;
    void setKeepAutoContrast(bool value);
    bool showHiddenFolders() const;
    void setShowHiddenFolders(bool value);
    QString transparency() const;
    void setTransparency(QString value);
    bool colorManaged() const;
    void setColorManaged(bool value);
    QString displayProfile() const;
    void setDisplayProfile(QString value);
    int sortKey() const;
    void setSortKey(int value);
    bool sortDescending() const;
    void setSortDescending(bool value);
    bool askBeforeEditing() const;
    void setAskBeforeEditing(bool value);
    bool trimEdges() const;
    void setTrimEdges(bool value);
    bool trashAfterJxl() const;
    void setTrashAfterJxl(bool value);
    bool panelPinned() const;
    void setPanelPinned(bool value);
    bool panelAnimated() const;
    void setPanelAnimated(bool value);
    bool histogramLogScale() const;
    void setHistogramLogScale(bool value);
    bool histogramFitRange() const;
    void setHistogramFitRange(bool value);
    int thumbnailCacheMiB() const;
    void setThumbnailCacheMiB(int value);
    int outputBits() const;
    void setOutputBits(int value);

signals:
    void hueChanged();
    void accentChromaChanged();
    void surfaceChromaChanged();
    void keepAutoContrastChanged();
    void showHiddenFoldersChanged();
    void transparencyChanged();
    void colorManagedChanged();
    void displayProfileChanged();
    void sortKeyChanged();
    void sortDescendingChanged();
    void askBeforeEditingChanged();
    void trimEdgesChanged();
    void trashAfterJxlChanged();
    void panelPinnedChanged();
    void panelAnimatedChanged();
    void histogramLogScaleChanged();
    void histogramFitRangeChanged();
    void thumbnailCacheMiBChanged();
    void outputBitsChanged();

private:
    // Not publicly default-constructible: Qt must use create(), not one owner per engine.
    explicit Preferences(QObject* parent = nullptr);
    explicit Preferences(std::unique_ptr<QSettings> settings, QObject* parent);
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace iv
