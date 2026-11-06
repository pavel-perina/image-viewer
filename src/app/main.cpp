#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStringList>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(ImageViewerPlugin)

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Image Viewer"));
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    const QStringList arguments = QCoreApplication::arguments();

    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("initialPath"), arguments.value(1)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("ImageViewer", "Main");
    return QCoreApplication::exec();
}
