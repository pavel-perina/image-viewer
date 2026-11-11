#include "ui/app_info.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QVariantMap>

#include "core/libraries.h"

namespace iv {

namespace {

QString& licencesFolderOverride() {
    static QString folder;
    return folder;
}

QVariantMap component(const QString& name, const QString& version, const QString& licence, const QString& url) {
    return {{QStringLiteral("name"), name},
            {QStringLiteral("version"), version},
            {QStringLiteral("licence"), licence},
            {QStringLiteral("url"), url}};
}

}  // namespace

AppInfo::AppInfo(QObject* parent) : QObject(parent) {}

QString AppInfo::version() const {
    return QCoreApplication::applicationVersion();
}

QVariantList AppInfo::components() const {
    QVariantList list;
    // The version running, which with Qt's own libraries can differ from the one built against.
    list.append(component(QStringLiteral("Qt"), QString::fromLatin1(qVersion()), QStringLiteral("LGPL-3.0"),
                          QStringLiteral("https://doc.qt.io/qt-6/licenses-used-in-qt.html")));
    for (const Library& library : libraries()) {
        list.append(component(QString::fromStdString(library.name), QString::fromStdString(library.version),
                              QString::fromStdString(library.licence), QString::fromStdString(library.url)));
    }
    list.append(component(QStringLiteral("Inter typeface"), QStringLiteral("4.1"), QStringLiteral("SIL OFL 1.1"),
                          QStringLiteral("https://rsms.me/inter/")));
    return list;
}

QUrl AppInfo::licencesUrl() const {
    const QString folder = !licencesFolderOverride().isEmpty()
            ? licencesFolderOverride()
            : QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("licenses"));
    return QFileInfo(folder).isDir() ? QUrl::fromLocalFile(folder) : QUrl();
}

void AppInfo::setLicencesFolderForTesting(const QString& folder) {
    licencesFolderOverride() = folder;
}

}  // namespace iv
