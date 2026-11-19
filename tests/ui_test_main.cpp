#include "ui_test_support.h"

#include <QQuickStyle>

#include "ui/program_font.h"
#include "ui/preferences.h"

Q_IMPORT_QML_PLUGIN(ImageViewerPlugin)
Q_IMPORT_QML_PLUGIN(ImageViewerStylePlugin)

int main(int argc, char** argv) {
    // Tests run side by side, and must neither see each other's settings nor touch the user's.
    // The same goes for the trash: Qt uses the one in the home folder for files on the same
    // disk as home, and test files are, as they are all in the temporary folder.
    const QTemporaryDir settingsFolder;
    qputenv("HOME", settingsFolder.path().toLocal8Bit());
    qputenv("XDG_DATA_HOME", (settingsFolder.path() + QStringLiteral("/share")).toLocal8Bit());
    qputenv("XDG_CONFIG_HOME", (settingsFolder.path() + QStringLiteral("/config")).toLocal8Bit());
    qputenv("XDG_CACHE_HOME", (settingsFolder.path() + QStringLiteral("/cache")).toLocal8Bit());
    QDir().mkpath(settingsFolder.path() + QStringLiteral("/share"));  // Trash is left for Qt to make
    QGuiApplication app(argc, argv);
    // The program's own style, whatever QT_QUICK_CONTROLS_STYLE says, so that the tests see
    // the controls as people do.
    QQuickStyle::setStyle(QStringLiteral("ImageViewerStyle"));
    // The program's font at one size, KDE's default, so that layouts and pictures are the same
    // wherever the tests run, rather than at whatever the platform gives (9 points offscreen).
    iv::useProgramFont(10);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsFolder.path());
#ifdef Q_OS_WIN
    // Qt ignores XDG_CACHE_HOME here, so each process needs a thumbnail cache of its own this way.
    static const QString organization = QStringLiteral("ImageViewerTests/") + QString::number(QCoreApplication::applicationPid());
    static const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/') + organization;
    // At exit, and registered first so that it runs last: the thumbnail database is open until then.
    std::atexit([] { QDir(cacheRoot).removeRecursively(); });
#else
    const QString organization = QStringLiteral("ImageViewerTests");
#endif
    QCoreApplication::setOrganizationName(organization);
    iv::Preferences::instance();  // GUI-thread initialization before thumbnail workers start
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
