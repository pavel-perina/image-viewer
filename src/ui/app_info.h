#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace iv {

// What the About dialog tells: the version, what the program is built with and where the
// licence texts are.
class AppInfo : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString version READ version CONSTANT)
    // Qt, the libraries and the typeface, each a map of name, version, licence and url.
    Q_PROPERTY(QVariantList components READ components CONSTANT)
    // The folder of licence texts the installer ships: beside the program on Windows, in
    // share/imageviewer/licenses in the AppImage; empty where there is none, as in a build folder
    // or with the system's libraries on Linux.
    Q_PROPERTY(QUrl licencesUrl READ licencesUrl CONSTANT)

public:
    explicit AppInfo(QObject* parent = nullptr);

    QString version() const;
    QVariantList components() const;
    QUrl licencesUrl() const;

    // Where a program in a folder finds its licence texts, as licencesUrl looks; empty if nowhere.
    static QString licencesFolderFor(const QString& programFolder);
    // Where the licence texts are looked for, in place of beside the program, for tests.
    static void setLicencesFolderForTesting(const QString& folder);
};

}  // namespace iv
