#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include "core/color.h"

class QJSEngine;
class QQmlEngine;

namespace iv {

// A validated profile can be loaded on a worker, then installed without UI-thread I/O.
struct DisplayProfileChoice {
    QString path;
    QString name;
    QString problem;
    std::shared_ptr<const IccBytes> bytes;
};

// Whether colours are managed and which display they are for. One for the whole application,
// and read from worker threads, because thumbnails are made there.
class ColorSettings : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString displayProfileName READ displayProfileName NOTIFY changed)
    Q_PROPERTY(bool hasDisplayProfile READ hasDisplayProfile NOTIFY changed)

public:
    static ColorSettings* instance();
    static ColorSettings* create(QQmlEngine*, QJSEngine*);

    // Safe from any thread.
    static ColorTarget target();
    // Goes up with every change, for what must notice that its pixels are out of date.
    static int generation();

    bool enabled() const;
    void setEnabled(bool enabled);
    QString displayProfileName() const;
    bool hasDisplayProfile() const;
    static DisplayProfileChoice readDisplayProfile(const QString& path);
    void usePreparedDisplayProfile(const DisplayProfileChoice& profile);

    // Returns what is wrong with the file, or nothing if it is now in use.
    Q_INVOKABLE QString useDisplayProfile(const QUrl& file);
    Q_INVOKABLE void useSrgbDisplay();

signals:
    void changed();

private:
    ColorSettings();
    void refreshPreferences();
    void publish(ColorTarget target, const QString& name);

    QString m_name;
    QString m_profilePath;
    bool m_settingProfile = false;
};

}  // namespace iv
