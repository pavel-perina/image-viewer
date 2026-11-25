#include "ui/app_info.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QStandardPaths>
#include <QSvgRenderer>
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

QString AppInfo::licencesFolderFor(const QString& programFolder) {
    const QDir folder(programFolder);
    for (const QString& candidate : {folder.filePath(QStringLiteral("licenses")),
                                     folder.filePath(QStringLiteral("../share/imageviewer/licenses"))}) {
        if (QFileInfo(candidate).isDir()) {
            return QDir::cleanPath(candidate);
        }
    }
    return {};
}

QUrl AppInfo::licencesUrl() const {
    const QString folder = !licencesFolderOverride().isEmpty()
            ? licencesFolderOverride()
            : licencesFolderFor(QCoreApplication::applicationDirPath());
    return QFileInfo(folder).isDir() ? QUrl::fromLocalFile(folder) : QUrl();
}

void AppInfo::setLicencesFolderForTesting(const QString& folder) {
    licencesFolderOverride() = folder;
}

QString AppInfo::installedDesktopEntry() {
    const QString name = QStringLiteral("imageviewer");
    const bool installed =
        !QStandardPaths::locate(QStandardPaths::ApplicationsLocation, name + QStringLiteral(".desktop")).isEmpty();
    return installed ? name : QString();
}

QIcon AppInfo::programIcon() {
    // Drawn here rather than left to Qt's SVG icon plugin, which a package can leave out.
    QSvgRenderer renderer(QStringLiteral(":/program/icon.svg"));
    QIcon icon;
    if (!renderer.isValid()) {
        return icon;
    }
    for (const int size : {16, 22, 24, 32, 48, 64, 128, 256}) {
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        renderer.render(&painter);
        painter.end();
        icon.addPixmap(QPixmap::fromImage(image));
    }
    return icon;
}

}  // namespace iv
