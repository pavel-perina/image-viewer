#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QSettings>
#include <QStringList>
#include <QtQml/QQmlExtensionPlugin>

#include "build_info.h"
#include "ui/app_info.h"
#include "ui/color_settings.h"
#include "ui/display_output.h"
#include "ui/program_font.h"
#include "ui/preferences.h"
#include "ui/retired_settings.h"
#include "ui/thumbnail_provider.h"

Q_IMPORT_QML_PLUGIN(ImageViewerPlugin)
Q_IMPORT_QML_PLUGIN(ImageViewerStylePlugin)

int main(int argc, char* argv[]) {
    QCoreApplication::setApplicationName(QStringLiteral("Image Viewer"));
    QCoreApplication::setOrganizationName(QStringLiteral("ImageViewer"));  // where settings are stored
    iv::requestOutputBits();  // names are set for preferences; still before the application
    QGuiApplication app(argc, argv);
#ifndef Q_OS_WIN
    // The installed desktop entry's name, by which Wayland desktops find its icon and name; and
    // the icon itself, for the rest. Windows takes it from the program file's own icon.
    QGuiApplication::setDesktopFileName(QStringLiteral("imageviewer"));
    QGuiApplication::setWindowIcon(iv::AppInfo::programIcon());
#endif
    QCoreApplication::setApplicationVersion(QStringLiteral(IV_BUILD_COMMIT ", built " IV_BUILD_TIME));
    // Our own style (src/ui/controls), which takes from Fusion what it does not redo, unless
    // another is asked for, to compare: QT_QUICK_CONTROLS_STYLE=Fusion, for one.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("ImageViewerStyle"));
    }

    iv::useProgramFont();  // at the size the system asks for

    iv::ColorSettings::instance();  // before anything is decoded, and after the settings have a home
    iv::Preferences::instance();
    {
        QSettings settings;
        iv::forgetRetiredSettings(settings);
    }

    const QStringList arguments = QCoreApplication::arguments();

    QQmlApplicationEngine engine;
    engine.addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
    engine.setInitialProperties({{QStringLiteral("initialPath"), arguments.value(1, QDir::homePath())}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("ImageViewer", "Main");
    return QCoreApplication::exec();
}
