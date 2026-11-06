#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStringList>
#include <QtQml/QQmlExtensionPlugin>

#include "ui/color_settings.h"
#include "ui/thumbnail_provider.h"

Q_IMPORT_QML_PLUGIN(ImageViewerPlugin)

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Image Viewer"));
    QCoreApplication::setOrganizationName(QStringLiteral("ImageViewer"));  // where settings are stored
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    iv::ColorSettings::instance();  // before anything is decoded, and after the settings have a home

    const QStringList arguments = QCoreApplication::arguments();

    QQmlApplicationEngine engine;
    engine.addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
    engine.setInitialProperties({{QStringLiteral("initialPath"), arguments.value(1, QDir::homePath())}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("ImageViewer", "Main");
    return QCoreApplication::exec();
}
