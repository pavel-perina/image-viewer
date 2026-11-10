#pragma once

// What the UI tests share: includes, helpers and fixtures. Each test file covers one area.

#include <gtest/gtest.h>

#include <QColor>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMimeData>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickTextureFactory>
#include <QQuickView>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSGRendererInterface>
#include <QSettings>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTest>
#include <QQmlComponent>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QtQml/QQmlExtensionPlugin>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <functional>
#include <memory>

#include "test_exif.h"
#include "test_jpeg.h"
#include "test_tiff.h"
#include "core/color.h"
#include "core/image_writer.h"
#include "core/jpeg_transform.h"
#include "ui/browse_context.h"
#include "ui/color_settings.h"
#include "ui/file_clipboard.h"
#include "ui/directory_tree_model.h"
#include "ui/folder_listing.h"
#include "ui/folder_lists.h"
#include "ui/key_hold.h"
#include "ui/key_names.h"
#include "ui/file_operations.h"
#include "ui/histogram_handle.h"
#include "ui/histogram_view.h"
#include "ui/image_view.h"
#include "ui/subfolder_list.h"
#include "ui/thumbnail_provider.h"
#include "ui/thumbnail_store.h"

using iv::BrowseContext;
using iv::ImageView;
using iv::test::encodeJpeg;
using iv::test::JpegOptions;
using iv::test::Rgb;
using iv::test::TiffImage;

inline bool waitUntil(const std::function<bool()>& condition, int timeoutMs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return true;
}

inline void writeJpeg(const QString& path, const JpegOptions& options = {}) {
    iv::test::writeFile(QFileInfo(path).filesystemFilePath(), encodeJpeg(options));
}

// A window showing one ImageView, whose rendered output can be inspected.
struct ViewFixture {
    QQuickWindow window;
    ImageView* view;

    ViewFixture() : view(new ImageView(window.contentItem())) {
        window.resize(400, 400);
        view->setSize(QSizeF(400, 400));
        window.show();
    }

    bool load(const QString& path) {
        view->setSource(path);
        return waitUntil([&] { return view->status() != ImageView::Status::Loading; });
    }

    QImage grab() {
        // Tiles arrive asynchronously; let them land before capturing.
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 300) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(5);
        }
        return window.grabWindow();
    }
};

inline void expectColor(const QImage& image, int x, int y, QColor expected) {
    const QColor actual = image.pixelColor(x, y);
    EXPECT_NEAR(actual.red(), expected.red(), 12) << "at " << x << "," << y;
    EXPECT_NEAR(actual.green(), expected.green(), 12) << "at " << x << "," << y;
    EXPECT_NEAR(actual.blue(), expected.blue(), 12) << "at " << x << "," << y;
}

// What Qt hands the provider: the URL without its scheme and provider name.
inline QString thumbnailId(const QString& path) {
    const QString prefix = QStringLiteral("image://") + QLatin1StringView(iv::ThumbnailProvider::kProviderId) + QLatin1Char('/');
    return iv::ThumbnailProvider::urlForPath(path).mid(prefix.size());
}

// A context showing a folder of a.jpg, b.jpg and so on.
struct SelectionFixture {
    QTemporaryDir dir;
    BrowseContext context;

    explicit SelectionFixture(int count = 6) {
        for (int i = 0; i < count; ++i) {
            writeJpeg(dir.filePath(QString(QChar(u'a' + i)) + QStringLiteral(".jpg")));
        }
        context.openPath(dir.path());
        EXPECT_TRUE(waitUntil([&] { return context.fileCount() == count && context.selectedCount() == std::min(count, 1); }));
    }

    QString names() const {
        QString out;
        for (const QString& path : context.selectedPaths()) {
            out += QFileInfo(path).baseName();
        }
        return out;
    }
};

// The value of a row of ImageView::details, or empty.
inline QString detail(const ImageView& view, const QString& label) {
    for (const QVariant& row : view.details()) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("label")).toString() == label) {
            return map.value(QStringLiteral("value")).toString();
        }
    }
    return {};
}

inline std::shared_ptr<const iv::Histogram> histogramOf(const ImageView& view) {
    return view.histogram().value<std::shared_ptr<const iv::Histogram>>();
}

// Puts a HistogramView in a window and returns what it draws.
inline QImage drawHistogram(const std::shared_ptr<const iv::Histogram>& histogram, bool log = false, bool fit = false) {
    QQuickWindow window;
    window.resize(256, 100);
    auto* view = new iv::HistogramView(window.contentItem());
    view->setSize(QSizeF(256, 100));
    view->setHistogram(QVariant::fromValue(histogram));
    view->setLogScale(log);
    view->setFitRange(fit);
    window.show();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 100) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    return window.grabWindow();
}

inline int barHeight(const QImage& image, int x) {
    const QRgb background = qRgb(16, 16, 16);
    int bar = 0;
    for (int y = image.height() - 1; y >= 0 && image.pixel(x, y) != background; --y) {
        ++bar;
    }
    return bar;
}

inline std::shared_ptr<iv::Histogram> emptyHistogram(int bins, bool gray) {
    auto histogram = std::make_shared<iv::Histogram>();
    histogram->gray = gray;
    histogram->bitsPerBin = bins == 256 ? 8 : 16;
    histogram->high = bins - 1;
    for (int c = 0; c < (gray ? 1 : 3); ++c) {
        histogram->channels[static_cast<std::size_t>(c)].assign(static_cast<std::size_t>(bins), 0);
    }
    return histogram;
}

// Puts settings back for the next test.
struct ColorSettingsGuard {
    ~ColorSettingsGuard() {
        iv::ColorSettings::instance()->setEnabled(true);
        iv::ColorSettings::instance()->useSrgbDisplay();
    }
};

inline QString writeAdobeJpeg(const QTemporaryDir& dir, const QString& name) {
    const QString path = dir.filePath(name);
    JpegOptions options;
    options.iccProfile = iv::adobeRgbProfile();
    options.pixel = [](int, int) { return Rgb{60, 200, 90}; };
    writeJpeg(path, options);
    return path;
}

// The real browser page in a window, for what only exists in QML. Any QML warning fails the
// test, since a mistake in a binding shows up as nothing more than that.
struct BrowserPageFixture {
    QTemporaryDir dir;
    BrowseContext context;
    QQuickView view;

    BrowserPageFixture() {
        for (const char* name : {"a.jpg", "b.jpg", "c.jpg", "d.jpg"}) {
            writeJpeg(dir.filePath(QString::fromLatin1(name)));
        }
        EXPECT_TRUE(QDir(dir.path()).mkpath(QStringLiteral("inner/deeper")));

        view.engine()->addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
        QObject::connect(view.engine(), &QQmlEngine::warnings, [](const QList<QQmlError>& warnings) {
            for (const QQmlError& warning : warnings) {
                ADD_FAILURE() << warning.toString().toStdString();
            }
        });
        view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), false}});
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(1300, 700);  // room for all the thumbnails in one row
        view.loadFromModule("ImageViewer", "BrowserPage");
        view.show();
        EXPECT_NE(view.rootObject(), nullptr);
        EXPECT_TRUE(waitUntil([&] { return view.isExposed(); }));
        context.openPath(dir.path());
        EXPECT_TRUE(waitUntil([&] { return context.fileCount() == 4; }));
    }

    QQuickItem* find(const char* className) const {
        for (QQuickItem* item : view.rootObject()->findChildren<QQuickItem*>()) {
            if (item->inherits(className)) {
                return item;
            }
        }
        return nullptr;
    }

    void clickAt(QPointF point, Qt::KeyboardModifiers modifiers) {
        const QPointF global = view.mapToGlobal(point);
        QMouseEvent press(QEvent::MouseButtonPress, point, point, global, Qt::LeftButton, Qt::LeftButton, modifiers);
        QCoreApplication::sendEvent(&view, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, point, point, global, Qt::LeftButton, Qt::NoButton, modifiers);
        QCoreApplication::sendEvent(&view, &release);
        QCoreApplication::processEvents();
    }

    // Clicks a thumbnail, which are 180 by 200 and laid out from the corner of the grid.
    void clickFile(int index, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QQuickItem* grid = find("QQuickGridView");
        ASSERT_NE(grid, nullptr);
        ASSERT_TRUE(waitUntil([&] { return grid->property("count").toInt() == 4; }));
        QCoreApplication::processEvents();
        clickAt(grid->mapToScene(QPointF(90 + 180 * index, 100)), modifiers);
    }

    QString selected() const {
        QString out;
        for (const QString& path : context.selectedPaths()) {
            out += QFileInfo(path).baseName();
        }
        return out;
    }
};

inline double srgbToLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

inline double linearToSrgb(double c) {
    c = std::clamp(c, 0.0, 1.0);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055;
}

inline double mks2013(double x) {
    x = std::abs(x);
    if (x <= 0.5) {
        return 17.0 / 16.0 - 1.75 * x * x;
    }
    if (x <= 1.5) {
        return (1.0 - x) * (1.75 - x);
    }
    return x <= 2.5 ? -0.125 * (x - 2.5) * (x - 2.5) : 0.0;
}

// What the window shows, or a null image once it has been closed: grabbing a closed window
// crashes in Qt's render thread, and someone may close it while the test runs on a desktop.
inline QImage grabIfShown(QQuickWindow& window) {
    if (!window.isVisible() || !window.isExposed()) {
        return {};
    }
    return window.grabWindow().convertToFormat(QImage::Format_RGB32);
}

struct TreeFixture : BrowserPageFixture {
    QQuickItem* tree = find("QQuickTreeView");
    iv::DirectoryTreeModel* model = qobject_cast<iv::DirectoryTreeModel*>(tree->property("model").value<QObject*>());

    QString path(const QString& relative) const { return QDir(context.directory()).filePath(relative); }

    int rowOf(const QString& path) const {
        int row = -1;
        QMetaObject::invokeMethod(tree, "rowAtIndex", Q_RETURN_ARG(int, row), Q_ARG(QModelIndex, model->indexForPath(path)));
        return row;
    }

    // Where the name of a row is, past its arrow.
    QPointF nameOf(const QString& path) const {
        QQuickItem* row = nullptr;
        QMetaObject::invokeMethod(tree, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, row), Q_ARG(QModelIndex, model->indexForPath(path)));
        return row ? row->mapToScene(QPointF(row->width() - 20, row->height() / 2)) : QPointF();
    }

    void settle() {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 100) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
    }
};

// What each row of the tree below the opened folder paints, as "label=folder it opens".
inline QStringList treeRows(TreeFixture& f) {
    QStringList rows;
    const QString base = f.context.directory() + QLatin1Char('/');
    for (int row = 0; row < f.tree->property("rows").toInt(); ++row) {
        QModelIndex index;
        QMetaObject::invokeMethod(f.tree, "index", Q_RETURN_ARG(QModelIndex, index), Q_ARG(int, row), Q_ARG(int, 0));
        const QString path = f.model->pathForIndex(index);
        if (!path.startsWith(base)) {
            continue;
        }
        QQuickItem* item = nullptr;
        QMetaObject::invokeMethod(f.tree, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(QModelIndex, index));
        const auto* label = item ? item->property("contentItem").value<QObject*>() : nullptr;
        rows << (label ? label->property("text").toString() : QStringLiteral("?")) + QLatin1Char('=') + path.mid(base.size());
    }
    return rows;
}

// The real system trash is used, and main() has moved it into a temporary home.
inline QString trashFolder() { return qEnvironmentVariable("XDG_DATA_HOME") + QStringLiteral("/Trash/files"); }

inline bool isInTrash(const QString& name) {
#ifdef Q_OS_WIN
    // The Recycle Bin renames what it receives, so a name cannot be looked up there.
    Q_UNUSED(name);
    return true;
#else
    return QDir(trashFolder()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).contains(name);
#endif
}

// A leading dot is what makes a folder hidden elsewhere.
inline void hideOnWindows([[maybe_unused]] const QString& path) {
#ifdef Q_OS_WIN
    QProcess::execute(QStringLiteral("attrib"), {QStringLiteral("+h"), QDir::toNativeSeparators(path)});
#endif
}

struct FileOpsFixture : SelectionFixture {
    QTemporaryDir other;
    QStringList problems;

    explicit FileOpsFixture(int count = 6) : SelectionFixture(count) {
        QObject::connect(&context, &BrowseContext::operationFinished, [this](const QStringList& p) { problems = p; });
    }

    // Waits for the job, and for the scan it started to arrive.
    void settle(const std::function<bool()>& condition) {
        EXPECT_TRUE(waitUntil([&] { return !context.busy() && condition(); }));
    }

    QString path(const QString& name) const { return dir.filePath(name); }
    QString otherPath(const QString& name) const { return other.filePath(name); }

    static qint64 size(const QString& path) { return QFileInfo(path).size(); }

    void writeBytes(const QString& path, int count) const {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(count, 'x'));
    }
};

inline QStringList rowTexts(const QVariantList& rows) {
    QStringList texts;
    for (const QVariant& row : rows) {
        texts << row.toMap().value(QStringLiteral("text")).toString();
    }
    return texts;
}

inline QQuickItem* findNamed(QQuickItem* parent, const QString& name) {
    if (parent->objectName() == name) {
        return parent;
    }
    for (QQuickItem* child : parent->childItems()) {
        if (QQuickItem* found = findNamed(child, name)) {
            return found;
        }
    }
    return nullptr;
}

// QPlatformDialogHelper::No, which is what Dialog.No is.
inline int QQuickDialogNoButton() { return 0x00010000; }

// An image larger than the view, so that fitting it shrinks it.
struct LoupeFixture : ViewFixture {
    QTemporaryDir dir;

    LoupeFixture() {
        JpegOptions options;
        options.width = 1600;
        options.height = 1600;
        writeJpeg(dir.filePath(QStringLiteral("large.jpg")), options);
        EXPECT_TRUE(load(dir.filePath(QStringLiteral("large.jpg"))));
    }

    void mouse(QEvent::Type type, QPointF at, Qt::MouseButtons buttons) {
        QMouseEvent event(type, at, at, view->mapToGlobal(at), Qt::LeftButton, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &event);
    }
    void press(QPointF at) { mouse(QEvent::MouseButtonPress, at, Qt::LeftButton); }
    void move(QPointF at) { mouse(QEvent::MouseMove, at, Qt::LeftButton); }
    void release(QPointF at) { mouse(QEvent::MouseButtonRelease, at, Qt::NoButton); }

    void wheel(int delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QPointF at(200, 200);
        QWheelEvent event(at, view->mapToGlobal(at), QPoint(), QPoint(0, delta), Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&window, &event);
    }
};

// 64x32, so that a quarter turn shows in the size.
inline void writeWideJpeg(const QString& path, int height = 32) {
    writeJpeg(path, {.width = 64, .height = height, .orientation = 1});
}

inline QSize jpegSize(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray bytes = file.readAll();
    const auto decoded = iv::test::decodeJpeg({bytes.begin(), bytes.end()});
    return {decoded.width, decoded.height};
}

// Stored 64 x 32 and turned a quarter by EXIF, so upright it is 32 x 64.
inline QString writeTurnedJpeg(const QTemporaryDir& dir, const QString& name = QStringLiteral("turned.jpg")) {
    const QString path = dir.filePath(name);
    writeJpeg(path, {.width = 64, .height = 32, .orientation = 6});
    return path;
}

inline bool settled(ImageView* view) {
    return waitUntil([&] { return view->status() == ImageView::Status::Ready; });
}

// Stored 8 x 4 at 16 bits and turned a quarter, so upright it is 4 x 8. Red counts columns and
// green rows of the stored image.
inline QString writeTurnedTiff(const QTemporaryDir& dir) {
    const QString path = dir.filePath(QStringLiteral("turned.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(),
                        {{.width = 8, .height = 4, .bits = 16, .orientation = 6, .pixel = [](int x, int y) {
                              return Rgb{static_cast<std::uint8_t>(x * 20), static_cast<std::uint8_t>(y * 20), 100};
                          }}});
    return path;
}

// 400 x 300, light with a dark rectangle turned 4 degrees clockwise around the middle.
inline QString writeTiltedJpeg(const QTemporaryDir& dir) {
    const QString path = dir.filePath(QStringLiteral("tilted.jpg"));
    writeJpeg(path, {.width = 400, .height = 300, .pixel = [](int x, int y) {
                         const double a = 4 * std::numbers::pi / 180;
                         const double dx = x + 0.5 - 200;
                         const double dy = y + 0.5 - 150;
                         const double u = dx * std::cos(a) + dy * std::sin(a);
                         const double v = -dx * std::sin(a) + dy * std::cos(a);
                         const std::uint8_t value = std::abs(u) < 140 && std::abs(v) < 90 ? 30 : 220;
                         return Rgb{value, value, value};
                     }});
    return path;
}

// The large view on a folder, for comparing.
struct ComparePage {
    BrowseContext context;
    QQuickView view;
    QObject* page = nullptr;
    ImageView* image = nullptr;
    ImageView* other = nullptr;
    QObject* splitter = nullptr;

    explicit ComparePage(const QString& open) {
        view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(900, 600);
        view.loadFromModule("ImageViewer", "ViewerPage");
        view.show();
        // Laid out first, as zooming a view of no size yet does nothing to see.
        EXPECT_TRUE(waitUntil([&] { return view.isExposed(); }));
        page = view.rootObject();
        image = page->property("view").value<ImageView*>();
        other = page->findChild<QQuickItem*>(QStringLiteral("otherSurface"))->property("view").value<ImageView*>();
        splitter = page->findChild<QObject*>(QStringLiteral("compareSplitter"));
        context.openPath(open);
    }
    QString comparing() const { return page->property("comparing").toString(); }
    bool active() const { return splitter->property("active").toBool(); }
};

inline QString writeGradient(const QTemporaryDir& dir, const QString& name, int width, int height, bool gray) {
    const QString path = dir.filePath(name);
    writeJpeg(path, {.width = width, .height = height, .pixel = [=](int x, int y) {
                         const auto v = static_cast<std::uint8_t>(x * 255 / width);
                         return gray ? Rgb{v, v, v} : Rgb{v, static_cast<std::uint8_t>(y * 255 / height), 200};
                     }});
    return path;
}

inline QStringList listingNames(const iv::FolderListing& listing) {
    QStringList names;
    for (int row = 0; row < listing.count(); ++row) {
        names << listing.get(row).value(QStringLiteral("name")).toString();
    }
    return names;
}

// Each character as a key press, as QTest::keyClicks does only for widgets.
inline void typeText(QWindow* window, const QString& text) {
    for (const QChar c : text) {
        QTest::keyClick(window, c.toLatin1());
    }
}

inline void touchFile(const QString& path, int bytes = 0) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(bytes, 'x'));
}

// A file chooser in a window of its own, with a folder holding a.jpg, b.png and a subfolder.
struct ChooserFixture {
    QTemporaryDir dir;
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* window = nullptr;
    QObject* chooser = nullptr;
    std::unique_ptr<QSignalSpy> chosen;
    std::unique_ptr<QSignalSpy> cancelled;

    explicit ChooserFixture(const QString& mode) {
        touchFile(dir.filePath(QStringLiteral("a.jpg")));
        touchFile(dir.filePath(QStringLiteral("b.png")));
        EXPECT_TRUE(QDir(dir.path()).mkpath(QStringLiteral("sub")));
        QObject::connect(&engine, &QQmlEngine::warnings, [](const QList<QQmlError>& warnings) {
            for (const QQmlError& warning : warnings) {
                ADD_FAILURE() << warning.toString().toStdString();
            }
        });
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            import ImageViewer
            ApplicationWindow {
                width: 1000; height: 700; visible: true
                property alias chooser: chooser
                FileChooser { id: chooser }
            }
        )", QUrl());
        EXPECT_TRUE(component.isReady()) << component.errorString().toStdString();
        root.reset(component.create());
        window = qobject_cast<QQuickWindow*>(root.get());
        chooser = root->property("chooser").value<QObject*>();
        chooser->setProperty("mode", mode);
        chooser->setProperty("types", QVariantList{
            QVariantMap{{"label", "JPEG"}, {"extensions", QStringList{"jpg", "jpeg"}}},
            QVariantMap{{"label", "PNG"}, {"extensions", QStringList{"png"}}},
        });
        chosen = std::make_unique<QSignalSpy>(chooser, SIGNAL(chosen(QString)));
        cancelled = std::make_unique<QSignalSpy>(chooser, SIGNAL(cancelled()));
        EXPECT_TRUE(waitUntil([&] { return window->isExposed(); }));
    }

    QObject* part(const char* name) const { return chooser->property(name).value<QObject*>(); }
    QQuickItem* item(const char* name) const { return qobject_cast<QQuickItem*>(part(name)); }
    QObject* listing() const { return chooser->findChild<iv::FolderListing*>(); }
    QString name() const { return item("nameField")->property("text").toString(); }
    QString directory() const { return chooser->property("directory").toString(); }

    void start(const QString& name) {
        QMetaObject::invokeMethod(chooser, "start", Q_ARG(QVariant, dir.path()), Q_ARG(QVariant, name));
        ASSERT_TRUE(waitUntil([&] { return chooser->property("opened").toBool(); }));
        ASSERT_TRUE(waitUntil([&] { return !listing()->property("loading").toBool() && directory() == dir.path(); }));
    }

    void click(QQuickItem* target) {
        ASSERT_NE(target, nullptr);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
    }

    // Makes an entry current as a click would.
    void pick(const QString& entry) {
        int row = -1;
        QMetaObject::invokeMethod(listing(), "indexOf", Q_RETURN_ARG(int, row), Q_ARG(QString, entry));
        ASSERT_GE(row, 0) << entry.toStdString();
        QQuickItem* list = item("fileList");
        list->setProperty("currentIndex", row);
        QMetaObject::invokeMethod(list, "clicked", Q_ARG(int, row));
    }
};

// The whole application, its window active, as a user has it. Records the QML warnings.
struct MainFixture {
    QTemporaryDir dir;
    QQmlApplicationEngine engine;
    QQuickWindow* window = nullptr;
    BrowseContext* context = nullptr;
    QStringList warnings;

    // On a folder of files a.jpg, b.jpg and so on, and of subfolders made first, as the tree
    // lists each folder once. Waits until the files are listed.
    explicit MainFixture(int files = 4, const QStringList& folders = {}) {
        for (const QString& folder : folders) {
            QDir(dir.path()).mkpath(folder);
        }
        for (int i = 0; i < files; ++i) {
            writeJpeg(dir.filePath(QStringLiteral("%1.jpg").arg(QChar('a' + i))));
        }
        open(dir.path());
        if (context) {
            waitUntil([&] { return context->fileCount() == files; });
        }
    }

    // On what prepare puts in the folder, starting at one of its files, or the folder itself
    // when start is empty.
    MainFixture(const std::function<void(const QTemporaryDir&)>& prepare, const QString& start) {
        prepare(dir);
        open(start.isEmpty() ? dir.path() : dir.filePath(start));
    }

    void open(const QString& path) {
        QObject::connect(&engine, &QQmlEngine::warnings, [this](const QList<QQmlError>& list) {
            for (const QQmlError& warning : list) {
                warnings << warning.toString();
            }
        });
        engine.addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
        engine.setInitialProperties({{QStringLiteral("initialPath"), path}});
        engine.loadFromModule("ImageViewer", "Main");
        if (engine.rootObjects().size() != 1) {
            return;
        }
        window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        window->requestActivate();
        waitUntil([&] { return window->isExposed(); });
        context = window->findChild<BrowseContext*>();
    }

    QObject* find(const QString& name) const { return window->findChild<QObject*>(name); }
    QString focused() const {
        QQuickItem* item = window->activeFocusItem();
        return item ? QString::fromLatin1(item->metaObject()->className()) + QLatin1Char(' ') + item->objectName() : QStringLiteral("none");
    }
};

// Files dragged from another application and dropped at a point of the window. Returns whether
// something took them.
inline bool dropFiles(QQuickWindow* window, QPointF at, const QStringList& paths, Qt::KeyboardModifiers modifiers = {}) {
    QMimeData mime;
    QList<QUrl> urls;
    for (const QString& path : paths) {
        urls << QUrl::fromLocalFile(path);
    }
    mime.setUrls(urls);
    const Qt::DropActions actions = Qt::CopyAction | Qt::MoveAction;
    QDragEnterEvent enter(at.toPoint(), actions, &mime, Qt::LeftButton, modifiers);
    QCoreApplication::sendEvent(window, &enter);
    QDragMoveEvent move(at.toPoint(), actions, &mime, Qt::LeftButton, modifiers);
    QCoreApplication::sendEvent(window, &move);
    QDropEvent drop(at, actions, &mime, Qt::LeftButton, modifiers);
    QCoreApplication::sendEvent(window, &drop);
    return drop.isAccepted();
}

// The middle of a folder's row in the tree, scrolled into sight once the tree lists it. Read just
// before it is used, as the tree centres the open folder again as each of its parents loads.
inline QPointF treeRowCentre(MainFixture& f, const QString& folder) {
    auto* tree = qobject_cast<QQuickItem*>(f.find(QStringLiteral("folderTree")));
    auto* model = qobject_cast<iv::DirectoryTreeModel*>(tree->property("model").value<QObject*>());
    QPointF centre;
    const bool shown = waitUntil([&] {
        const QModelIndex index = model->indexForPath(folder);
        int number = -1;
        QMetaObject::invokeMethod(tree, "rowAtIndex", Q_RETURN_ARG(int, number), Q_ARG(QModelIndex, index));
        if (number < 0) {
            return false;
        }
        QQmlExpression(qmlContext(tree), tree, QStringLiteral("positionViewAtRow(%1, TableView.Contain)").arg(number)).evaluate();
        QCoreApplication::processEvents();
        QQuickItem* row = nullptr;
        QMetaObject::invokeMethod(tree, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, row), Q_ARG(QModelIndex, index));
        if (!row) {
            return false;
        }
        centre = row->mapToScene(QPointF(row->width() / 2, row->height() / 2));
        return tree->contains(tree->mapFromScene(centre));
    });
    return shown ? centre : QPointF();
}
