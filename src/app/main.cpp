#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
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
    // Fusion unless another is asked for, to compare: QT_QUICK_CONTROLS_STYLE=Material, for one.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("Fusion"));
    }

    // The same font everywhere, at the size the system asks for. What Inter lacks, such as Chinese
    // file names, comes from the system's fonts as before.
    if (const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/InterVariable.ttf")); id >= 0) {
        QFont font = QGuiApplication::font();
        font.setFamilies(QFontDatabase::applicationFontFamilies(id));
        QGuiApplication::setFont(font);
    }

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
