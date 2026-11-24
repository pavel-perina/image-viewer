#pragma once

#include <QObject>
#include <QSize>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include "ui/appearance_draft.h"

namespace iv {
class AppearanceDraft;
class Preferences;
class ThemePalette;
class ColorSettings;
struct DisplayProfileChoice;

// One transaction across Settings pages. Only Appearance previews; file validation is
// asynchronous, and no preference is written until every changed value is ready.
class SettingsDraft : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(iv::AppearanceDraft* appearance READ appearance CONSTANT)
    Q_PROPERTY(QString transparency READ transparency WRITE setTransparency NOTIFY changed)
    Q_PROPERTY(bool colorManaged READ colorManaged WRITE setColorManaged NOTIFY changed)
    Q_PROPERTY(QString displayProfile READ displayProfile NOTIFY changed)
    Q_PROPERTY(int outputBits READ outputBits WRITE setOutputBits NOTIFY changed)
    Q_PROPERTY(QSize thumbnailSize READ thumbnailSize WRITE setThumbnailSize NOTIFY changed)
    Q_PROPERTY(int thumbnailCacheMiB READ thumbnailCacheMiB WRITE setThumbnailCacheMiB NOTIFY changed)
    // Not reset with the rest of Viewing: it is where the user keeps files, not a look.
    Q_PROPERTY(QString colorTablesFolder READ colorTablesFolder NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
public:
    explicit SettingsDraft(QObject* parent = nullptr);
    // An isolated preferences/palette pair; does not install profiles in the app backend.
    SettingsDraft(Preferences& preferences, ThemePalette& theme, QObject* parent = nullptr);
    ~SettingsDraft() override;
    AppearanceDraft* appearance() const;
    QString transparency() const;
    void setTransparency(const QString& value);
    bool colorManaged() const;
    void setColorManaged(bool value);
    QString displayProfile() const;
    int outputBits() const;
    void setOutputBits(int value);
    QSize thumbnailSize() const;
    void setThumbnailSize(QSize value);
    int thumbnailCacheMiB() const;
    void setThumbnailCacheMiB(int value);
    QString colorTablesFolder() const;
    bool dirty() const;
    bool busy() const;
    QString problem() const;
    Q_INVOKABLE bool begin();
    Q_INVOKABLE void apply(bool closeAfter = false);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void chooseProfile(const QUrl& file);
    Q_INVOKABLE void useSrgb();
    // An empty URL clears it.
    Q_INVOKABLE void chooseColorTablesFolder(const QUrl& folder);
    Q_INVOKABLE void resetViewing();
    Q_INVOKABLE void resetThumbnails();
signals:
    void changed();
    void applied(bool closeAfter);
    void activationRequested();
private:
    SettingsDraft(Preferences&, ThemePalette&, ColorSettings*, QObject*);
    enum Field { Transparency, ColorManaged, Profile, OutputBits, ThumbnailSize, CacheMiB, TablesFolder };
    void refresh();
    void set(Field field, QVariant value);
    void readProfile(const QString& path, bool applying, bool closeAfter);
    void commit(bool closeAfter, const DisplayProfileChoice* profile = nullptr);
    bool profileEdited() const;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace iv
