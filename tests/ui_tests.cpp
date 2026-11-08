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
#include <QQuickItem>
#include <QQuickTextureFactory>
#include <QQuickView>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSGRendererInterface>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <QQmlComponent>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QtQml/QQmlExtensionPlugin>

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
#include "ui/directory_tree_model.h"
#include "ui/folder_lists.h"
#include "ui/key_names.h"
#include "ui/file_operations.h"
#include "ui/histogram_handle.h"
#include "ui/histogram_view.h"
#include "ui/image_view.h"
#include "ui/subfolder_list.h"
#include "ui/thumbnail_provider.h"
#include "ui/thumbnail_store.h"

Q_IMPORT_QML_PLUGIN(ImageViewerPlugin)

using iv::BrowseContext;
using iv::ImageView;
using iv::test::encodeJpeg;
using iv::test::JpegOptions;
using iv::test::Rgb;
using iv::test::TiffImage;

namespace {

bool waitUntil(const std::function<bool()>& condition, int timeoutMs = 5000) {
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

void writeJpeg(const QString& path, const JpegOptions& options = {}) {
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

void expectColor(const QImage& image, int x, int y, QColor expected) {
    const QColor actual = image.pixelColor(x, y);
    EXPECT_NEAR(actual.red(), expected.red(), 12) << "at " << x << "," << y;
    EXPECT_NEAR(actual.green(), expected.green(), 12) << "at " << x << "," << y;
    EXPECT_NEAR(actual.blue(), expected.blue(), 12) << "at " << x << "," << y;
}

}  // namespace

TEST(BrowseContext, SortsNaturallyAndNavigates) {
    QTemporaryDir dir;
    for (const char* name : {"img10.jpg", "img2.JPG", "img1.jpeg"}) {
        writeJpeg(dir.filePath(QString::fromLatin1(name)));
    }
    QFile notes(dir.filePath(QStringLiteral("notes.txt")));
    ASSERT_TRUE(notes.open(QIODevice::WriteOnly));
    notes.close();

    BrowseContext context;
    context.openPath(dir.filePath(QStringLiteral("img2.JPG")));
    EXPECT_EQ(context.currentName(), QStringLiteral("img2.JPG"));  // known before the scan ends

    ASSERT_TRUE(waitUntil([&] { return context.fileCount() == 3; }));
    EXPECT_EQ(context.currentIndex(), 1);
    EXPECT_TRUE(context.hasPrevious());
    EXPECT_TRUE(context.hasNext());

    context.next();
    EXPECT_EQ(context.currentName(), QStringLiteral("img10.jpg"));
    EXPECT_FALSE(context.hasNext());
    context.next();
    EXPECT_EQ(context.currentName(), QStringLiteral("img10.jpg"));

    context.previous();
    context.previous();
    EXPECT_EQ(context.currentName(), QStringLiteral("img1.jpeg"));
    EXPECT_FALSE(context.hasPrevious());
}

TEST(BrowseContext, OpeningADirectoryShowsItsFirstImage) {
    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("b.jpg")));
    writeJpeg(dir.filePath(QStringLiteral("a.jpg")));

    BrowseContext context;
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return context.fileCount() == 2; }));
    EXPECT_EQ(context.currentName(), QStringLiteral("a.jpg"));
    EXPECT_EQ(context.currentIndex(), 0);
}

TEST(BrowseContext, TwoContextsAreIndependent) {
    QTemporaryDir first, second;
    writeJpeg(first.filePath(QStringLiteral("one.jpg")));
    writeJpeg(second.filePath(QStringLiteral("two.jpg")));

    BrowseContext a, b;
    a.openPath(first.path());
    b.openPath(second.path());
    ASSERT_TRUE(waitUntil([&] { return a.fileCount() == 1 && b.fileCount() == 1; }));
    EXPECT_EQ(a.currentName(), QStringLiteral("one.jpg"));
    EXPECT_EQ(b.currentName(), QStringLiteral("two.jpg"));
}

TEST(BrowseContext, ExposesFilesAsAModel) {
    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("a.jpg")));
    writeJpeg(dir.filePath(QStringLiteral("b.jpg")));

    BrowseContext context;
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return context.fileCount() == 2; }));

    QAbstractItemModel* model = context.files();
    ASSERT_EQ(model->rowCount(), 2);
    const QHash<int, QByteArray> roles = model->roleNames();
    const auto value = [&](int row, const QByteArray& role) {
        return model->data(model->index(row, 0), roles.key(role)).toString();
    };
    EXPECT_EQ(value(1, "name"), QStringLiteral("b.jpg"));
    EXPECT_EQ(value(1, "path"), dir.filePath(QStringLiteral("b.jpg")));
    EXPECT_TRUE(value(1, "thumbnail").startsWith(QStringLiteral("image://thumbnails/")));

    context.setCurrentIndex(1);
    EXPECT_EQ(context.currentName(), QStringLiteral("b.jpg"));
    context.setCurrentIndex(99);
    EXPECT_EQ(context.currentIndex(), 1);
    context.setCurrentIndex(-1);
    EXPECT_EQ(context.currentIndex(), 1);
}

TEST(BrowseContext, EmptyDirectoryHasNoCurrentImage) {
    QTemporaryDir dir;
    BrowseContext context;
    context.openPath(dir.path());
    QCoreApplication::processEvents();
    QThread::msleep(50);
    QCoreApplication::processEvents();
    EXPECT_EQ(context.fileCount(), 0);
    EXPECT_TRUE(context.currentPath().isEmpty());
    EXPECT_FALSE(context.hasNext());
}

TEST(BrowseContext, BreadcrumbsWalkFromTheRoot) {
    QTemporaryDir dir;
    const QString nested = dir.filePath(QStringLiteral("a b/c"));
    ASSERT_TRUE(QDir().mkpath(nested));

    BrowseContext context;
    context.openPath(nested);
    const QVariantList crumbs = context.breadcrumbs();
    ASSERT_GE(crumbs.size(), 3);
    EXPECT_EQ(crumbs.first().toMap().value("path").toString(), QDir::rootPath());
    EXPECT_EQ(crumbs.last().toMap().value("name").toString(), QStringLiteral("c"));
    EXPECT_EQ(crumbs.last().toMap().value("path").toString(), nested);
    EXPECT_EQ(crumbs.at(crumbs.size() - 2).toMap().value("name").toString(), QStringLiteral("a b"));

    context.openPath(QDir::rootPath());
    ASSERT_EQ(context.breadcrumbs().size(), 1);
    EXPECT_EQ(context.breadcrumbs().first().toMap().value("path").toString(), QDir::rootPath());
}

TEST(DirectoryTreeModel, ShowsOneColumnAndMapsPaths) {
    QTemporaryDir dir;
    iv::DirectoryTreeModel model;
    EXPECT_EQ(model.columnCount(), 1);
    const QModelIndex index = model.indexForPath(dir.path());
    ASSERT_TRUE(index.isValid());
    EXPECT_EQ(model.pathForIndex(index), dir.path());
}

namespace {

// What Qt hands the provider: the URL without its scheme and provider name.
QString thumbnailId(const QString& path) {
    const QString prefix = QStringLiteral("image://") + QLatin1StringView(iv::ThumbnailProvider::kProviderId) + QLatin1Char('/');
    return iv::ThumbnailProvider::urlForPath(path).mid(prefix.size());
}

}  // namespace

TEST(ThumbnailProvider, DeliversThumbnailsForAnyFileName) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("100% #1 ?é.jpg"));
    JpegOptions options;
    options.width = 1000;
    options.height = 500;
    writeJpeg(path, options);

    iv::ThumbnailProvider provider;
    std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(thumbnailId(path), {}));
    bool finished = false;
    QObject::connect(response.get(), &QQuickImageResponse::finished, response.get(),
                     [&] { finished = true; });
    ASSERT_TRUE(waitUntil([&] { return finished; }));
    EXPECT_TRUE(response->errorString().isEmpty()) << response->errorString().toStdString();

    std::unique_ptr<QQuickTextureFactory> factory(response->textureFactory());
    ASSERT_TRUE(factory);
    EXPECT_EQ(factory->textureSize(), QSize(256, 128));
}

TEST(ThumbnailProvider, MakesTheRequestedSize) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.jpg"));
    JpegOptions options;
    options.width = 1000;
    options.height = 500;
    writeJpeg(path, options);
    iv::ThumbnailProvider provider;
    std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(thumbnailId(path), {164, 164}));
    bool finished = false;
    QObject::connect(response.get(), &QQuickImageResponse::finished, response.get(), [&] { finished = true; });
    ASSERT_TRUE(waitUntil([&] { return finished; }));
    std::unique_ptr<QQuickTextureFactory> factory(response->textureFactory());
    ASSERT_TRUE(factory);
    EXPECT_EQ(factory->textureSize(), QSize(164, 82));
}

TEST(ThumbnailProvider, ReportsUnreadableFiles) {
    iv::ThumbnailProvider provider;
    std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(thumbnailId(QStringLiteral("/does/not/exist.jpg")), {}));
    // This fails so fast that finished() can come before anyone could connect to it, which
    // Qt allows for when it loads images, so look at the result instead.
    EXPECT_TRUE(waitUntil([&] { return !response->errorString().isEmpty(); }));
}

// Qt deletes a response only after finished(), so a cancelled one must still emit it.
TEST(ThumbnailProvider, CancelledResponsesFinishExactlyOnce) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("large.jpg"));
    JpegOptions options;
    options.width = 2000;
    options.height = 2000;
    writeJpeg(path, options);

    iv::ThumbnailProvider provider;
    std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(thumbnailId(path), {}));
    int finishedCount = 0;
    QObject::connect(response.get(), &QQuickImageResponse::finished, response.get(),
                     [&] { ++finishedCount; });
    response->cancel();
    ASSERT_TRUE(waitUntil([&] { return finishedCount > 0; }));

    // Give a task that was already running the time to try finishing a second time.
    QThread::msleep(300);
    QCoreApplication::processEvents();
    EXPECT_EQ(finishedCount, 1);
    EXPECT_FALSE(response->errorString().isEmpty());
}

TEST(ThumbnailProvider, DestroyingPendingResponsesIsSafe) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("large.jpg"));
    JpegOptions options;
    options.width = 3000;
    options.height = 3000;
    writeJpeg(path, options);

    const QString id = thumbnailId(path);
    iv::ThumbnailProvider provider;
    for (int i = 0; i < 50; ++i) {
        std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(id, {}));
        if (i % 2 == 0) {
            response->cancel();
        }
    }
    QThread::msleep(100);
    SUCCEED();
}

TEST(ImageView, RendersTheImageFittedAndCentred) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("quad.jpg"));
    JpegOptions options;
    options.width = 800;
    options.height = 800;
    options.pixel = [](int x, int y) {
        if (y < 400) {
            return x < 400 ? Rgb{220, 30, 30} : Rgb{30, 220, 30};
        }
        return x < 400 ? Rgb{30, 30, 220} : Rgb{230, 230, 30};
    };
    writeJpeg(path, options);

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    ASSERT_EQ(fixture.view->status(), ImageView::Status::Ready);
    EXPECT_EQ(fixture.view->imageWidth(), 800);
    EXPECT_DOUBLE_EQ(fixture.view->zoom(), 0.5);

    const QImage image = fixture.grab();
    expectColor(image, 100, 100, QColor(220, 30, 30));
    expectColor(image, 300, 100, QColor(30, 220, 30));
    expectColor(image, 100, 300, QColor(30, 30, 220));
    expectColor(image, 300, 300, QColor(230, 230, 30));
}

TEST(ImageView, AppliesExifOrientation) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("rotated.jpg"));
    JpegOptions options;
    options.width = 800;
    options.height = 400;
    options.orientation = 6;  // rotate 90 degrees clockwise to display
    options.pixel = [](int x, int) { return x < 400 ? Rgb{220, 30, 30} : Rgb{30, 30, 220}; };
    writeJpeg(path, options);

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    EXPECT_EQ(fixture.view->imageWidth(), 400);
    EXPECT_EQ(fixture.view->imageHeight(), 800);

    // Stored left half (red) ends up on top after a clockwise quarter turn.
    const QImage image = fixture.grab();
    expectColor(image, 200, 100, QColor(220, 30, 30));
    expectColor(image, 200, 300, QColor(30, 30, 220));
}

TEST(ImageView, ActualSizeDrawsTheFullResolutionLevelAcrossTiles) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("large.jpg"));
    JpegOptions options;
    options.width = 4400;
    options.height = 4400;
    // The tile boundary at 2048 lies in the middle of the view at 100%.
    options.pixel = [](int x, int) { return x < 2048 ? Rgb{220, 30, 30} : Rgb{30, 30, 220}; };
    writeJpeg(path, options);

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    fixture.view->actualSize();
    EXPECT_DOUBLE_EQ(fixture.view->zoom(), 1.0);

    // At 100% and centred, view x=0 is image x=2000, so the boundary is at view x=48.
    const QImage image = fixture.grab();
    expectColor(image, 20, 200, QColor(220, 30, 30));
    expectColor(image, 100, 200, QColor(30, 30, 220));
}

TEST(ImageView, ShowsPagesAndResetsThemForTheNextImage) {
    QTemporaryDir dir;
    TiffImage red;
    red.width = 200;
    red.height = 200;
    red.pixel = [](int, int) { return Rgb{220, 30, 30}; };
    TiffImage blue = red;
    blue.width = 100;
    blue.pixel = [](int, int) { return Rgb{30, 30, 220}; };
    const QString multi = dir.filePath(QStringLiteral("multi.tif"));
    iv::test::writeTiff(QFileInfo(multi).filesystemFilePath(), {red, blue});
    const QString single = dir.filePath(QStringLiteral("single.jpg"));
    writeJpeg(single);

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(multi));
    ASSERT_EQ(fixture.view->status(), ImageView::Status::Ready);
    EXPECT_EQ(fixture.view->pageCount(), 2);
    EXPECT_EQ(fixture.view->page(), 0);
    EXPECT_EQ(fixture.view->imageWidth(), 200);

    fixture.view->setPage(1);
    ASSERT_TRUE(waitUntil([&] { return fixture.view->page() == 1; }));
    EXPECT_EQ(fixture.view->imageWidth(), 100);
    expectColor(fixture.grab(), 200, 200, QColor(30, 30, 220));

    // Pages that do not exist are ignored.
    fixture.view->setPage(5);
    fixture.view->setPage(-1);
    EXPECT_EQ(fixture.view->page(), 1);

    // Nothing about the previous file survives into the next one.
    ASSERT_TRUE(fixture.load(single));
    ASSERT_TRUE(waitUntil([&] { return fixture.view->status() == ImageView::Status::Ready; }));
    EXPECT_EQ(fixture.view->page(), 0);
    EXPECT_EQ(fixture.view->pageCount(), 1);
    EXPECT_EQ(fixture.view->imageWidth(), 64);

    ASSERT_TRUE(fixture.load(multi));
    ASSERT_TRUE(waitUntil([&] { return fixture.view->status() == ImageView::Status::Ready; }));
    EXPECT_EQ(fixture.view->page(), 0);
    EXPECT_EQ(fixture.view->imageWidth(), 200);
}

TEST(ImageView, SixteenBitGrayStretchesOnRequestAndContrastResetsPerImage) {
    QTemporaryDir dir;
    // A narrow range of 16-bit values, which would look black without stretching.
    TiffImage gray;
    gray.width = 200;
    gray.height = 200;
    gray.bits = 16;
    gray.samples = 1;
    gray.pixel = [](int x, int) { return x < 100 ? Rgb{2, 2, 2} : Rgb{4, 4, 4}; };
    const QString grayPath = dir.filePath(QStringLiteral("gray.tif"));
    iv::test::writeTiff(QFileInfo(grayPath).filesystemFilePath(), {gray});

    TiffImage photo = gray;
    photo.samples = 3;
    photo.pixel = [](int, int) { return Rgb{128, 128, 128}; };
    const QString photoPath = dir.filePath(QStringLiteral("photo.tif"));
    iv::test::writeTiff(QFileInfo(photoPath).filesystemFilePath(), {photo});

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(grayPath));
    EXPECT_TRUE(fixture.view->contrastAdjustable());
    EXPECT_FALSE(fixture.view->autoContrast());
    QImage image = fixture.grab();
    EXPECT_LT(image.pixelColor(250, 200).red(), 20);  // the true values are almost black

    fixture.view->setAutoContrast(true);
    EXPECT_TRUE(fixture.view->autoContrast());
    image = fixture.grab();
    EXPECT_LT(image.pixelColor(150, 200).red(), 40);   // bottom of the stretched range
    EXPECT_GT(image.pixelColor(250, 200).red(), 215);  // top of it

    // The manual choice belongs to that image and does not carry over.
    ASSERT_TRUE(fixture.load(photoPath));
    EXPECT_FALSE(fixture.view->autoContrast());
    ASSERT_TRUE(fixture.load(grayPath));
    EXPECT_FALSE(fixture.view->autoContrast());
}

TEST(ImageView, EightBitImagesHaveNoContrastToAdjust) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("plain.jpg"));
    writeJpeg(path);
    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    EXPECT_FALSE(fixture.view->contrastAdjustable());
}

TEST(ImageView, DrawsAFlatLargeTiffThroughOverviewsAndRealTiles) {
    QTemporaryDir dir;
    TiffImage large;
    large.width = 5000;
    large.height = 5000;
    large.tileSize = 512;
    large.compression = 8;  // deflate
    large.pixel = [](int x, int) { return x < 2500 ? Rgb{220, 30, 30} : Rgb{30, 30, 220}; };
    const QString path = dir.filePath(QStringLiteral("flat.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {large});

    ViewFixture fixture;
    fixture.view->setSource(path);
    ASSERT_TRUE(waitUntil([&] { return fixture.view->status() != ImageView::Status::Loading; }, 20000));
    ASSERT_EQ(fixture.view->status(), ImageView::Status::Ready);
    QImage image = fixture.grab();
    expectColor(image, 100, 200, QColor(220, 30, 30));
    expectColor(image, 300, 200, QColor(30, 30, 220));

    // At 100% the view is centred on the colour boundary, using real tiles.
    fixture.view->actualSize();
    image = fixture.grab();
    expectColor(image, 100, 200, QColor(220, 30, 30));
    expectColor(image, 300, 200, QColor(30, 30, 220));
}

namespace {

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

}  // namespace

TEST(Selection, StartsWithTheOpenedImage) {
    SelectionFixture f;
    EXPECT_EQ(f.names(), QStringLiteral("a"));
    EXPECT_EQ(f.context.currentIndex(), 0);

    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("x.jpg")));
    writeJpeg(dir.filePath(QStringLiteral("y.jpg")));
    BrowseContext context;
    context.openPath(dir.filePath(QStringLiteral("y.jpg")));
    ASSERT_TRUE(waitUntil([&] { return context.selectedCount() == 1; }));
    EXPECT_EQ(QFileInfo(context.selectedPaths().front()).fileName(), QStringLiteral("y.jpg"));
}

TEST(Selection, PlainClickSelectsOnlyThatFile) {
    SelectionFixture f;
    f.context.selectAll();
    EXPECT_EQ(f.context.selectedCount(), 6);
    f.context.clickFile(2, false, false);
    EXPECT_EQ(f.names(), QStringLiteral("c"));
    EXPECT_EQ(f.context.currentIndex(), 2);
    // Also when it is the current one already, where only the selection changes.
    f.context.selectAll();
    f.context.clickFile(2, false, false);
    EXPECT_EQ(f.names(), QStringLiteral("c"));
}

TEST(Selection, CtrlTogglesAndMovesTheCurrentFile) {
    SelectionFixture f;
    f.context.clickFile(2, true, false);
    EXPECT_EQ(f.names(), QStringLiteral("ac"));
    EXPECT_EQ(f.context.currentIndex(), 2);
    f.context.clickFile(4, true, false);
    EXPECT_EQ(f.names(), QStringLiteral("ace"));
    f.context.clickFile(2, true, false);
    EXPECT_EQ(f.names(), QStringLiteral("ae"));
    EXPECT_EQ(f.context.currentIndex(), 2);  // current without being selected
}

TEST(Selection, ShiftSelectsARangeFromTheAnchorInBothDirections) {
    SelectionFixture f;
    f.context.clickFile(2, false, false);
    f.context.clickFile(4, false, true);
    EXPECT_EQ(f.names(), QStringLiteral("cde"));
    EXPECT_EQ(f.context.currentIndex(), 4);
    // The anchor stays, so the range turns around it instead of growing.
    f.context.clickFile(0, false, true);
    EXPECT_EQ(f.names(), QStringLiteral("abc"));
    f.context.clickFile(2, false, true);
    EXPECT_EQ(f.names(), QStringLiteral("c"));
}

TEST(Selection, ShiftRangeStartsAtTheLastCtrlClick) {
    SelectionFixture f;
    f.context.clickFile(3, true, false);
    EXPECT_EQ(f.names(), QStringLiteral("ad"));
    f.context.clickFile(5, false, true);  // replaces, as plain shift does
    EXPECT_EQ(f.names(), QStringLiteral("def"));
}

TEST(Selection, CtrlShiftAddsARange) {
    SelectionFixture f;
    f.context.clickFile(4, true, false);
    EXPECT_EQ(f.names(), QStringLiteral("ae"));
    f.context.clickFile(5, true, true);
    EXPECT_EQ(f.names(), QStringLiteral("aef"));
}

TEST(Selection, KeyboardExtendsFromTheAnchor) {
    SelectionFixture f;
    f.context.clickFile(2, false, false);
    f.context.extendSelectionTo(3);
    f.context.extendSelectionTo(4);
    EXPECT_EQ(f.names(), QStringLiteral("cde"));
    EXPECT_EQ(f.context.currentIndex(), 4);
    f.context.extendSelectionTo(3);
    EXPECT_EQ(f.names(), QStringLiteral("cd"));
    f.context.extendSelectionTo(1);
    EXPECT_EQ(f.names(), QStringLiteral("bc"));
    // Past either end nothing happens.
    f.context.extendSelectionTo(-1);
    f.context.extendSelectionTo(99);
    EXPECT_EQ(f.names(), QStringLiteral("bc"));
    EXPECT_EQ(f.context.currentIndex(), 1);
}

TEST(Selection, MovingToAnotherImageSelectsOnlyIt) {
    SelectionFixture f;
    f.context.selectAll();
    f.context.next();
    EXPECT_EQ(f.names(), QStringLiteral("b"));
    f.context.selectAll();
    f.context.setCurrentIndex(4);
    EXPECT_EQ(f.names(), QStringLiteral("e"));
    // And the anchor moves along, for a range started from the keyboard.
    f.context.extendSelectionTo(5);
    EXPECT_EQ(f.names(), QStringLiteral("ef"));
}

TEST(Selection, ClearingKeepsTheCurrentImage) {
    SelectionFixture f;
    f.context.clickFile(3, false, false);
    f.context.clearSelection();
    EXPECT_EQ(f.context.selectedCount(), 0);
    EXPECT_EQ(f.context.currentIndex(), 3);
    EXPECT_FALSE(f.context.currentPath().isEmpty());
}

TEST(Selection, ReachesTheModelAndNotifiesOnlyOnChange) {
    SelectionFixture f;
    QAbstractItemModel* model = f.context.files();
    const int role = model->roleNames().key("selected");
    int changes = 0;
    QObject::connect(&f.context, &BrowseContext::selectionChanged, [&] { ++changes; });
    QList<int> changedRows;
    QObject::connect(model, &QAbstractItemModel::dataChanged, [&](const QModelIndex& first, const QModelIndex& last, const QList<int>& roles) {
        EXPECT_EQ(roles, QList<int>{role});
        for (int row = first.row(); row <= last.row(); ++row) {
            changedRows << row;
        }
    });

    f.context.clickFile(1, true, false);
    EXPECT_EQ(changes, 1);
    EXPECT_EQ(changedRows, QList<int>{1});
    EXPECT_TRUE(model->data(model->index(1, 0), role).toBool());
    EXPECT_FALSE(model->data(model->index(2, 0), role).toBool());

    f.context.selectAll();
    EXPECT_EQ(changes, 2);
    f.context.selectAll();
    EXPECT_EQ(changes, 2);  // nothing changed
}

TEST(Selection, DoesNotSurviveAnotherFolder) {
    SelectionFixture f;
    f.context.selectAll();
    QTemporaryDir other;
    writeJpeg(other.filePath(QStringLiteral("only.jpg")));
    f.context.openPath(other.path());
    EXPECT_EQ(f.context.selectedCount(), 0);  // until the new folder is listed
    ASSERT_TRUE(waitUntil([&] { return f.context.selectedCount() == 1; }));
    EXPECT_EQ(QFileInfo(f.context.selectedPaths().front()).fileName(), QStringLiteral("only.jpg"));
}

TEST(Selection, IgnoresFilesThatDoNotExistAndEmptyFolders) {
    SelectionFixture f;
    f.context.clickFile(-1, false, false);
    f.context.clickFile(99, true, true);
    EXPECT_EQ(f.names(), QStringLiteral("a"));

    QTemporaryDir empty;
    BrowseContext context;
    context.openPath(empty.path());
    ASSERT_TRUE(waitUntil([&] { return context.directory() == QFileInfo(empty.path()).absoluteFilePath(); }));
    context.selectAll();
    context.clearSelection();
    context.extendSelectionTo(0);
    context.clickFile(0, false, false);
    EXPECT_EQ(context.selectedCount(), 0);
    EXPECT_TRUE(context.selectedPaths().isEmpty());
}

TEST(BrowseContext, ListsTiffFiles) {
    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("a.jpg")));
    iv::test::writeTiff(QFileInfo(dir.filePath(QStringLiteral("b.tif"))).filesystemFilePath(), {TiffImage{}});
    iv::test::writeTiff(QFileInfo(dir.filePath(QStringLiteral("c.TIFF"))).filesystemFilePath(), {TiffImage{}});
    BrowseContext context;
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return context.fileCount() == 3; }));
}

namespace {

// The value of a row of ImageView::details, or empty.
QString detail(const ImageView& view, const QString& label) {
    for (const QVariant& row : view.details()) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("label")).toString() == label) {
            return map.value(QStringLiteral("value")).toString();
        }
    }
    return {};
}

std::shared_ptr<const iv::Histogram> histogramOf(const ImageView& view) {
    return view.histogram().value<std::shared_ptr<const iv::Histogram>>();
}

}  // namespace

TEST(ImageView, DetailsFollowTheFileAndPage) {
    QTemporaryDir dir;
    TiffImage first;
    first.width = 100;
    TiffImage second;
    second.width = 50;
    second.bits = 16;
    const QString path = dir.filePath(QStringLiteral("two.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {first, second});

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    EXPECT_EQ(detail(*fixture.view, QStringLiteral("Name")), QStringLiteral("two.tif"));
    EXPECT_FALSE(detail(*fixture.view, QStringLiteral("Size")).isEmpty());
    EXPECT_FALSE(detail(*fixture.view, QStringLiteral("Modified")).isEmpty());
    EXPECT_EQ(detail(*fixture.view, QStringLiteral("Page")), QStringLiteral("1 of 2"));
    EXPECT_EQ(detail(*fixture.view, QStringLiteral("Bit depth")), QStringLiteral("8-bit"));

    fixture.view->setPage(1);
    ASSERT_TRUE(waitUntil([&] { return fixture.view->page() == 1; }));
    EXPECT_EQ(detail(*fixture.view, QStringLiteral("Page")), QStringLiteral("2 of 2"));
    EXPECT_EQ(detail(*fixture.view, QStringLiteral("Bit depth")), QStringLiteral("16-bit"));
    EXPECT_EQ(detail(*fixture.view, QStringLiteral("Name")), QStringLiteral("two.tif"));  // survives a page change

    fixture.view->setSource(dir.filePath(QStringLiteral("missing.tif")));
    ASSERT_TRUE(waitUntil([&] { return fixture.view->status() == ImageView::Status::Error; }));
    EXPECT_TRUE(fixture.view->details().isEmpty());
}

TEST(ImageView, TheLocationLinksToAMap) {
    QTemporaryDir dir;
    iv::test::JpegOptions options;
    options.exif = iv::test::exampleExifBlock(true);
    const QString path = dir.filePath(QStringLiteral("located.jpg"));
    iv::test::writeFile(QFileInfo(path).filesystemFilePath(), encodeJpeg(options));

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    QVariantMap coordinates;
    for (const QVariant& row : fixture.view->details()) {
        if (row.toMap().value(QStringLiteral("label")) == QStringLiteral("Coordinates")) {
            coordinates = row.toMap();
        }
    }
    EXPECT_EQ(coordinates.value(QStringLiteral("value")).toString(), QStringLiteral("49.19183° N, 16.60500° E"));
    EXPECT_EQ(coordinates.value(QStringLiteral("link")).toString(),
              QStringLiteral("https://geohack.toolforge.org/geohack.php?params=49.191833_N_16.605000_E"));
    EXPECT_FALSE(detail(*fixture.view, QStringLiteral("Altitude")).isEmpty());
}

TEST(ImageView, ComputesHistogramOnlyWhenEnabledAndFollowsThePage) {
    QTemporaryDir dir;
    TiffImage bright;
    bright.pixel = [](int, int) { return Rgb{200, 200, 200}; };
    TiffImage dark = bright;
    dark.pixel = [](int, int) { return Rgb{20, 20, 20}; };
    const QString path = dir.filePath(QStringLiteral("two.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {bright, dark});

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    QCoreApplication::processEvents();
    EXPECT_FALSE(histogramOf(*fixture.view));

    fixture.view->setHistogramEnabled(true);
    ASSERT_TRUE(waitUntil([&] { return bool(histogramOf(*fixture.view)); }));
    EXPECT_TRUE(histogramOf(*fixture.view)->gray);
    EXPECT_GT(histogramOf(*fixture.view)->channels[0][200], 0u);

    fixture.view->setPage(1);
    ASSERT_TRUE(waitUntil([&] {
        const auto histogram = histogramOf(*fixture.view);
        return histogram && histogram->channels[0][20] > 0;
    }));

    // A failed open leaves nothing behind.
    fixture.view->setSource(dir.filePath(QStringLiteral("missing.tif")));
    ASSERT_TRUE(waitUntil([&] { return fixture.view->status() == ImageView::Status::Error; }));
    EXPECT_FALSE(histogramOf(*fixture.view));
}

TEST(ImageView, ChangingContrastDoesNotLoseAPendingHistogram) {
    QTemporaryDir dir;
    TiffImage gray;
    gray.bits = 16;
    gray.samples = 1;
    const QString path = dir.filePath(QStringLiteral("gray.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {gray});

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    fixture.view->setHistogramEnabled(true);
    fixture.view->setAutoContrast(false);  // cancels the work queue group
    ASSERT_TRUE(waitUntil([&] { return bool(histogramOf(*fixture.view)); }));
}

namespace {

// Puts a HistogramView in a window and returns what it draws.
QImage drawHistogram(const std::shared_ptr<const iv::Histogram>& histogram, bool log = false, bool fit = false) {
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

int barHeight(const QImage& image, int x) {
    const QRgb background = qRgb(16, 16, 16);
    int bar = 0;
    for (int y = image.height() - 1; y >= 0 && image.pixel(x, y) != background; --y) {
        ++bar;
    }
    return bar;
}

std::shared_ptr<iv::Histogram> emptyHistogram(int bins, bool gray) {
    auto histogram = std::make_shared<iv::Histogram>();
    histogram->gray = gray;
    histogram->bitsPerBin = bins == 256 ? 8 : 16;
    histogram->high = bins - 1;
    for (int c = 0; c < (gray ? 1 : 3); ++c) {
        histogram->channels[static_cast<std::size_t>(c)].assign(static_cast<std::size_t>(bins), 0);
    }
    return histogram;
}

}  // namespace

TEST(HistogramView, DrawsGrayAndRgbDifferently) {
    auto gray = emptyHistogram(256, true);
    gray->channels[0][100] = 50;
    const QImage grayImage = drawHistogram(gray);
    const QColor grayBar = grayImage.pixelColor(100, 99);
    EXPECT_EQ(grayBar.red(), grayBar.green());
    EXPECT_EQ(grayBar.green(), grayBar.blue());
    EXPECT_EQ(barHeight(grayImage, 100), 100);
    EXPECT_EQ(barHeight(grayImage, 50), 0);

    auto rgb = emptyHistogram(256, false);
    rgb->channels[0][50] = 50;
    rgb->channels[1][100] = 50;
    rgb->channels[2][150] = 50;
    const QImage rgbImage = drawHistogram(rgb);
    EXPECT_GT(rgbImage.pixelColor(50, 99).red(), rgbImage.pixelColor(50, 99).green() + 100);
    EXPECT_GT(rgbImage.pixelColor(100, 99).green(), rgbImage.pixelColor(100, 99).red() + 100);
    EXPECT_GT(rgbImage.pixelColor(150, 99).blue(), rgbImage.pixelColor(150, 99).red() + 100);
}

TEST(HistogramView, UsesThePaletteForChannelsAndOverlaps) {
    const auto colorOf = [](std::initializer_list<int> channels) {
        auto histogram = emptyHistogram(256, false);
        for (const int c : channels) {
            histogram->channels[static_cast<std::size_t>(c)][100] = 50;
        }
        return drawHistogram(histogram).pixelColor(100, 99).name();
    };
    EXPECT_EQ(colorOf({0}), QStringLiteral("#e22023"));
    EXPECT_EQ(colorOf({1}), QStringLiteral("#009800"));
    EXPECT_EQ(colorOf({2}), QStringLiteral("#6868e7"));
    EXPECT_EQ(colorOf({0, 1}), QStringLiteral("#d3d16b"));
    EXPECT_EQ(colorOf({1, 2}), QStringLiteral("#00e7e7"));
    EXPECT_EQ(colorOf({0, 2}), QStringLiteral("#ffa5ef"));
    EXPECT_EQ(colorOf({0, 1, 2}), QStringLiteral("#cccccc"));

    auto gray = emptyHistogram(256, true);
    gray->channels[0][100] = 50;
    EXPECT_EQ(drawHistogram(gray).pixelColor(100, 99).name(), QStringLiteral("#cccccc"));
}

TEST(HistogramView, SixteenBitBinsAreCombinedIntoColumns) {
    auto histogram = emptyHistogram(65536, true);
    // 256 columns of 256 bins each: these all land in column 128.
    histogram->channels[0][32768] = 10;
    histogram->channels[0][32900] = 10;
    histogram->channels[0][1000] = 10;  // column 3, half as tall
    const QImage image = drawHistogram(histogram);
    EXPECT_EQ(barHeight(image, 128), 100);
    EXPECT_EQ(barHeight(image, 3), 50);
}

TEST(HistogramView, LogScaleLiftsSmallCounts) {
    auto histogram = emptyHistogram(256, true);
    histogram->channels[0][100] = 100000;
    histogram->channels[0][150] = 100;
    EXPECT_LT(barHeight(drawHistogram(histogram, false), 150), 3);
    EXPECT_GT(barHeight(drawHistogram(histogram, true), 150), 30);
}

TEST(HistogramView, AClippedEndDoesNotFlattenALinearScale) {
    auto histogram = emptyHistogram(256, true);
    histogram->channels[0][0] = 1000000;
    histogram->channels[0][100] = 100;
    const QImage image = drawHistogram(histogram);
    EXPECT_EQ(barHeight(image, 100), 100);
    EXPECT_EQ(barHeight(image, 0), 100);  // cut off at the top
}

TEST(HistogramView, FitRangeSpreadsTheValuesInUse) {
    auto histogram = emptyHistogram(65536, true);
    histogram->measuredSize = {10, 10};
    histogram->channels[0][1000] = 10;
    histogram->channels[0][4000] = 10;  // like 12-bit data in 16-bit samples

    iv::HistogramView view;
    view.setHistogram(QVariant::fromValue(std::shared_ptr<const iv::Histogram>(histogram)));
    EXPECT_TRUE(view.description().startsWith(QStringLiteral("0 to 65535")));
    view.setFitRange(true);
    EXPECT_TRUE(view.description().startsWith(QStringLiteral("1000 to 4000")));

    const QImage image = drawHistogram(histogram, false, true);
    EXPECT_GT(barHeight(image, 0), 0);
    EXPECT_GT(barHeight(image, 255), 0);
    EXPECT_EQ(barHeight(image, 128), 0);
}

TEST(HistogramView, EmptyIsSafe) {
    iv::HistogramView view;
    EXPECT_TRUE(view.description().isEmpty());
    view.setHistogram(QVariant());
    view.setFitRange(true);
    EXPECT_TRUE(view.description().isEmpty());
    const QImage image = drawHistogram(nullptr);
    EXPECT_EQ(barHeight(image, 100), 0);
}

TEST(ImageView, ReportsErrorsAndRecovers) {
    QTemporaryDir dir;
    const QString bad = dir.filePath(QStringLiteral("bad.jpg"));
    QFile file(bad);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("not an image");
    file.close();
    const QString good = dir.filePath(QStringLiteral("good.jpg"));
    writeJpeg(good);

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(bad));
    EXPECT_EQ(fixture.view->status(), ImageView::Status::Error);
    EXPECT_FALSE(fixture.view->errorString().isEmpty());

    ASSERT_TRUE(fixture.load(good));
    EXPECT_EQ(fixture.view->status(), ImageView::Status::Ready);
    EXPECT_TRUE(fixture.view->errorString().isEmpty());
}

TEST(ImageView, RapidSwitchingEndsOnTheLastImage) {
    QTemporaryDir dir;
    JpegOptions small;
    small.width = 32;
    small.height = 16;
    const QString last = dir.filePath(QStringLiteral("last.jpg"));
    writeJpeg(last, small);

    JpegOptions large;
    large.width = 2000;
    large.height = 2000;
    const QString first = dir.filePath(QStringLiteral("first.jpg"));
    writeJpeg(first, large);

    ViewFixture fixture;
    for (int i = 0; i < 20; ++i) {
        fixture.view->setSource(first);
        fixture.view->setSource(last);
    }
    ASSERT_TRUE(waitUntil([&] { return fixture.view->status() == ImageView::Status::Ready; }));
    EXPECT_EQ(fixture.view->imageWidth(), 32);
}

TEST(ImageView, DestroyingWhileLoadingIsSafe) {
    QTemporaryDir dir;
    JpegOptions large;
    large.width = 3000;
    large.height = 3000;
    const QString path = dir.filePath(QStringLiteral("large.jpg"));
    writeJpeg(path, large);

    for (int i = 0; i < 5; ++i) {
        ViewFixture fixture;
        fixture.view->setSource(path);
    }
    QCoreApplication::processEvents();
    SUCCEED();
}

namespace {

// Puts settings back for the next test.
struct ColorSettingsGuard {
    ~ColorSettingsGuard() {
        iv::ColorSettings::instance()->setEnabled(true);
        iv::ColorSettings::instance()->useSrgbDisplay();
    }
};

QString writeAdobeJpeg(const QTemporaryDir& dir, const QString& name) {
    const QString path = dir.filePath(name);
    JpegOptions options;
    options.iccProfile = iv::adobeRgbProfile();
    options.pixel = [](int, int) { return Rgb{60, 200, 90}; };
    writeJpeg(path, options);
    return path;
}

}  // namespace

TEST(ColorView, ConvertsAndFollowsTheSetting) {
    const ColorSettingsGuard guard;
    QTemporaryDir dir;
    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(writeAdobeJpeg(dir, QStringLiteral("a.jpg"))));
    fixture.view->fitToView();
    const QColor managed = fixture.grab().pixelColor(200, 200);
    EXPECT_LT(managed.red(), 30);  // sRGB has to go well below the file's 60 to match Adobe's green
    EXPECT_NEAR(managed.green(), 200, 4);

    iv::ColorSettings::instance()->setEnabled(false);
    const QColor unmanaged = fixture.grab().pixelColor(200, 200);
    EXPECT_NEAR(unmanaged.red(), 60, 4);
    EXPECT_NEAR(unmanaged.green(), 200, 4);
    EXPECT_NEAR(unmanaged.blue(), 90, 4);

    iv::ColorSettings::instance()->setEnabled(true);
    EXPECT_NEAR(fixture.grab().pixelColor(200, 200).red(), managed.red(), 1);
}

TEST(ColorView, FollowsADisplayProfile) {
    const ColorSettingsGuard guard;
    QTemporaryDir dir;
    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(writeAdobeJpeg(dir, QStringLiteral("a.jpg"))));
    fixture.view->fitToView();
    EXPECT_LT(fixture.grab().pixelColor(200, 200).red(), 30);

    // Showing Adobe RGB on an Adobe RGB display changes nothing.
    const QString profile = dir.filePath(QStringLiteral("display.icc"));
    iv::test::writeFile(QFileInfo(profile).filesystemFilePath(), iv::adobeRgbProfile());
    EXPECT_EQ(iv::ColorSettings::instance()->useDisplayProfile(QUrl::fromLocalFile(profile)), QString());
    EXPECT_EQ(iv::ColorSettings::instance()->displayProfileName(), QStringLiteral("Adobe RGB (1998)"));
    EXPECT_NEAR(fixture.grab().pixelColor(200, 200).red(), 60, 4);

    iv::ColorSettings::instance()->useSrgbDisplay();
    EXPECT_EQ(iv::ColorSettings::instance()->displayProfileName(), QStringLiteral("sRGB"));
    EXPECT_LT(fixture.grab().pixelColor(200, 200).red(), 30);
}

TEST(ColorView, RefusesFilesThatAreNoDisplayProfile) {
    const ColorSettingsGuard guard;
    QTemporaryDir dir;
    const QString junk = dir.filePath(QStringLiteral("junk.icc"));
    iv::test::writeFile(QFileInfo(junk).filesystemFilePath(), {1, 2, 3, 4});
    EXPECT_FALSE(iv::ColorSettings::instance()->useDisplayProfile(QUrl::fromLocalFile(junk)).isEmpty());
    EXPECT_FALSE(iv::ColorSettings::instance()->useDisplayProfile(QUrl::fromLocalFile(dir.filePath(QStringLiteral("none.icc")))).isEmpty());
    EXPECT_FALSE(iv::ColorSettings::instance()->hasDisplayProfile());
}

TEST(ColorView, AChangeWhileLoadingIsNotLost) {
    const ColorSettingsGuard guard;
    QTemporaryDir dir;
    ViewFixture fixture;
    fixture.view->setSource(writeAdobeJpeg(dir, QStringLiteral("a.jpg")));
    iv::ColorSettings::instance()->setEnabled(false);
    ASSERT_TRUE(waitUntil([&] { return fixture.view->status() == ImageView::Status::Ready; }));
    fixture.view->fitToView();
    EXPECT_NEAR(fixture.grab().pixelColor(200, 200).red(), 60, 4);
}

TEST(ColorView, ThumbnailsGetNewUrlsWhenTheSettingChanges) {
    const ColorSettingsGuard guard;
    QTemporaryDir dir;
    writeAdobeJpeg(dir, QStringLiteral("a.jpg"));
    BrowseContext context;
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return context.fileCount() == 1; }));
    QAbstractItemModel* model = context.files();
    const int role = model->roleNames().key("thumbnail");
    const QString before = model->data(model->index(0, 0), role).toString();
    int changes = 0;
    QObject::connect(model, &QAbstractItemModel::dataChanged, [&](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
        changes += roles.contains(role) ? 1 : 0;
    });
    iv::ColorSettings::instance()->setEnabled(false);
    EXPECT_EQ(changes, 1);
    EXPECT_NE(model->data(model->index(0, 0), role).toString(), before);
}

namespace {

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

}  // namespace

TEST(ViewerPage, HandlesShowThePartInViewWhileItMovesAndLeaveTheMouseToTheImage) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("big.jpg"));
    JpegOptions options;
    options.width = 4000;
    options.height = 2000;
    writeJpeg(path, options);
    BrowseContext context;
    QQuickView view;
    QObject::connect(view.engine(), &QQmlEngine::warnings, [](const QList<QQmlError>& warnings) {
        for (const QQmlError& warning : warnings) {
            ADD_FAILURE() << warning.toString().toStdString();
        }
    });
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)},
                               {QStringLiteral("active"), true},
                               {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(1000, 500);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    ASSERT_NE(view.rootObject(), nullptr);
    view.rootObject()->setProperty("panelAnimated", false);
    context.openPath(path);
    auto* image = view.rootObject()->property("view").value<ImageView*>();
    ASSERT_NE(image, nullptr);
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->width() == 1000; }));
    auto* horizontal = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("horizontalHandle"));
    auto* vertical = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("verticalHandle"));
    ASSERT_NE(horizontal, nullptr);
    ASSERT_NE(vertical, nullptr);

    auto* bars = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("imageScrollBars"));
    ASSERT_NE(bars, nullptr);
    const auto send = [&](QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, at, at, view.mapToGlobal(at), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &event);
        QCoreApplication::processEvents();
    };
    const auto drag = [&](QPointF from, QPointF by) {
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        for (int step = 1; step <= 4; ++step) {
            send(QEvent::MouseMove, from + by * step / 4, Qt::NoButton, Qt::LeftButton);
        }
        send(QEvent::MouseButtonRelease, from + by, Qt::LeftButton, Qt::NoButton);
    };

    EXPECT_FALSE(horizontal->isVisible()) << "all of it fits";
    EXPECT_FALSE(vertical->isVisible());

    image->actualSize();
    ASSERT_TRUE(horizontal->isVisible());
    ASSERT_TRUE(vertical->isVisible());
    // The tracks leave a margin at each end and the corner where the handles would meet.
    constexpr double kAcross = 1000 - 2 * 2 - 10;
    constexpr double kDown = 500 - 2 * 2 - 10;
    EXPECT_DOUBLE_EQ(horizontal->width(), 0.25 * kAcross);
    EXPECT_DOUBLE_EQ(vertical->height(), 0.25 * kDown);

    // A press on a handle's strip drags the image like anywhere else.
    const QRectF before = image->visibleArea();
    const QPointF onHandle = horizontal->mapToScene(QPointF(horizontal->width() / 2, horizontal->height() / 2));
    drag(onHandle, QPointF(-200, -100));
    const QRectF after = image->visibleArea();
    EXPECT_DOUBLE_EQ(after.x(), before.x() + 200.0 / 4000);
    EXPECT_DOUBLE_EQ(after.y(), before.y() + 100.0 / 2000);
    EXPECT_DOUBLE_EQ(horizontal->x(), 2 + after.x() * kAcross);
    EXPECT_DOUBLE_EQ(vertical->y(), 2 + after.y() * kDown);

    // Seen as it moved, and gone about half a second after it stopped.
    EXPECT_EQ(bars->opacity(), 1);
    QElapsedTimer sinceStop;
    sinceStop.start();
    EXPECT_TRUE(waitUntil([&] { return bars->opacity() == 0; }, 3000));
    EXPECT_GE(sinceStop.elapsed(), 300);
    EXPECT_LE(sinceStop.elapsed(), 1000);
    const double zoomBefore = image->zoom();
    const QPointF middle(500, 250);
    QWheelEvent wheel(middle, view.mapToGlobal(middle), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
                      Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&view, &wheel);
    EXPECT_GT(image->zoom(), zoomBefore);
    EXPECT_EQ(bars->opacity(), 1) << "zooming shows them again at once";

    // A tiny part of the image still gives a visible handle, which reaches the end of its track.
    while (image->visibleArea().width() * kAcross > 32 && image->zoom() < 32) {
        image->zoomIn();
    }
    ASSERT_LT(image->visibleArea().width() * kAcross, 32);
    EXPECT_DOUBLE_EQ(horizontal->width(), 32);
    drag(onHandle, QPointF(-100000, 0));
    EXPECT_DOUBLE_EQ(image->visibleArea().right(), 1);
    EXPECT_DOUBLE_EQ(horizontal->x() + horizontal->width(), 2 + kAcross);

    image->fitToView();
    EXPECT_FALSE(horizontal->isVisible()) << "fitted again";
    EXPECT_FALSE(vertical->isVisible());
}

TEST(ViewerPage, PanelTakesRoomFromTheImageExceptAtFullScreen) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.jpg"));
    writeJpeg(path);
    BrowseContext context;
    QQuickView view;
    QObject::connect(view.engine(), &QQmlEngine::warnings, [](const QList<QQmlError>& warnings) {
        for (const QQmlError& warning : warnings) {
            ADD_FAILURE() << warning.toString().toStdString();
        }
    });
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)},
                               {QStringLiteral("active"), true},
                               {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(1000, 600);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* const page = view.rootObject();
    ASSERT_NE(page, nullptr);
    page->setProperty("panelAnimated", false);
    const auto* image = page->property("view").value<ImageView*>();
    ASSERT_NE(image, nullptr);

    page->setProperty("panelPinned", false);
    EXPECT_TRUE(waitUntil([&] { return image->width() == 1000; }));

    page->setProperty("panelPinned", true);
    EXPECT_TRUE(waitUntil([&] { return image->width() < 1000; }));
    const qreal withPanel = image->width();
    EXPECT_GT(withPanel, 500);

    // At full screen the panel goes over the image, shown or not.
    page->setProperty("fullScreen", true);
    EXPECT_TRUE(waitUntil([&] { return image->width() == 1000; }));
    page->setProperty("panelRevealed", true);
    QCoreApplication::processEvents();
    EXPECT_EQ(image->width(), 1000);

    page->setProperty("fullScreen", false);
    EXPECT_TRUE(waitUntil([&] { return image->width() == withPanel; }));
    page->setProperty("panelPinned", false);  // the settings outlive the test
}

TEST(ImageView, ResamplingNeedsTheGpuAndLeavesTheSoftwareRendererAlone) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.jpg"));
    JpegOptions options;
    options.width = 800;
    options.height = 800;
    options.pixel = [](int, int) { return Rgb{200, 40, 40}; };
    writeJpeg(path, options);
    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    fixture.view->setResampling(true);
    fixture.view->zoomOut();
    if (fixture.window.rendererInterface()->graphicsApi() != QSGRendererInterface::Software) {
        GTEST_SKIP() << "for the software renderer";
    }
    EXPECT_FALSE(fixture.view->resampleActive());
    expectColor(fixture.grab(), 200, 200, QColor(200, 40, 40));
}

namespace {

double srgbToLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

double linearToSrgb(double c) {
    c = std::clamp(c, 0.0, 1.0);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055;
}

double mks2013(double x) {
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
QImage grabIfShown(QQuickWindow& window) {
    if (!window.isVisible() || !window.isExposed()) {
        return {};
    }
    return window.grabWindow().convertToFormat(QImage::Format_RGB32);
}

}  // namespace

// Needs a GPU or at least a GL driver, so run this one by hand on a real display or Xvfb:
//   QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=opengl iv_ui_tests --gtest_filter='ResampledView.*'
TEST(ResampledView, MatchesTheKernelComputedOnTheCpu) {
    QTemporaryDir dir;
    const int width = 600;
    const int height = 500;
    const auto pattern = [](int x, int y) {
        const auto v = static_cast<std::uint8_t>((x / 7 + y / 5) % 2 != 0 ? 230 : 25);
        return Rgb{v, static_cast<std::uint8_t>((x * 255) / 600), static_cast<std::uint8_t>(255 - v)};
    };
    TiffImage image;
    image.width = width;
    image.height = height;
    image.pixel = pattern;
    const QString path = dir.filePath(QStringLiteral("a.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {image});

    BrowseContext context;
    QQuickView window;
    window.engine()->addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
    window.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)},
                                 {QStringLiteral("active"), true},
                                 {QStringLiteral("fullScreen"), true}});
    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.resize(400, 400);
    window.loadFromModule("ImageViewer", "ViewerPage");
    window.show();
    ASSERT_TRUE(waitUntil([&] { return window.isExposed(); }));
    QCoreApplication::processEvents();
    if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
        GTEST_SKIP() << "needs a GPU";
    }
    auto* view = window.rootObject()->property("view").value<ImageView*>();
    ASSERT_NE(view, nullptr);
    window.rootObject()->setProperty("sharpResampling", true);
    context.openPath(path);
    ASSERT_TRUE(waitUntil([&] { return view->status() == ImageView::Status::Ready; }));
    ASSERT_TRUE(waitUntil([&] { return view->resampleActive(); }));

    QImage shown;
    const auto settle = [&] {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 500) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        shown = grabIfShown(window);
    };
    settle();

    const auto expected = [&](int px, int py) {
        const double ratio = view->resampleRatio();
        const double spacing = view->resampleSpacing();
        const QRectF bounds = view->resampleBounds();
        const double cx = view->resamplePhase().x() + (px + 0.5) * ratio - bounds.x();
        const double cy = view->resamplePhase().y() + (py + 0.5) * ratio - bounds.y();
        std::array<double, 3> sum{};
        double weights = 0;
        const int radiusX = static_cast<int>(std::ceil(2.5 * spacing));
        for (int j = static_cast<int>(std::floor(cy)) - radiusX; j <= static_cast<int>(std::floor(cy)) + radiusX; ++j) {
            const double wy = mks2013((j + 0.5 - cy) / spacing);
            for (int i = static_cast<int>(std::floor(cx)) - radiusX; i <= static_cast<int>(std::floor(cx)) + radiusX; ++i) {
                const double w = wy * mks2013((i + 0.5 - cx) / spacing);
                const Rgb c = pattern(std::clamp(i, 0, width - 1), std::clamp(j, 0, height - 1));
                sum[0] += w * srgbToLinear(c.r / 255.0);
                sum[1] += w * srgbToLinear(c.g / 255.0);
                sum[2] += w * srgbToLinear(c.b / 255.0);
                weights += w;
            }
        }
        return std::array<int, 3>{static_cast<int>(std::lround(255 * linearToSrgb(sum[0] / weights))),
                                  static_cast<int>(std::lround(255 * linearToSrgb(sum[1] / weights))),
                                  static_cast<int>(std::lround(255 * linearToSrgb(sum[2] / weights)))};
    };

    const auto compareAll = [&](int tolerance) {
        if (shown.isNull()) {
            ADD_FAILURE() << "the window was closed";
            return;
        }
        const QRectF rect = view->resampleImageRect();
        int worst = 0;
        int checked = 0;
        for (int y = 0; y < shown.height(); ++y) {
            for (int x = 0; x < shown.width(); ++x) {
                const double u = (x + 0.5) / shown.width();
                const double v = (y + 0.5) / shown.height();
                if (u < rect.left() || v < rect.top() || u >= rect.right() || v >= rect.bottom()) {
                    continue;
                }
                const auto e = expected(x, y);
                const QColor c = shown.pixelColor(x, y);
                worst = std::max({worst, std::abs(c.red() - e[0]), std::abs(c.green() - e[1]), std::abs(c.blue() - e[2])});
                ++checked;
            }
        }
        EXPECT_GT(checked, 1000);
        EXPECT_LE(worst, tolerance);
    };
    compareAll(2);  // minifying
    view->zoomIn();
    view->zoomIn();
    view->zoomIn();
    ASSERT_TRUE(view->resampleActive());
    settle();
    compareAll(2);  // stretched less or not at all, panned to wherever the zoom left it

    // Panning, even onto tiles that are not there yet, must not take the kernel away, as dropping
    // to another filter and back shows as flicker.
    ASSERT_TRUE(view->resampleActive());
    int dropped = 0;
    const QMetaObject::Connection watching = QObject::connect(view, &ImageView::resampleChanged, view, [&] {
        dropped += view->resampleActive() ? 0 : 1;
    });
    const auto sendMouse = [&](QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, at, at, window.mapToGlobal(at), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &event);
        QCoreApplication::processEvents();
    };
    const QPointF start(window.width() / 2.0, window.height() / 2.0);
    sendMouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    for (int step = 1; step <= 40; ++step) {
        sendMouse(QEvent::MouseMove, start + QPointF(step * 2 % 60, step % 30), Qt::NoButton, Qt::LeftButton);
        QTest::qWait(10);
    }
    sendMouse(QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
    QTest::qWait(600);
    QObject::disconnect(watching);
    EXPECT_EQ(dropped, 0);

    // Turned off, the view shows again at once.
    window.rootObject()->setProperty("sharpResampling", false);
    ASSERT_FALSE(view->resampleActive());
    settle();
    ASSERT_FALSE(shown.isNull()) << "the window was closed";
    const QPointF middle = view->imageToItem({width / 2.0, height / 2.0}) * window.devicePixelRatio();
    const Rgb inside = pattern(width / 2, height / 2);
    const QColor c = shown.pixelColor(middle.toPoint());
    EXPECT_NEAR(c.green(), inside.g, 40) << "the picture, not the background";
}

// Needs a GPU, like the test above:
//   QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=opengl iv_ui_tests --gtest_filter='ColorPreview.*'
TEST(ColorPreview, TheShaderShowsWhatApplyingGives) {
    QTemporaryDir dir;
    TiffImage image;
    image.width = 300;
    image.height = 200;
    image.pixel = [](int x, int y) {
        return Rgb{static_cast<std::uint8_t>(x * 255 / 299), static_cast<std::uint8_t>(y * 255 / 199),
                   static_cast<std::uint8_t>((x + y) % 256)};
    };
    const QString path = dir.filePath(QStringLiteral("a.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {image});
    // A 9-point cube that warms the picture and swaps green and blue a little.
    const QString cubePath = dir.filePath(QStringLiteral("look.cube"));
    {
        QFile cube(cubePath);
        ASSERT_TRUE(cube.open(QIODevice::WriteOnly));
        QTextStream out(&cube);
        out << "LUT_3D_SIZE 9\n";
        for (int b = 0; b < 9; ++b) {
            for (int g = 0; g < 9; ++g) {
                for (int r = 0; r < 9; ++r) {
                    const double rr = r / 8.0;
                    const double gg = g / 8.0;
                    const double bb = b / 8.0;
                    out << std::min(1.0, rr * 1.1 + 0.02) << ' ' << 0.7 * gg + 0.3 * bb << ' ' << 0.3 * gg + 0.6 * bb << '\n';
                }
            }
        }
    }

    BrowseContext context;
    QQuickView window;
    window.engine()->addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
    window.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)},
                                 {QStringLiteral("active"), true},
                                 {QStringLiteral("fullScreen"), true}});
    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.resize(400, 400);
    window.loadFromModule("ImageViewer", "ViewerPage");
    window.show();
    ASSERT_TRUE(waitUntil([&] { return window.isExposed(); }));
    QCoreApplication::processEvents();
    if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
        GTEST_SKIP() << "needs a GPU";
    }
    auto* view = window.rootObject()->property("view").value<ImageView*>();
    ASSERT_NE(view, nullptr);
    context.openPath(path);
    ASSERT_TRUE(waitUntil([&] { return view->status() == ImageView::Status::Ready; }));
    view->actualSize();
    const auto grab = [&] {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 500) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return grabIfShown(window);
    };
    ASSERT_EQ(view->loadColorCube(QUrl::fromLocalFile(cubePath)), QString());

    const QPointF topLeft = view->imageToItem({0, 0}) * window.devicePixelRatio();
    const auto compare = [&](const QVariantMap& settings) {
        view->previewColors(settings);
        const QImage preview = grab();
        view->adjustColors(settings);
        EXPECT_TRUE(view->colorPreviewActive()) << "until the result shows";
        EXPECT_TRUE(waitUntil([&] { return view->status() == ImageView::Status::Ready; }));
        EXPECT_FALSE(view->colorPreviewActive());
        const QImage applied = grab();
        view->undo();
        EXPECT_TRUE(waitUntil([&] { return view->status() == ImageView::Status::Ready; }));
        if (preview.isNull() || applied.isNull()) {
            ADD_FAILURE() << "the window was closed";
            return;
        }
        int worst = 0;
        int changed = 0;
        for (int y = 2; y < 198; y += 3) {
            for (int x = 2; x < 298; x += 3) {
                const int px = static_cast<int>(topLeft.x()) + x;
                const int py = static_cast<int>(topLeft.y()) + y;
                const QColor a = preview.pixelColor(px, py);
                const QColor b = applied.pixelColor(px, py);
                const Rgb original = image.pixel(x, y);
                const QColor c(original.r, original.g, original.b);
                worst = std::max({worst, std::abs(a.red() - b.red()), std::abs(a.green() - b.green()), std::abs(a.blue() - b.blue())});
                changed += (b != c) ? 1 : 0;
            }
        }
        EXPECT_GT(changed, 1000) << "the edit shows";
        EXPECT_LE(worst, 2) << QJsonDocument(QJsonObject::fromVariantMap(settings)).toJson(QJsonDocument::Compact).toStdString();
    };
    // Levels and the cube.
    compare({{QStringLiteral("black"), 0.1}, {QStringLiteral("white"), 0.9}, {QStringLiteral("gamma"), 1.4}});
    // Oklab lightness and chroma, then the cube.
    compare({{QStringLiteral("lightness"), 0.7}, {QStringLiteral("saturation"), 1.6}, {QStringLiteral("contrast"), 1.3}});
    // Colours pushed far beyond the range, fitted by chroma and clipped.
    compare({{QStringLiteral("saturation"), 2.0}, {QStringLiteral("brightness"), 0.05}});
    compare({{QStringLiteral("saturation"), 2.0}, {QStringLiteral("brightness"), 0.05}, {QStringLiteral("gamutMap"), false}});
    view->clearColorCube();
    // Everything but a cube, with blacks lifted twice over.
    compare({{QStringLiteral("black"), 0.05}, {QStringLiteral("gamma"), 0.8}, {QStringLiteral("outBlack"), 0.1},
             {QStringLiteral("outWhite"), 0.9}, {QStringLiteral("brightness"), 0.1}, {QStringLiteral("lightness"), 1.2},
             {QStringLiteral("saturation"), 0.3}});
}

TEST(BrowserPage, ClicksWithModifiersReachTheSelection) {
    BrowserPageFixture f;
    ASSERT_EQ(f.selected(), QStringLiteral("a"));
    f.clickFile(2, Qt::ControlModifier);
    EXPECT_EQ(f.selected(), QStringLiteral("ac"));
    EXPECT_EQ(f.context.currentIndex(), 2);
    f.clickFile(1);
    EXPECT_EQ(f.selected(), QStringLiteral("b"));
    f.clickFile(3, Qt::ShiftModifier);
    EXPECT_EQ(f.selected(), QStringLiteral("bcd"));
}

TEST(BrowserPage, DoubleClickWithCtrlTogglesOnce) {
    BrowserPageFixture f;
    f.clickFile(2, Qt::ControlModifier);
    f.clickFile(2, Qt::ControlModifier);
    EXPECT_EQ(f.selected(), QStringLiteral("ac"));
}

TEST(BrowserPage, ClickBetweenThumbnailsClearsTheSelection) {
    BrowserPageFixture f;
    f.clickFile(1);
    QQuickItem* grid = f.find("QQuickGridView");
    ASSERT_NE(grid, nullptr);
    f.clickAt(grid->mapToScene(QPointF(90, 500)), Qt::NoModifier);
    EXPECT_EQ(f.selected(), QString());
    EXPECT_EQ(f.context.currentIndex(), 1);
}

namespace {

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

}  // namespace

// What each row of the tree below the opened folder paints, as "label=folder it opens".
QStringList treeRows(TreeFixture& f) {
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

TEST(BrowserPage, TreeLabelsStayRightWhenASubfolderAppears) {
    TreeFixture f;
    const QDir dir(f.context.directory());
    ASSERT_TRUE(dir.mkpath(QStringLiteral("a")));
    ASSERT_TRUE(dir.mkpath(QStringLiteral("b")));
    const QStringList flat{QStringLiteral("a=a"), QStringLiteral("b=b"), QStringLiteral("inner=inner")};
    ASSERT_TRUE(waitUntil([&] { return treeRows(f) == flat; })) << treeRows(f).join(u' ').toStdString();

    // Created before a is expanded, so expanding it finds c while the rows below move down.
    ASSERT_TRUE(dir.mkpath(QStringLiteral("a/c")));
    f.settle();
    QMetaObject::invokeMethod(f.tree, "expand", Q_ARG(int, f.rowOf(f.path(QStringLiteral("a")))));
    const QStringList withC{QStringLiteral("a=a"), QStringLiteral("c=a/c"), QStringLiteral("b=b"), QStringLiteral("inner=inner")};
    EXPECT_TRUE(waitUntil([&] { return treeRows(f) == withC; }, 2000)) << treeRows(f).join(u' ').toStdString();

    // And created while it is expanded.
    ASSERT_TRUE(dir.mkpath(QStringLiteral("a/d")));
    const QStringList withD{QStringLiteral("a=a"), QStringLiteral("c=a/c"), QStringLiteral("d=a/d"), QStringLiteral("b=b"), QStringLiteral("inner=inner")};
    EXPECT_TRUE(waitUntil([&] { return treeRows(f) == withD; }, 2000)) << treeRows(f).join(u' ').toStdString();

    // Collapsing and expanding again finds them without reading the disk.
    const int a = f.rowOf(f.path(QStringLiteral("a")));
    QMetaObject::invokeMethod(f.tree, "collapse", Q_ARG(int, a));
    const QStringList collapsed{QStringLiteral("a=a"), QStringLiteral("b=b"), QStringLiteral("inner=inner")};
    EXPECT_TRUE(waitUntil([&] { return treeRows(f) == collapsed; }, 2000)) << treeRows(f).join(u' ').toStdString();
    QMetaObject::invokeMethod(f.tree, "expand", Q_ARG(int, a));
    EXPECT_TRUE(waitUntil([&] { return treeRows(f) == withD; }, 2000)) << treeRows(f).join(u' ').toStdString();

    // Only the row of the opened folder is highlighted.
    const QString b = f.path(QStringLiteral("b"));
    const QString opened = f.context.directory();
    f.context.openPath(b);
    f.settle();
    QQuickItem* bRow = nullptr;
    QMetaObject::invokeMethod(f.tree, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, bRow), Q_ARG(QModelIndex, f.model->indexForPath(b)));
    ASSERT_NE(bRow, nullptr);
    EXPECT_TRUE(bRow->property("highlighted").toBool());
    QQuickItem* cRow = nullptr;
    QMetaObject::invokeMethod(f.tree, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, cRow), Q_ARG(QModelIndex, f.model->indexForPath(QDir(opened).filePath(QStringLiteral("a/c")))));
    ASSERT_NE(cRow, nullptr);
    EXPECT_FALSE(cRow->property("highlighted").toBool());
}

TEST(BrowserPage, TreeLabelFollowsARenamedFolder) {
    TreeFixture f;
    const QDir dir(f.context.directory());
    ASSERT_TRUE(dir.mkpath(QStringLiteral("a")));
    const QStringList before{QStringLiteral("a=a"), QStringLiteral("inner=inner")};
    ASSERT_TRUE(waitUntil([&] { return treeRows(f) == before; })) << treeRows(f).join(u' ').toStdString();
    ASSERT_TRUE(f.context.rename(dir.filePath(QStringLiteral("a")), QStringLiteral("z")).isEmpty());
    const QStringList after{QStringLiteral("inner=inner"), QStringLiteral("z=z")};
    EXPECT_TRUE(waitUntil([&] { return treeRows(f) == after; }, 3000)) << treeRows(f).join(u' ').toStdString();
}

TEST(BrowserPage, TreeExpandsTheOpenedFolder) {
    TreeFixture f;
    const QString inner = f.path(QStringLiteral("inner"));
    EXPECT_TRUE(waitUntil([&] { return f.rowOf(inner) > f.rowOf(f.context.directory()); }));
}

TEST(BrowserPage, TreeShowsTheOtherFoldersOnTheWayToAnOpenedOne) {
    TreeFixture f;
    const QDir dir(f.context.directory());
    ASSERT_TRUE(dir.mkpath(QStringLiteral("inner/other")));
    ASSERT_TRUE(dir.mkpath(QStringLiteral("inner/deeper/deepest")));
    ASSERT_TRUE(dir.mkpath(QStringLiteral("inner/deeper/beside")));
    const QString target = dir.filePath(QStringLiteral("inner/deeper/deepest"));

    // Nothing has listed these yet, as when a path comes from the command line.
    f.context.openPath(target);
    for (const char* name : {"inner/other", "inner/deeper/beside"}) {
        const QString folder = dir.filePath(QString::fromLatin1(name));
        EXPECT_TRUE(waitUntil([&] { return f.rowOf(folder) >= 0; })) << name;
    }
    f.settle();
    QQuickItem* row = nullptr;
    QMetaObject::invokeMethod(f.tree, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, row), Q_ARG(QModelIndex, f.model->indexForPath(target)));
    EXPECT_NE(row, nullptr) << "the opened folder stays in sight as the rest fills in above it";
}

TEST(BrowserPage, ClickingAFolderOpensAndExpandsIt) {
    TreeFixture f;
    const QString inner = f.path(QStringLiteral("inner"));
    ASSERT_TRUE(waitUntil([&] { return !f.nameOf(inner).isNull(); }));
    f.clickAt(f.nameOf(inner), Qt::NoModifier);
    EXPECT_EQ(f.context.directory(), inner);
    const QString deeper = QDir(inner).filePath(QStringLiteral("deeper"));
    EXPECT_TRUE(waitUntil([&] { return f.rowOf(deeper) > f.rowOf(inner); }));
}

TEST(BrowserPage, DoubleClickingAFolderLeavesItExpanded) {
    TreeFixture f;
    const QString inner = f.path(QStringLiteral("inner"));
    ASSERT_TRUE(waitUntil([&] { return !f.nameOf(inner).isNull(); }));
    const QPointF name = f.nameOf(inner);
    f.clickAt(name, Qt::NoModifier);
    f.clickAt(name, Qt::NoModifier);
    f.settle();
    EXPECT_EQ(f.context.directory(), inner);
    const QString deeper = QDir(inner).filePath(QStringLiteral("deeper"));
    EXPECT_TRUE(waitUntil([&] { return f.rowOf(deeper) > f.rowOf(inner); }));
}

namespace {

// The real system trash is used, and main() has moved it into a temporary home.
QString trashFolder() { return qEnvironmentVariable("XDG_DATA_HOME") + QStringLiteral("/Trash/files"); }

bool isInTrash(const QString& name) {
    return QDir(trashFolder()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).contains(name);
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

}  // namespace

TEST(FileOperations, TrashKeepsTheFileRecoverableAndMovesToTheNextImage) {
    FileOpsFixture f;
    f.context.clickFile(2, false, false);
    QSignalSpy resets(f.context.files(), &QAbstractItemModel::modelReset);

    const QString unique = QStringLiteral("trash-%1.jpg").arg(QCoreApplication::applicationPid());
    ASSERT_TRUE(QFile::rename(f.path(QStringLiteral("c.jpg")), f.path(unique)));
    f.context.refresh();
    ASSERT_TRUE(waitUntil([&] { return f.context.currentName() != QStringLiteral("c.jpg"); }));

    f.context.trash({f.path(unique)});
    f.settle([&] { return f.context.fileCount() == 5; });

    EXPECT_TRUE(f.problems.isEmpty()) << f.problems.join(u'|').toStdString();
    EXPECT_FALSE(QFileInfo::exists(f.path(unique)));
    EXPECT_TRUE(isInTrash(unique)) << "looked in " << trashFolder().toStdString();
    EXPECT_EQ(resets.count(), 0) << "a view would lose its scroll position";
}

TEST(FileOperations, RemovingTheCurrentImageShowsTheNextAndTheLastShowsThePrevious) {
    FileOpsFixture f(3);
    f.context.clickFile(1, false, false);
    f.context.trash({f.path(QStringLiteral("b.jpg"))});
    f.settle([&] { return f.context.fileCount() == 2; });
    EXPECT_EQ(f.context.currentName(), QStringLiteral("c.jpg"));
    EXPECT_EQ(f.names(), QStringLiteral("c"));

    f.context.trash({f.path(QStringLiteral("c.jpg"))});
    f.settle([&] { return f.context.fileCount() == 1; });
    EXPECT_EQ(f.context.currentName(), QStringLiteral("a.jpg"));

    QSignalSpy emptied(&f.context, &BrowseContext::emptied);
    f.context.trash({f.path(QStringLiteral("a.jpg"))});
    f.settle([&] { return f.context.fileCount() == 0; });
    EXPECT_EQ(f.context.currentPath(), QString());
    EXPECT_EQ(emptied.count(), 1);
}

TEST(FileOperations, PermanentDeleteBypassesTheTrash) {
    FileOpsFixture f(3);
    f.context.clickFile(1, false, false);
    const int trashedBefore = static_cast<int>(QDir(trashFolder()).entryList(QDir::Files).size());

    f.context.deletePermanently({f.path(QStringLiteral("b.jpg"))});
    f.settle([&] { return f.context.fileCount() == 2; });
    EXPECT_TRUE(f.problems.isEmpty());
    EXPECT_FALSE(QFileInfo::exists(f.path(QStringLiteral("b.jpg"))));
    EXPECT_EQ(QDir(trashFolder()).entryList(QDir::Files).size(), trashedBefore);
    EXPECT_EQ(f.context.currentName(), QStringLiteral("c.jpg"));
}

TEST(FileOperations, PermanentDeleteReportsProblemsAndLeavesFolders) {
    FileOpsFixture f(1);
    const QString folder = f.path(QStringLiteral("sub"));
    ASSERT_TRUE(QDir().mkpath(folder));
    writeJpeg(QDir(folder).filePath(QStringLiteral("inside.jpg")));

    f.context.deletePermanently({folder, f.path(QStringLiteral("missing.jpg")), f.path(QStringLiteral("a.jpg"))});
    f.settle([&] { return f.context.fileCount() == 0; });
    EXPECT_EQ(f.problems.size(), 2) << f.problems.join(u'|').toStdString();
    EXPECT_TRUE(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("inside.jpg"))));
}

TEST(FileOperations, RefreshFindsChangesAndKeepsCurrentAndSelectionByName) {
    FileOpsFixture f(3);
    f.context.clickFile(1, false, false);
    f.context.clickFile(2, true, false);

    writeJpeg(f.path(QStringLiteral("0.jpg")));  // sorts first, so the rows move
    f.context.refresh();
    ASSERT_TRUE(waitUntil([&] { return f.context.fileCount() == 4; }));
    EXPECT_EQ(f.context.currentName(), QStringLiteral("c.jpg"));
    EXPECT_EQ(f.context.currentIndex(), 3);
    EXPECT_EQ(f.names(), QStringLiteral("bc"));
}

TEST(FileOperations, NoticesChangesInTheFolderByItself) {
    FileOpsFixture f(2);
    writeJpeg(f.path(QStringLiteral("z.jpg")));
    EXPECT_TRUE(waitUntil([&] { return f.context.fileCount() == 3; }));
}

TEST(FileOperations, AChangedFileGetsANewThumbnailUrl) {
    FileOpsFixture f(1);
    const QModelIndex first = f.context.files()->index(0, 0);
    const auto url = [&] { return f.context.files()->data(first, f.context.files()->roleNames().key("thumbnail")).toString(); };
    const QString before = url();

    QFile file(f.path(QStringLiteral("a.jpg")));
    ASSERT_TRUE(file.open(QIODevice::ReadWrite));
    ASSERT_TRUE(file.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
    file.close();
    f.context.refresh();
    EXPECT_TRUE(waitUntil([&] { return url() != before; }));
}

TEST(FileOperations, CopiesKeepTheOriginalAndItsTime) {
    FileOpsFixture f(2);
    const QDateTime old = QDateTime::currentDateTime().addDays(-10);
    {
        QFile file(f.path(QStringLiteral("a.jpg")));
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.setFileTime(old, QFileDevice::FileModificationTime));
    }
    f.context.copyTo({f.path(QStringLiteral("a.jpg"))}, f.other.path(), BrowseContext::SkipExisting);
    f.settle([&] { return QFileInfo::exists(f.otherPath(QStringLiteral("a.jpg"))); });
    EXPECT_TRUE(QFileInfo::exists(f.path(QStringLiteral("a.jpg"))));
    EXPECT_LT(std::abs(QFileInfo(f.otherPath(QStringLiteral("a.jpg"))).lastModified().secsTo(old)), 2);
}

TEST(FileOperations, MovesLeaveTheFolder) {
    FileOpsFixture f(3);
    f.context.moveTo({f.path(QStringLiteral("a.jpg")), f.path(QStringLiteral("b.jpg"))}, f.other.path(), BrowseContext::SkipExisting);
    f.settle([&] { return f.context.fileCount() == 1; });
    EXPECT_TRUE(QFileInfo::exists(f.otherPath(QStringLiteral("a.jpg"))));
    EXPECT_TRUE(QFileInfo::exists(f.otherPath(QStringLiteral("b.jpg"))));
    EXPECT_EQ(f.context.currentName(), QStringLiteral("c.jpg"));
}

TEST(FileOperations, ConflictsFollowThePolicyAndNeverLoseAFile) {
    FileOpsFixture f(1);
    const QString source = f.path(QStringLiteral("a.jpg"));
    const QString existing = f.otherPath(QStringLiteral("a.jpg"));
    f.writeBytes(existing, 7);
    EXPECT_EQ(f.context.conflictCount({source}, f.other.path()), 1);
    const qint64 sourceSize = FileOpsFixture::size(source);

    f.context.copyTo({source}, f.other.path(), BrowseContext::SkipExisting);
    f.settle([&] { return true; });
    EXPECT_EQ(FileOpsFixture::size(existing), 7) << "skip";

    f.context.copyTo({source}, f.other.path(), BrowseContext::KeepBoth);
    f.settle([&] { return QFileInfo::exists(f.otherPath(QStringLiteral("a (1).jpg"))); });
    EXPECT_EQ(FileOpsFixture::size(existing), 7) << "keep both";
    EXPECT_EQ(FileOpsFixture::size(f.otherPath(QStringLiteral("a (1).jpg"))), sourceSize);

    const int trashedBefore = static_cast<int>(QDir(trashFolder()).entryList(QDir::Files).size());
    f.context.copyTo({source}, f.other.path(), BrowseContext::ReplaceExisting);
    f.settle([&] { return FileOpsFixture::size(existing) == sourceSize; });
    EXPECT_TRUE(f.problems.isEmpty());
    EXPECT_EQ(QDir(trashFolder()).entryList(QDir::Files).size(), trashedBefore + 1) << "the old file went to the trash";
}

TEST(FileOperations, AFileNeverReplacesItself) {
    FileOpsFixture f(1);
    const QString source = f.path(QStringLiteral("a.jpg"));
    const qint64 size = FileOpsFixture::size(source);
    EXPECT_EQ(f.context.conflictCount({source}, f.dir.path()), 0);

    f.context.moveTo({source}, f.dir.path(), BrowseContext::ReplaceExisting);
    f.settle([&] { return true; });
    EXPECT_EQ(FileOpsFixture::size(source), size);
    EXPECT_TRUE(f.problems.isEmpty());

    f.context.copyTo({source}, f.dir.path(), BrowseContext::ReplaceExisting);
    f.settle([&] { return f.context.fileCount() == 2; });
    EXPECT_EQ(FileOpsFixture::size(source), size);
    EXPECT_EQ(FileOpsFixture::size(f.path(QStringLiteral("a (1).jpg"))), size);
    EXPECT_EQ(f.context.currentName(), QStringLiteral("a (1).jpg")) << "the new file is selected";
    EXPECT_EQ(f.names(), QStringLiteral("a (1)"));
}

TEST(FileOperations, FailuresAreReportedPerFileAndTheRestContinue) {
    FileOpsFixture f(2);
    const QStringList paths{f.path(QStringLiteral("missing.jpg")), f.path(QStringLiteral("a.jpg"))};
    f.context.copyTo(paths, f.other.path(), BrowseContext::SkipExisting);
    f.settle([&] { return QFileInfo::exists(f.otherPath(QStringLiteral("a.jpg"))); });
    ASSERT_EQ(f.problems.size(), 1);
    EXPECT_TRUE(f.problems.front().startsWith(QStringLiteral("missing.jpg: ")));
}

TEST(FileOperations, CancellingStopsBeforeTheNextFile) {
    FileOpsFixture f(6);
    QSignalSpy finished(&f.context, &BrowseContext::operationFinished);
    f.context.copyTo(f.context.selectedPaths(), f.other.path(), BrowseContext::SkipExisting);
    f.context.cancelJob();
    f.settle([&] { return finished.count() == 1; });
    EXPECT_FALSE(f.context.busy());
}

TEST(FileOperations, RenameChecksTheNameAndSelectsTheResult) {
    FileOpsFixture f(3);
    const QString a = f.path(QStringLiteral("a.jpg"));
    EXPECT_FALSE(f.context.rename(a, QString()).isEmpty());
    EXPECT_FALSE(f.context.rename(a, QStringLiteral("x/y.jpg")).isEmpty());
    EXPECT_FALSE(f.context.rename(a, QStringLiteral("b.jpg")).isEmpty()) << "exists";
    EXPECT_TRUE(QFileInfo::exists(a));

    EXPECT_TRUE(f.context.rename(a, QStringLiteral("z.jpg")).isEmpty());
    f.settle([&] { return f.context.currentName() == QStringLiteral("z.jpg"); });
    EXPECT_FALSE(QFileInfo::exists(a));
    EXPECT_EQ(f.context.currentIndex(), 2);
    EXPECT_EQ(f.names(), QStringLiteral("z"));
}

TEST(FileOperations, FoldersCanBeCreatedRenamedAndTrashed) {
    FileOpsFixture f(1);
    EXPECT_TRUE(f.context.createFolder(f.dir.path(), QStringLiteral("sub")).isEmpty());
    f.settle([&] { return QFileInfo(f.path(QStringLiteral("sub"))).isDir(); });
    EXPECT_FALSE(f.context.createFolder(f.dir.path(), QStringLiteral("sub")).isEmpty());

    const QString sub = f.path(QStringLiteral("sub"));
    writeJpeg(QDir(sub).filePath(QStringLiteral("inside.jpg")));
    f.context.openPath(sub);
    ASSERT_TRUE(waitUntil([&] { return f.context.fileCount() == 1; }));

    // Renaming the folder being shown follows it, and the image stays current.
    EXPECT_TRUE(f.context.rename(sub, QStringLiteral("renamed")).isEmpty());
    const QString renamed = f.path(QStringLiteral("renamed"));
    f.settle([&] { return f.context.directory() == renamed && f.context.fileCount() == 1; });
    EXPECT_EQ(f.context.currentName(), QStringLiteral("inside.jpg"));

    // Trashing it goes to its parent.
    const QString unique = QStringLiteral("folder-%1").arg(QCoreApplication::applicationPid());
    EXPECT_TRUE(f.context.rename(renamed, unique).isEmpty());
    const QString named = f.path(unique);
    f.settle([&] { return f.context.directory() == named && f.context.fileCount() == 1; });
    f.context.trashFolder(named);
    f.settle([&] { return !QFileInfo::exists(named) && f.context.directory() == f.dir.path(); });
    EXPECT_TRUE(isInTrash(unique));
    EXPECT_TRUE(f.problems.isEmpty());
}

TEST(FileOperations, RefusesToTrashHomeOrItsParents) {
    FileOpsFixture f(1);
    for (const QString& folder : {QDir::homePath(), QFileInfo(QDir::homePath()).absolutePath(), QDir::rootPath()}) {
        f.problems.clear();
        f.context.trashFolder(folder);
        EXPECT_FALSE(f.problems.isEmpty()) << folder.toStdString();
        EXPECT_TRUE(QFileInfo::exists(folder));
    }
}

TEST(FileOperations, SortsByEachKeyAndKeepsTheCurrentImage) {
    FileOpsFixture f(0);
    const auto write = [&](const char* name, int bytes, int age) {
        const QString path = f.path(QString::fromLatin1(name));
        f.writeBytes(path, bytes);
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.setFileTime(QDateTime::currentDateTime().addDays(-age), QFileDevice::FileModificationTime));
    };
    write("a.png", 30, 1);
    write("b.jpg", 10, 2);
    write("c.tif", 20, 3);
    f.context.refresh();
    ASSERT_TRUE(waitUntil([&] { return f.context.fileCount() == 3; }));
    f.context.clickFile(1, false, false);

    const auto order = [&] {
        QString out;
        const auto* model = f.context.files();
        for (int row = 0; row < model->rowCount(); ++row) {
            out += model->data(model->index(row, 0)).toString().front();
        }
        return out;
    };
    const auto expectOrder = [&](BrowseContext::SortKey key, bool descending, const char* expected) {
        f.context.setSortKey(key);
        f.context.setSortDescending(descending);
        EXPECT_TRUE(waitUntil([&] { return order() == QString::fromLatin1(expected); })) << expected << " but " << order().toStdString();
        EXPECT_EQ(f.context.currentName(), QStringLiteral("b.jpg"));
        EXPECT_EQ(f.context.currentIndex(), order().indexOf(QLatin1Char('b')));
        EXPECT_EQ(f.names(), QStringLiteral("b"));
    };
    expectOrder(BrowseContext::SortBySize, false, "bca");
    expectOrder(BrowseContext::SortByDate, false, "cba");
    expectOrder(BrowseContext::SortByType, false, "bac");
    expectOrder(BrowseContext::SortByName, true, "cba");
    expectOrder(BrowseContext::SortByName, false, "abc");
}

TEST(Clipboard, FilesReachTheClipboardAndASingleOneAlsoAsAnImage) {
    FileOpsFixture f(2);
    const QStringList both{f.path(QStringLiteral("a.jpg")), f.path(QStringLiteral("b.jpg"))};
    f.context.copyFilesToClipboard(both);
    EXPECT_EQ(f.context.clipboardFiles(), both);
    EXPECT_FALSE(QGuiApplication::clipboard()->mimeData()->hasImage());

    f.context.copyFilesToClipboard({both[0]});
    EXPECT_EQ(f.context.clipboardFiles(), QStringList{both[0]});
    EXPECT_TRUE(waitUntil([] { return QGuiApplication::clipboard()->mimeData()->hasImage(); }));
    EXPECT_EQ(f.context.clipboardFiles(), QStringList{both[0]}) << "the image adds to the files";
    EXPECT_EQ(QGuiApplication::clipboard()->image().width(), 64);
    EXPECT_TRUE(f.context.canPaste());
}

TEST(Clipboard, ALateImageDoesNotOverwriteANewerCopy) {
    FileOpsFixture f(1);
    f.context.copyFilesToClipboard({f.path(QStringLiteral("a.jpg"))});
    QGuiApplication::clipboard()->setText(QStringLiteral("newer"));
    waitUntil([] { return false; }, 300);
    EXPECT_EQ(QGuiApplication::clipboard()->text(), QStringLiteral("newer"));
    EXPECT_FALSE(f.context.canPaste());
}

TEST(Clipboard, FullPathsAreText) {
    FileOpsFixture f(1);
    f.context.copyPathsToClipboard({f.path(QStringLiteral("a.jpg"))});
    EXPECT_EQ(QGuiApplication::clipboard()->text(), QDir::toNativeSeparators(f.path(QStringLiteral("a.jpg"))));
}

TEST(Clipboard, PastedImageBecomesATimeNamedPngAndIsSelected) {
    FileOpsFixture f(1);
    QImage image(20, 10, QImage::Format_ARGB32);
    image.fill(qRgba(10, 200, 30, 128));
    QGuiApplication::clipboard()->setImage(image);
    ASSERT_TRUE(f.context.canPaste());
    EXPECT_TRUE(f.context.clipboardFiles().isEmpty());

    const QRegularExpression pattern(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}_\\d{6}\\.png$"));
    f.context.pasteImage();
    f.settle([&] { return f.context.fileCount() == 2; });
    const QString name = f.context.currentName();
    EXPECT_TRUE(pattern.match(name).hasMatch()) << name.toStdString();
    EXPECT_EQ(f.context.selectedCount(), 1);
    const QImage saved(f.path(name));
    EXPECT_EQ(saved.size(), image.size());
    EXPECT_EQ(saved.pixelColor(3, 3).alpha(), 128) << "transparency survives";
}

TEST(Clipboard, TwoImagesInTheSameSecondBothGetFiles) {
    QTemporaryDir dir;
    QImage image(4, 4, QImage::Format_RGB32);
    image.fill(Qt::red);
    const QDateTime when(QDate(2026, 12, 24), QTime(12, 59, 59));
    const auto first = iv::saveNewImage(image, dir.path(), when);
    const auto second = iv::saveNewImage(image, dir.path(), when);
    ASSERT_EQ(first.created.size(), 1);
    ASSERT_EQ(second.created.size(), 1);
    EXPECT_EQ(QFileInfo(first.created[0]).fileName(), QStringLiteral("2026-12-24_125959.png"));
    EXPECT_EQ(QFileInfo(second.created[0]).fileName(), QStringLiteral("2026-12-24_125959_1.png"));
}

TEST(History, GoesBackAndForwardAndRemembersTheImage) {
    FileOpsFixture f(3);
    const QString first = f.dir.path();
    const QString second = f.other.path();
    writeJpeg(f.otherPath(QStringLiteral("x.jpg")));
    f.context.clickFile(2, false, false);
    EXPECT_FALSE(f.context.canGoBack());

    f.context.openPath(second);
    ASSERT_TRUE(waitUntil([&] { return f.context.fileCount() == 1; }));
    EXPECT_TRUE(f.context.canGoBack());
    EXPECT_FALSE(f.context.canGoForward());

    f.context.goBack();
    ASSERT_TRUE(waitUntil([&] { return f.context.fileCount() == 3 && f.context.currentIndex() == 2; }));
    EXPECT_EQ(f.context.directory(), first);
    EXPECT_EQ(f.context.currentName(), QStringLiteral("c.jpg"));
    EXPECT_FALSE(f.context.canGoBack());
    EXPECT_TRUE(f.context.canGoForward());

    f.context.goForward();
    ASSERT_TRUE(waitUntil([&] { return f.context.fileCount() == 1; }));
    EXPECT_EQ(f.context.directory(), second);
    EXPECT_FALSE(f.context.canGoForward());
}

TEST(History, ANewFolderForgetsForwardAndTheSameFolderIsNotRecorded) {
    FileOpsFixture f(2);
    f.context.openPath(f.path(QStringLiteral("b.jpg")));
    EXPECT_FALSE(f.context.canGoBack()) << "another image of the same folder";
    f.context.openPath(f.other.path());
    f.context.goBack();
    EXPECT_TRUE(f.context.canGoForward());
    QTemporaryDir third;
    f.context.openPath(third.path());
    EXPECT_FALSE(f.context.canGoForward());
}

TEST(History, PassesOverFoldersThatAreGoneAndFollowsARename) {
    FileOpsFixture f(1);
    const QDir base(f.other.path());
    ASSERT_TRUE(base.mkpath(QStringLiteral("gone")));
    ASSERT_TRUE(base.mkpath(QStringLiteral("old")));
    f.context.openPath(base.filePath(QStringLiteral("old")));
    f.context.openPath(base.filePath(QStringLiteral("gone")));
    f.context.openPath(f.dir.path());

    ASSERT_TRUE(QDir(base.filePath(QStringLiteral("gone"))).removeRecursively());
    EXPECT_TRUE(f.context.rename(base.filePath(QStringLiteral("old")), QStringLiteral("new")).isEmpty());
    f.settle([] { return true; });

    f.context.goBack();
    EXPECT_EQ(f.context.directory(), base.filePath(QStringLiteral("new")));
}

TEST(History, UpGoesToTheParentAndStopsAtTheRoot) {
    FileOpsFixture f(1);
    const QString parent = QFileInfo(f.dir.path()).absolutePath();
    f.context.goUp();
    EXPECT_EQ(f.context.directory(), parent);
    f.context.goBack();
    EXPECT_EQ(f.context.directory(), f.dir.path());
    f.context.openPath(QDir::rootPath());
    EXPECT_FALSE(f.context.canGoUp());
    const QString root = f.context.directory();
    f.context.goUp();
    EXPECT_EQ(f.context.directory(), root);
}

TEST(TypedPath, UnderstandsWhatPeoplePaste) {
    FileOpsFixture f(2);
    const QString sub = f.path(QStringLiteral("sub dir"));
    ASSERT_TRUE(QDir().mkpath(sub));
    const auto opens = [&](const QString& text, const QString& directory) {
        f.context.openPath(f.dir.path());
        EXPECT_EQ(f.context.openTyped(text), QString()) << text.toStdString();
        EXPECT_EQ(f.context.directory(), directory) << text.toStdString();
    };
    opens(sub, sub);
    opens(sub + QLatin1Char('/'), sub);
    opens(QStringLiteral("  ") + sub + QStringLiteral("\n"), sub);
    opens(QLatin1Char('"') + sub + QLatin1Char('"'), sub);
    opens(QLatin1Char('\'') + sub + QLatin1Char('\''), sub);
    opens(QUrl::fromLocalFile(sub).toString(), sub);
    opens(QUrl::fromLocalFile(sub).toString(QUrl::FullyEncoded), sub);
    opens(QStringLiteral("sub dir"), sub);
    opens(QStringLiteral("sub dir/.."), f.dir.path());
    opens(QStringLiteral("~"), QDir::homePath());
}

TEST(TypedPath, AFileOpensItsFolderWithThatImage) {
    FileOpsFixture f(3);
    f.context.openPath(f.other.path());
    EXPECT_EQ(f.context.openTyped(f.path(QStringLiteral("b.jpg"))), QString());
    EXPECT_EQ(f.context.directory(), f.dir.path());
    EXPECT_EQ(f.context.currentName(), QStringLiteral("b.jpg"));
}

TEST(TypedPath, ProblemsLeaveTheFolderAlone) {
    FileOpsFixture f(1);
    const QString before = f.context.directory();
    for (const QString& text : {QString(), QStringLiteral("   "), f.path(QStringLiteral("missing")), QStringLiteral("https://example.com/a"), QStringLiteral("file://host/share/a")}) {
        EXPECT_FALSE(f.context.openTyped(text).isEmpty()) << text.toStdString();
        EXPECT_EQ(f.context.directory(), before);
    }
    EXPECT_FALSE(f.context.canGoBack());
}

TEST(SubfolderList, ListsFoldersOnlyInNaturalOrderAndFollowsThePath) {
    QTemporaryDir dir;
    for (const char* name : {"b10", "b9", "A", ".hidden"}) {
        ASSERT_TRUE(QDir(dir.path()).mkpath(QString::fromLatin1(name)));
    }
    writeJpeg(dir.filePath(QStringLiteral("file.jpg")));

    iv::SubfolderList list;
    list.setPath(dir.path());
    EXPECT_TRUE(list.loading());
    ASSERT_TRUE(waitUntil([&] { return !list.loading(); }));
    EXPECT_EQ(list.folders(), (QStringList{QStringLiteral("A"), QStringLiteral("b9"), QStringLiteral("b10")}));

    list.setPath(QDir(dir.path()).filePath(QStringLiteral("A")));
    EXPECT_TRUE(list.folders().isEmpty()) << "nothing of the previous folder shows";
    ASSERT_TRUE(waitUntil([&] { return !list.loading(); }));
    EXPECT_TRUE(list.folders().isEmpty());

    list.setPath(dir.filePath(QStringLiteral("missing")));
    ASSERT_TRUE(waitUntil([&] { return !list.loading(); }));
    EXPECT_TRUE(list.folders().isEmpty());
}

TEST(SubfolderList, DestroyingWhileLoadingIsSafe) {
    QTemporaryDir dir;
    for (int i = 0; i < 20; ++i) {
        iv::SubfolderList list;
        list.setPath(dir.path());
    }
}

TEST(BrowserPage, PathBarShowsCrumbsBoldLastAndListsSubfolders) {
    TreeFixture f;
    auto* bar = f.view.rootObject()->findChild<QObject*>(QStringLiteral("pathBar"));
    ASSERT_NE(bar, nullptr);
    const QStringList names = bar->property("crumbNames").toStringList();
    ASSERT_GE(names.size(), 2);
    EXPECT_EQ(names.back(), QFileInfo(f.dir.path()).fileName());
    EXPECT_EQ(bar->property("boldCrumb").toString(), names.back());

    QMetaObject::invokeMethod(bar, "openSubfolders", Q_ARG(QVariant, f.dir.path()), Q_ARG(QVariant, QVariant()));
    EXPECT_TRUE(waitUntil([&] { return bar->property("menuFolders").toStringList() == QStringList{QStringLiteral("inner")}; }));
    const QString inner = f.path(QStringLiteral("inner"));
    QMetaObject::invokeMethod(bar, "chooseSubfolder", Q_ARG(QVariant, QStringLiteral("inner")));
    EXPECT_EQ(f.context.directory(), inner);
    EXPECT_EQ(bar->property("boldCrumb").toString(), QStringLiteral("inner"));
    EXPECT_FALSE(bar->property("menuOpen").toBool());
}

TEST(BrowserPage, PathBarEditsAndKeepsAProblemOnScreen) {
    TreeFixture f;
    auto* bar = f.view.rootObject()->findChild<QObject*>(QStringLiteral("pathBar"));
    ASSERT_NE(bar, nullptr);
    const QString start = f.context.directory();

    QMetaObject::invokeMethod(bar, "edit");
    EXPECT_TRUE(bar->property("editing").toBool());
    EXPECT_EQ(bar->property("editText").toString(), QDir::toNativeSeparators(start));

    QMetaObject::invokeMethod(bar, "submit", Q_ARG(QVariant, f.path(QStringLiteral("missing"))));
    EXPECT_TRUE(bar->property("editing").toBool());
    EXPECT_FALSE(bar->property("problem").toString().isEmpty());
    EXPECT_EQ(f.context.directory(), start);

    const QString inner = f.path(QStringLiteral("inner"));
    QMetaObject::invokeMethod(bar, "submit", Q_ARG(QVariant, inner));
    EXPECT_FALSE(bar->property("editing").toBool());
    EXPECT_EQ(f.context.directory(), inner);
    EXPECT_TRUE(bar->property("problem").toString().isEmpty());
}

TEST(BrowserPage, PreviewShowsHandlesOnlyWhenZoomedIntoPartOfTheImage) {
    BrowserPageFixture f;
    f.view.rootObject()->setProperty("active", true);
    f.context.setCurrentIndex(0);
    auto* preview = f.view.rootObject()->property("preview").value<ImageView*>();
    ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(waitUntil([&] { return preview->status() == ImageView::Status::Ready; })) << preview->source().toStdString();
    auto* horizontal = f.view.rootObject()->findChild<QQuickItem*>(QStringLiteral("horizontalHandle"));
    ASSERT_NE(horizontal, nullptr);
    EXPECT_FALSE(horizontal->isVisible()) << "all of it fits";

    for (int i = 0; i < 20 && preview->visibleArea().width() >= 0.5; ++i) {
        preview->zoomIn();
    }
    ASSERT_LT(preview->visibleArea().width(), 0.5);
    EXPECT_TRUE(horizontal->isVisible());

    preview->fitToView();
    EXPECT_FALSE(horizontal->isVisible()) << "fitted again";
}

TEST(BrowserPage, PreviewUsesTheSharpSetting) {
    BrowserPageFixture f;
    auto* preview = f.view.rootObject()->property("preview").value<ImageView*>();
    ASSERT_NE(preview, nullptr);
    EXPECT_FALSE(preview->resampling());
    f.view.rootObject()->setProperty("sharpResampling", true);
    EXPECT_TRUE(preview->resampling());
}

TEST(BrowserPage, ThumbnailsAreShownOnePixelForOne) {
    BrowserPageFixture f;
    QQuickItem* image = nullptr;
    // Delegates are children of the items they show in, but not of the page as objects.
    const std::function<QQuickItem*(QQuickItem*)> loadedThumbnail = [&](QQuickItem* item) -> QQuickItem* {
        if (item->objectName() == QStringLiteral("thumbnailImage") && item->property("status").toInt() == 1 && item->width() > 0) {
            return item;
        }
        for (QQuickItem* child : item->childItems()) {
            if (QQuickItem* found = loadedThumbnail(child)) {
                return found;
            }
        }
        return nullptr;
    };
    ASSERT_TRUE(waitUntil([&] { return (image = loadedThumbnail(f.view.contentItem())) != nullptr; }));
    EXPECT_EQ(image->property("sourceSize").toSize(), QSize(164, 164));
    EXPECT_LE(std::max(image->width(), image->height()), 164);
    const double scale = f.view.effectiveDevicePixelRatio();
    EXPECT_DOUBLE_EQ(image->width() * scale, std::round(image->width() * scale));
    const QPointF corner = image->mapToScene(QPointF(0, 0)) * scale;
    EXPECT_DOUBLE_EQ(corner.x(), std::round(corner.x()));
    EXPECT_DOUBLE_EQ(corner.y(), std::round(corner.y()));
}

TEST(BrowserPage, PageKeysMoveWholeRowsAndStopAtTheEnds) {
    BrowserPageFixture f;
    f.view.resize(700, 300);  // room for one row of three at a time, as the fixture has four
    QCoreApplication::processEvents();
    f.context.setCurrentIndex(0);
    QObject* page = f.view.rootObject();
    QMetaObject::invokeMethod(page, "movePage", Q_ARG(QVariant, 1));
    const int columns = f.find("QQuickGridView")->property("columns").toInt();
    ASSERT_GT(columns, 0);
    EXPECT_EQ(f.context.currentIndex(), std::min(columns, 3));
    QMetaObject::invokeMethod(page, "movePage", Q_ARG(QVariant, 100));
    EXPECT_EQ(f.context.currentIndex(), 3) << "stops at the last, rather than not moving";
    QMetaObject::invokeMethod(page, "extendSelectionByPage", Q_ARG(QVariant, -100));
    EXPECT_EQ(f.context.currentIndex(), 0);
    EXPECT_EQ(f.context.selectedCount(), 4);
    QMetaObject::invokeMethod(page, "movePage", Q_ARG(QVariant, -1));
    EXPECT_EQ(f.context.currentIndex(), 0);
}

TEST(BrowserPage, ScrollbarAppearsOverItsListInTheAccentColourAndFades) {
    BrowserPageFixture f;
    QQuickItem* grid = f.find("QQuickGridView");
    ASSERT_NE(grid, nullptr);
    QQuickItem* bar = nullptr;
    for (QQuickItem* item : f.view.rootObject()->findChildren<QQuickItem*>()) {
        if (item->inherits("QQuickScrollBar") && item->parentItem() == grid) {
            bar = item;
        }
    }
    ASSERT_NE(bar, nullptr);
    EXPECT_FALSE(bar->isVisible()) << "four files fit, so nothing to scroll";

    f.view.resize(700, 300);  // two rows, and room for one
    ASSERT_TRUE(waitUntil([&] { return bar->isVisible() && bar->property("size").toDouble() < 1; }));

    const auto moveTo = [&](QPointF at) {
        QMouseEvent move(QEvent::MouseMove, at, at, f.view.mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&f.view, &move);
        QCoreApplication::processEvents();
    };
    const QPointF overList = grid->mapToScene(QPointF(50, 50));
    const QPointF elsewhere = f.view.rootObject()->mapToScene(QPointF(5, f.view.height() - 5));

    // Out of sight while the mouse is elsewhere and nothing moves.
    moveTo(elsewhere);
    EXPECT_TRUE(waitUntil([&] { return bar->opacity() == 0; }, 2000));

    moveTo(overList);
    EXPECT_TRUE(waitUntil([&] { return bar->opacity() == 1; }, 2000)) << "over the list";

    // The handle is the accent colour at 20%, over whatever lies under it.
    const QPoint handle = bar->mapToScene(QPointF(bar->width() / 2, 10)).toPoint();
    const QImage with = f.view.grabWindow();
    bar->setVisible(false);
    const QImage without = f.view.grabWindow();
    bar->setVisible(true);
    const QColor under = without.pixelColor(handle);
    const QColor accent(QStringLiteral("#5a93aa"));
    const QColor seen = with.pixelColor(handle);
    EXPECT_NEAR(seen.red(), under.red() * 0.8 + accent.red() * 0.2, 2);
    EXPECT_NEAR(seen.green(), under.green() * 0.8 + accent.green() * 0.2, 2);
    EXPECT_NEAR(seen.blue(), under.blue() * 0.8 + accent.blue() * 0.2, 2);

    moveTo(elsewhere);
    EXPECT_TRUE(waitUntil([&] { return bar->opacity() == 0; }, 2000)) << "and it fades again";
}

TEST(BrowserPage, SubfolderListStaysInsideTheWindowUnderTheLastCaret) {
    TreeFixture f;
    auto* bar = f.view.rootObject()->findChild<QObject*>(QStringLiteral("pathBar"));
    ASSERT_NE(bar, nullptr);
    auto* anchor = qobject_cast<QQuickItem*>(bar);
    ASSERT_NE(anchor, nullptr);
    QQuickItem corner(anchor);  // as far right as a caret can be
    corner.setPosition(QPointF(anchor->width() - 1, 0));
    QMetaObject::invokeMethod(bar, "openSubfolders", Q_ARG(QVariant, f.dir.path()), Q_ARG(QVariant, QVariant::fromValue(&corner)));
    QObject* menu = bar->property("menu").value<QObject*>();
    ASSERT_NE(menu, nullptr);
    EXPECT_LE(menu->property("x").toDouble() + menu->property("width").toDouble(), f.view.width());
}

TEST(Main, LoadsWithItsFileActionsWithoutWarnings) {
    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("a.jpg")));
    QQmlApplicationEngine engine;
    engine.addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
    QObject::connect(&engine, &QQmlEngine::warnings, [](const QList<QQmlError>& warnings) {
        for (const QQmlError& warning : warnings) {
            ADD_FAILURE() << warning.toString().toStdString();
        }
    });
    engine.setInitialProperties({{QStringLiteral("initialPath"), dir.path()}});
    engine.loadFromModule("ImageViewer", "Main");
    ASSERT_EQ(engine.rootObjects().size(), 1);
    QCoreApplication::processEvents();
    waitUntil([] { return false; }, 200);
}

TEST(Main, EditingThePathTurnsOffShortcutsForKeysATextFieldLeaves) {
    QTemporaryDir dir;
    QQmlApplicationEngine engine;
    engine.addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
    engine.setInitialProperties({{QStringLiteral("initialPath"), dir.path()}});
    engine.loadFromModule("ImageViewer", "Main");
    ASSERT_EQ(engine.rootObjects().size(), 1);
    QObject* window = engine.rootObjects().front();
    auto* bar = window->findChild<QObject*>(QStringLiteral("pathBar"));
    ASSERT_NE(bar, nullptr);

    EXPECT_FALSE(window->property("typing").toBool());
    QMetaObject::invokeMethod(bar, "edit");
    EXPECT_TRUE(window->property("typing").toBool());
    bar->setProperty("editing", false);
    EXPECT_FALSE(window->property("typing").toBool());
}

TEST(BrowserPage, RightClickSelectsAnUnselectedFileAndKeepsASelection) {
    BrowserPageFixture f;
    QSignalSpy menus(f.view.rootObject(), SIGNAL(fileMenuRequested()));
    QQuickItem* grid = f.find("QQuickGridView");
    ASSERT_NE(grid, nullptr);
    f.clickFile(0);
    f.clickFile(1, Qt::ControlModifier);
    ASSERT_EQ(f.selected(), QStringLiteral("ab"));

    const auto rightClick = [&](int index) {
        const QPointF point = grid->mapToScene(QPointF(90 + 180 * index, 100));
        const QPointF global = f.view.mapToGlobal(point);
        QMouseEvent press(QEvent::MouseButtonPress, point, point, global, Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&f.view, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, point, point, global, Qt::RightButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&f.view, &release);
        QCoreApplication::processEvents();
    };
    rightClick(1);
    EXPECT_EQ(menus.count(), 1);
    EXPECT_EQ(f.selected(), QStringLiteral("ab"));
    rightClick(3);
    EXPECT_EQ(menus.count(), 2);
    EXPECT_EQ(f.selected(), QStringLiteral("d"));
}

namespace {

QStringList rowTexts(const QVariantList& rows) {
    QStringList texts;
    for (const QVariant& row : rows) {
        texts << row.toMap().value(QStringLiteral("text")).toString();
    }
    return texts;
}

QQuickItem* findNamed(QQuickItem* parent, const QString& name) {
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

}  // namespace

TEST(BrowseContext, SummarizesFoldersFilesAndSelection) {
    QTemporaryDir dir;
    iv::test::writeFile(QFileInfo(dir.filePath(QStringLiteral("big.jpg"))).filesystemFilePath(), std::vector<std::uint8_t>(5000, 0));
    iv::test::writeFile(QFileInfo(dir.filePath(QStringLiteral("small.jpg"))).filesystemFilePath(), std::vector<std::uint8_t>(1000, 0));
    ASSERT_TRUE(QDir(dir.path()).mkpath(QStringLiteral("one")));
    BrowseContext context;
    EXPECT_EQ(context.summary(), QString());
    QSignalSpy changes(&context, &BrowseContext::summaryChanged);
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return context.fileCount() == 2; }));
    EXPECT_GT(changes.count(), 0);
    EXPECT_EQ(context.summary(), QStringLiteral("1 directory | 2 files (6 kB) | 1 selected (5 kB)"));

    context.clearSelection();
    EXPECT_EQ(context.summary(), QStringLiteral("1 directory | 2 files (6 kB)"));
    context.selectAll();
    EXPECT_EQ(context.summary(), QStringLiteral("1 directory | 2 files (6 kB) | 2 selected (6 kB)"));

    // Folders change on their own, with no image coming or going.
    ASSERT_TRUE(QDir(dir.path()).mkpath(QStringLiteral("two")));
    context.refresh();
    EXPECT_TRUE(waitUntil([&] { return context.summary().startsWith(QStringLiteral("2 directories | ")); }));
}

TEST(BrowseContext, JumpsBackThroughTheHistoryAtOnce) {
    QTemporaryDir dir;
    const QDir root(dir.path());
    for (const char* name : {"a", "b", "c", "gone", "d"}) {
        ASSERT_TRUE(root.mkpath(QString::fromLatin1(name)));
    }
    BrowseContext context;
    const auto path = [&](const char* name) { return root.filePath(QString::fromLatin1(name)); };
    for (const char* name : {"a", "b", "c", "gone", "d"}) {
        context.openPath(path(name));
    }
    EXPECT_EQ(context.backFolders(), (QStringList{path("gone"), path("c"), path("b"), path("a")}));
    ASSERT_TRUE(QDir(path("gone")).removeRecursively());
    EXPECT_EQ(context.backFolders(), (QStringList{path("c"), path("b"), path("a")})) << "not one that went away";

    context.goBackBy(2);  // to b, past c and the one that is gone
    EXPECT_EQ(context.directory(), path("b"));
    EXPECT_EQ(context.backFolders(), QStringList{path("a")});

    // Forward returns through what was passed, the nearest first.
    context.goForward();
    EXPECT_EQ(context.directory(), path("c"));
    context.goForward();
    EXPECT_EQ(context.directory(), path("d"));
    EXPECT_FALSE(context.canGoForward());

    // Asking for more than there is goes to the oldest.
    context.goBackBy(100);
    EXPECT_EQ(context.directory(), path("a"));
    EXPECT_FALSE(context.canGoBack());
}

TEST(FolderLists, KeepsFavoritesAndRecentFoldersBetweenRuns) {
    QSettings().remove(QStringLiteral("Folders"));
    {
        iv::FolderLists lists;
        lists.addFavorite(QStringLiteral("/a"));
        lists.addFavorite(QStringLiteral("/b"));
        lists.addFavorite(QStringLiteral("/c"));
        lists.addFavorite(QStringLiteral("/a"));
        EXPECT_EQ(lists.favorites(), (QStringList{"/a", "/b", "/c"}));
        lists.moveFavorite(2, 1);
        EXPECT_EQ(lists.favorites(), (QStringList{"/a", "/c", "/b"}));
        lists.moveFavorite(0, -1);
        lists.moveFavorite(2, 3);
        EXPECT_EQ(lists.favorites(), (QStringList{"/a", "/c", "/b"}));
        lists.removeFavorite(QStringLiteral("/c"));
        lists.removeFavorite(QStringLiteral("/b"));

        lists.noteVisited(QStringLiteral("/x"));
        lists.noteVisited(QStringLiteral("/y"));
        lists.noteVisited(QStringLiteral("/x"));
        EXPECT_EQ(lists.recent(), (QStringList{"/x", "/y"}));
    }
    {
        iv::FolderLists lists;
        EXPECT_EQ(lists.favorites(), QStringList{"/a"}) << "a single one is still a list";
        EXPECT_EQ(lists.recent(), (QStringList{"/x", "/y"}));
        for (int i = 0; i < 30; ++i) {
            lists.noteVisited(QStringLiteral("/%1").arg(i));
        }
        EXPECT_EQ(lists.recent().size(), 12);
        EXPECT_EQ(lists.recent().front(), QStringLiteral("/29"));
    }
    QSettings().remove(QStringLiteral("Folders"));
}

TEST(BrowserPage, StatusSaysWhatIsInTheFolder) {
    BrowserPageFixture f;
    QQuickItem* status = findNamed(f.view.rootObject(), QStringLiteral("browserStatus"));
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(status->property("text").toString().startsWith(QStringLiteral("1 directory | 4 files (")));
    EXPECT_TRUE(status->property("text").toString().contains(QStringLiteral("| 1 selected (")));
}

TEST(BrowserPage, PreviewCollapsesAndDescribesItsImage) {
    BrowserPageFixture f;
    auto* preview = qobject_cast<ImageView*>(f.view.rootObject()->property("preview").value<QObject*>());
    ASSERT_NE(preview, nullptr);
    f.view.rootObject()->setProperty("active", true);
    ASSERT_TRUE(waitUntil([&] { return preview->status() == ImageView::Status::Ready; }));
    QQuickItem* summary = findNamed(f.view.rootObject(), QStringLiteral("previewSummary"));
    ASSERT_NE(summary, nullptr);
    const QString text = summary->property("text").toString();
    EXPECT_TRUE(text.startsWith(QStringLiteral("64x64 0.00MP 24bit JPEG "))) << text.toStdString();
    EXPECT_TRUE(QRegularExpression(QStringLiteral(" \\d+(B|kB) \\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d$")).match(text).hasMatch()) << text.toStdString();
    QQuickItem* camera = findNamed(f.view.rootObject(), QStringLiteral("previewCamera"));
    ASSERT_NE(camera, nullptr);
    EXPECT_FALSE(camera->isVisible()) << "this image has no EXIF";

    // The two buttons on the line.
    QQuickItem* actual = findNamed(f.view.rootObject(), QStringLiteral("previewActualSize"));
    QQuickItem* fit = findNamed(f.view.rootObject(), QStringLiteral("previewFit"));
    ASSERT_NE(actual, nullptr);
    ASSERT_NE(fit, nullptr);
    QMetaObject::invokeMethod(fit, "clicked");
    EXPECT_GT(preview->zoom(), 1) << "64 pixels fill the panel";
    QMetaObject::invokeMethod(actual, "clicked");
    EXPECT_DOUBLE_EQ(preview->zoom(), 1);

    // The title closes it, which leaves room for the tree and stops reading images.
    QQuickItem* header = findNamed(f.view.rootObject(), QStringLiteral("previewHeader"));
    ASSERT_NE(header, nullptr);
    QQuickItem* panel = header->parentItem()->parentItem();
    const qreal openHeight = panel->height();
    f.clickAt(header->mapToScene(QPointF(header->width() / 2, header->height() / 2)), Qt::NoModifier);
    EXPECT_TRUE(waitUntil([&] { return panel->height() < 40; })) << panel->height();
    EXPECT_EQ(preview->source(), QString());
    EXPECT_FALSE(summary->isVisible());

    f.clickAt(header->mapToScene(QPointF(header->width() / 2, header->height() / 2)), Qt::NoModifier);
    EXPECT_TRUE(waitUntil([&] { return panel->height() > 100; })) << panel->height();
    EXPECT_NEAR(panel->height(), openHeight, 1);
    EXPECT_EQ(preview->source(), f.context.currentPath());
}

TEST(PathBar, BackArrowOffersHistoryAndTheStarOffersFolders) {
    BrowserPageFixture f;
    QSettings().remove(QStringLiteral("Folders"));
    QQuickItem* bar = f.view.rootObject()->property("pathBar").value<QQuickItem*>();
    ASSERT_NE(bar, nullptr);
    const QString first = f.context.directory();
    const QString inner = QDir(first).filePath(QStringLiteral("inner"));
    const QString deeper = QDir(inner).filePath(QStringLiteral("deeper"));
    f.context.openPath(inner);
    f.context.openPath(deeper);

    QQuickItem* back = findNamed(bar, QStringLiteral("backButton"));
    ASSERT_NE(back, nullptr);
    QObject* history = bar->findChild<QObject*>(QStringLiteral("historyMenu"));
    ASSERT_NE(history, nullptr);
    QMetaObject::invokeMethod(back, "secondaryActivated");
    EXPECT_TRUE(waitUntil([&] { return history->property("visible").toBool(); }));
    EXPECT_EQ(history->property("count").toInt(), 2);
    QMetaObject::invokeMethod(history, "close");

    QQuickItem* star = findNamed(bar, QStringLiteral("folderButton"));
    ASSERT_NE(star, nullptr);
    EXPECT_FALSE(star->property("lit").toBool());
    QMetaObject::invokeMethod(star, "activated");
    EXPECT_TRUE(star->property("menuOpen").toBool());
    QStringList texts = rowTexts(star->property("menuRows").toList());
    EXPECT_TRUE(texts.value(0).startsWith(QStringLiteral("Add ")));
    EXPECT_EQ(texts.value(1), QStringLiteral("Recent"));
    EXPECT_TRUE(texts.contains(QDir::toNativeSeparators(inner)));
    EXPECT_FALSE(texts.contains(QDir::toNativeSeparators(deeper))) << "not where we already are";

    QVariantList rows = star->property("menuRows").toList();
    QMetaObject::invokeMethod(star, "choose", Q_ARG(QVariant, rows.first()));
    EXPECT_TRUE(star->property("lit").toBool());
    QMetaObject::invokeMethod(star, "activated");
    texts = rowTexts(star->property("menuRows").toList());
    EXPECT_TRUE(texts.value(0).startsWith(QStringLiteral("Remove ")));
    EXPECT_EQ(texts.value(1), QStringLiteral("Favorites"));
    EXPECT_EQ(texts.value(2), QDir::toNativeSeparators(deeper));

    // Choosing a recent folder goes there.
    rows = star->property("menuRows").toList();
    const auto toInner = std::ranges::find_if(rows, [&](const QVariant& row) { return row.toMap().value(QStringLiteral("path")).toString() == inner; });
    ASSERT_NE(toInner, rows.end());
    QMetaObject::invokeMethod(star, "choose", Q_ARG(QVariant, *toInner));
    EXPECT_EQ(f.context.directory(), inner);
    EXPECT_FALSE(star->property("lit").toBool());
    QSettings().remove(QStringLiteral("Folders"));
}

TEST(PathBar, AClickInItsListsDoesNotReachTheThumbnailsBelow) {
    TreeFixture f;
    QSettings().remove(QStringLiteral("Folders"));
    QQuickItem* bar = f.view.rootObject()->property("pathBar").value<QQuickItem*>();
    ASSERT_NE(bar, nullptr);
    QQuickItem* star = findNamed(bar, QStringLiteral("folderButton"));
    ASSERT_NE(star, nullptr);
    QQuickItem* grid = f.find("QQuickGridView");
    ASSERT_NE(grid, nullptr);
    const QString selectedBefore = f.selected();

    // The first row of the list, over the thumbnail of another file.
    QMetaObject::invokeMethod(star, "activated");
    ASSERT_TRUE(star->property("menuOpen").toBool());
    f.settle();
    const QPointF onRow = star->mapToScene(QPointF(200, star->height() + 13));
    int below = -1;
    QMetaObject::invokeMethod(grid, "indexAt", Q_RETURN_ARG(int, below), Q_ARG(qreal, grid->mapFromScene(onRow).x()), Q_ARG(qreal, grid->mapFromScene(onRow).y()));
    ASSERT_GE(below, 1) << "over a file other than the current one";
    f.clickAt(onRow, Qt::NoModifier);
    f.settle();
    EXPECT_FALSE(star->property("menuOpen").toBool());
    EXPECT_TRUE(star->property("lit").toBool()) << "the click did what the row says";
    EXPECT_EQ(f.selected(), selectedBefore);
    QSettings().remove(QStringLiteral("Folders"));
}

namespace {

// QPlatformDialogHelper::No, which is what Dialog.No is.
int QQuickDialogNoButton() { return 0x00010000; }

}  // namespace

TEST(ThumbnailStore, MeasuresAndClearsTheThumbnailsOnDisk) {
    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("a.jpg")));
    iv::ThumbnailStore store;
    QSignalSpy measured(&store, &iv::ThumbnailStore::measured);
    QSignalSpy cleared(&store, &iv::ThumbnailStore::cleared);
    const auto measure = [&] {
        measured.clear();
        store.measure();
        EXPECT_TRUE(waitUntil([&] { return !measured.isEmpty(); }));
        return measured.isEmpty() ? QVariantList() : measured.first();
    };

    store.clear();
    ASSERT_TRUE(waitUntil([&] { return !cleared.isEmpty(); }));
    EXPECT_TRUE(cleared.takeFirst().at(0).toBool());
    EXPECT_EQ(measure().value(1).toDouble(), 0);

    // A thumbnail made through the provider is kept.
    iv::ThumbnailProvider provider;
    std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(thumbnailId(dir.filePath(QStringLiteral("a.jpg"))), QSize(64, 64)));
    QSignalSpy finished(response.get(), &QQuickImageResponse::finished);
    ASSERT_TRUE(finished.wait(5000));
    const QVariantList some = measure();
    EXPECT_TRUE(some.value(0).toBool());
    EXPECT_EQ(some.value(1).toDouble(), 1);
    EXPECT_GT(some.value(2).toDouble(), 0);

    store.clear();
    ASSERT_TRUE(waitUntil([&] { return !cleared.isEmpty(); }));
    EXPECT_TRUE(cleared.takeFirst().at(0).toBool());
    EXPECT_EQ(measure().value(1).toDouble(), 0);
}

TEST(KeyNames, WritesShortcutsAsTheSystemDoes) {
    EXPECT_EQ(iv::KeyNames::text(QStringLiteral("Ctrl+Shift+N")), QKeySequence(QStringLiteral("Ctrl+Shift+N")).toString(QKeySequence::NativeText));
    EXPECT_EQ(iv::KeyNames::text(QStringLiteral("F5")), QStringLiteral("F5"));
    EXPECT_EQ(iv::KeyNames::text(static_cast<int>(QKeySequence::Copy)), QKeySequence::keyBindings(QKeySequence::Copy).value(0).toString(QKeySequence::NativeText));
    EXPECT_FALSE(iv::KeyNames::text(static_cast<int>(QKeySequence::Copy)).isEmpty());
    EXPECT_EQ(iv::KeyNames::text(QVariant()), QString());
    EXPECT_EQ(iv::KeyNames::text(QString()), QString());
}

TEST(AppMenuItem, ShowsTheShortcutOfItsAction) {
    QQmlEngine engine;
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
            width: 600; height: 400; visible: true
            property alias menu: menu
            property alias plain: plain
            property alias standard: standard
            property alias none: none
            property alias longest: longest
            Action { id: renameAction; text: "&Rename"; shortcut: "F2" }
            Action { id: longAction; text: "Delete Permanently Something Rather Long..."; shortcut: "Ctrl+Shift+Alt+Del" }
            Action { id: copyAction; text: "&Copy"; shortcut: StandardKey.Copy }
            Action { id: modeAction; text: "View" }
            AppMenu {
                id: menu
                AppMenuItem { id: longest; action: longAction }
                AppMenuItem { id: plain; action: renameAction }
                AppMenuItem { id: standard; action: copyAction }
                AppMenuItem { id: none; action: modeAction }
            }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    QMetaObject::invokeMethod(root->property("menu").value<QObject*>(), "open");
    const auto label = [&](const char* name) { return findNamed(root->property(name).value<QQuickItem*>(), QStringLiteral("shortcutText")); };
    ASSERT_NE(label("plain"), nullptr);
    EXPECT_EQ(label("plain")->property("text").toString(), QStringLiteral("F2"));
    EXPECT_TRUE(label("plain")->isVisible());
    EXPECT_LT(label("plain")->opacity(), 1);
    EXPECT_FALSE(label("standard")->property("text").toString().isEmpty());
    EXPECT_FALSE(label("none")->isVisible());
    // The menu makes room for it beside the longest name.
    auto* item = root->property("plain").value<QQuickItem*>();
    EXPECT_GE(item->property("implicitWidth").toReal(), label("plain")->implicitWidth() + 32);
    auto* menu = root->property("menu").value<QObject*>();
    auto* longest = root->property("longest").value<QQuickItem*>();
    EXPECT_GE(menu->property("width").toReal(), longest->implicitWidth());
    EXPECT_GE(longest->width(), longest->implicitWidth()) << "nothing runs into the shortcut";
}

TEST(MessageBox, TakesTheHeightOfAllItsText) {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 600; height: 400; visible: true
            property alias box: box
            MessageBox { id: box; title: "A caption"; standardButtons: Dialog.Ok }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* box = root->property("box").value<QObject*>();
    box->setProperty("text", QStringLiteral("one"));
    const qreal oneLine = box->property("implicitContentHeight").toReal();
    box->setProperty("text", QStringLiteral("one\n\nthree\nfour\nfive"));
    QMetaObject::invokeMethod(box, "open");
    EXPECT_TRUE(waitUntil([&] { return box->property("opened").toBool(); }));
    auto* content = box->property("contentItem").value<QQuickItem*>();
    EXPECT_TRUE(waitUntil([&] { return content->height() > std::max(oneLine, 32.0) + 30; }, 1000))
        << "the names of files to delete are in there, but it is " << content->height();
}

TEST(MessageBox, KeysWorkAsInAStandardMessageBox) {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 600; height: 400; visible: true
            property int accepts: 0
            property int rejects: 0
            property int outside: 0
            property alias box: box
            Shortcut { sequences: [StandardKey.Copy, "Return"]; onActivated: outside++ }
            MessageBox {
                id: box
                kind: "warning"
                title: "A caption"
                text: "Some text."
                standardButtons: Dialog.Yes | Dialog.No
                onAccepted: accepts++
                onRejected: rejects++
            }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    ASSERT_NE(window, nullptr);
    window->requestActivate();
    ASSERT_TRUE(waitUntil([&] { return window->isExposed(); }));
    auto* box = root->property("box").value<QObject*>();
    const auto show = [&] {
        QMetaObject::invokeMethod(box, "open");
        EXPECT_TRUE(waitUntil([&] { return box->property("opened").toBool(); }));
    };

    show();
    QGuiApplication::clipboard()->clear();
    QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
    EXPECT_EQ(QGuiApplication::clipboard()->text(), QStringLiteral("A caption\n\nSome text."));
    EXPECT_TRUE(box->property("visible").toBool()) << "copying leaves it open";

    // Enter presses the button in focus, the first one unless the dialog says otherwise.
    QTest::keyClick(window, Qt::Key_Return);
    EXPECT_EQ(root->property("accepts").toInt(), 1);
    EXPECT_TRUE(waitUntil([&] { return !box->property("visible").toBool(); }));

    box->setProperty("focusedButton", QQuickDialogNoButton());
    show();
    QTest::keyClick(window, Qt::Key_Return);
    EXPECT_EQ(root->property("rejects").toInt(), 1) << "a dangerous question can start at No";
    EXPECT_TRUE(waitUntil([&] { return !box->property("visible").toBool(); }));

    show();
    QTest::keyClick(window, Qt::Key_Escape);
    EXPECT_EQ(root->property("rejects").toInt(), 2);
    EXPECT_EQ(root->property("outside").toInt(), 0) << "none of these reach the window's shortcuts";
}

TEST(MessageBox, EnterOnACheckBoxPressesTheDefaultButton) {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 600; height: 400; visible: true
            property int accepts: 0
            property alias box: box
            property alias check: check
            MessageBox {
                id: box
                title: "A caption"
                text: "Some text."
                standardButtons: Dialog.Ok | Dialog.Cancel
                extras: [CheckBox { id: check; text: "An option" }]
                onAccepted: accepts++
            }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    window->requestActivate();
    ASSERT_TRUE(waitUntil([&] { return window->isExposed(); }));
    auto* box = root->property("box").value<QObject*>();
    auto* check = root->property("check").value<QQuickItem*>();
    QMetaObject::invokeMethod(box, "open");
    ASSERT_TRUE(waitUntil([&] { return box->property("opened").toBool(); }));

    check->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Space);
    EXPECT_TRUE(check->property("checked").toBool()) << "Space toggles";
    QTest::keyClick(window, Qt::Key_Return);
    EXPECT_TRUE(check->property("checked").toBool()) << "Enter does not";
    EXPECT_EQ(root->property("accepts").toInt(), 1);
}

namespace {

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

}  // namespace

TEST(ImageView, HoldingTheButtonShowsThatPlaceAtActualSize) {
    LoupeFixture f;
    ASSERT_DOUBLE_EQ(f.view->zoom(), 0.25);
    EXPECT_EQ(f.view->cursor().shape(), Qt::BitmapCursor) << "a magnifying glass";

    // Near the top left, which is a quarter of the way in on the image.
    f.press(QPointF(100, 100));
    EXPECT_DOUBLE_EQ(f.view->zoom(), 1);
    QRectF area = f.view->visibleArea();
    EXPECT_NEAR(area.x(), (400 - 100) / 1600.0, 0.001) << "image x 400 stays under the mouse at view x 100";
    EXPECT_NEAR(area.y(), (400 - 100) / 1600.0, 0.001);
    EXPECT_EQ(f.view->cursor().shape(), Qt::BitmapCursor);

    // Moving looks around while it is held.
    f.move(QPointF(80, 100));
    area = f.view->visibleArea();
    EXPECT_NEAR(area.x(), (400 - 80) / 1600.0, 0.001);
    EXPECT_DOUBLE_EQ(f.view->zoom(), 1);

    f.release(QPointF(80, 100));
    EXPECT_DOUBLE_EQ(f.view->zoom(), 0.25);
    EXPECT_EQ(f.view->visibleArea(), QRectF(0, 0, 1, 1));
    f.view->setSize(QSizeF(200, 200));
    EXPECT_DOUBLE_EQ(f.view->zoom(), 0.125) << "it fits the view again, not one zoom";
}

TEST(ImageView, CursorIsAHandWhereThereIsSomethingToPanTo) {
    LoupeFixture f;
    f.view->actualSize();
    EXPECT_EQ(f.view->cursor().shape(), Qt::OpenHandCursor);
    f.press(QPointF(200, 200));
    EXPECT_DOUBLE_EQ(f.view->zoom(), 1) << "stays as it is";
    EXPECT_EQ(f.view->cursor().shape(), Qt::ClosedHandCursor);
    f.move(QPointF(150, 200));
    EXPECT_EQ(f.view->cursor().shape(), Qt::ClosedHandCursor);
    const QRectF panned = f.view->visibleArea();
    f.release(QPointF(150, 200));
    EXPECT_EQ(f.view->cursor().shape(), Qt::OpenHandCursor);
    EXPECT_EQ(f.view->visibleArea(), panned) << "panning stays where it went";
}

TEST(ImageView, SmallImageShownWholeHasNoLoupeOrHand) {
    ViewFixture f;
    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("small.jpg")));
    ASSERT_TRUE(f.load(dir.filePath(QStringLiteral("small.jpg"))));
    ASSERT_DOUBLE_EQ(f.view->zoom(), 1);
    EXPECT_EQ(f.view->cursor().shape(), Qt::ArrowCursor);
}

TEST(ImageView, AnotherImageArrivingWhileHeldKeepsItsOwnView) {
    LoupeFixture f;
    JpegOptions options;
    options.width = 800;
    options.height = 800;
    writeJpeg(f.dir.filePath(QStringLiteral("other.jpg")), options);
    f.press(QPointF(100, 100));
    ASSERT_DOUBLE_EQ(f.view->zoom(), 1);
    ASSERT_TRUE(f.load(f.dir.filePath(QStringLiteral("other.jpg"))));
    const double zoom = f.view->zoom();
    f.release(QPointF(100, 100));
    EXPECT_DOUBLE_EQ(f.view->zoom(), zoom) << "the zoom of the first would not suit it";
}

TEST(ImageView, WheelCanAskForAnotherImageAndCtrlZooms) {
    LoupeFixture f;
    const double zoom = f.view->zoom();
    f.wheel(120);
    EXPECT_GT(f.view->zoom(), zoom) << "zooms unless told otherwise";

    f.view->fitToView();
    f.view->setWheelNavigates(true);
    QSignalSpy requests(f.view, &ImageView::navigateRequested);
    const double fitted = f.view->zoom();
    f.wheel(-120);
    ASSERT_EQ(requests.count(), 1);
    EXPECT_EQ(requests.takeFirst().at(0).toInt(), 1) << "towards the user is the next";
    f.wheel(120);
    ASSERT_EQ(requests.count(), 1);
    EXPECT_EQ(requests.takeFirst().at(0).toInt(), -1);
    EXPECT_DOUBLE_EQ(f.view->zoom(), fitted);

    // A touchpad's small steps add up to one, and turning back starts over.
    f.wheel(-50);
    f.wheel(-50);
    EXPECT_EQ(requests.count(), 0);
    f.wheel(-20);
    EXPECT_EQ(requests.count(), 1);
    requests.clear();
    f.wheel(-100);
    f.wheel(100);
    EXPECT_EQ(requests.count(), 0);

    f.wheel(120, Qt::ControlModifier);
    EXPECT_GT(f.view->zoom(), fitted);
    EXPECT_EQ(requests.count(), 0);
}

TEST(ViewerPage, WheelMovesThroughTheImages) {
    QTemporaryDir dir;
    for (const char* name : {"a.jpg", "b.jpg", "c.jpg"}) {
        writeJpeg(dir.filePath(QString::fromLatin1(name)));
    }
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(600, 400);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    ASSERT_NE(view.rootObject(), nullptr);
    auto* image = view.rootObject()->property("view").value<ImageView*>();
    ASSERT_NE(image, nullptr);
    // The info panel starts by sliding out of the way, and the image has no width until then.
    ASSERT_TRUE(waitUntil([&] { return view.isExposed() && image->width() > 500; }));
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return context.fileCount() == 3 && context.currentIndex() == 0; }));
    const auto wheel = [&](int delta) {
        const QPointF at(100, 100);
        QWheelEvent event(at, view.mapToGlobal(at), QPoint(), QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&view, &event);
    };
    wheel(120);
    EXPECT_EQ(context.currentIndex(), 0) << "stays at the first";
    wheel(-120);
    EXPECT_EQ(context.currentIndex(), 1);
    wheel(-360);
    EXPECT_EQ(context.currentIndex(), 2) << "stops at the last";
    wheel(120);
    EXPECT_EQ(context.currentIndex(), 1);
}

namespace {

// 64x32, so that a quarter turn shows in the size.
void writeWideJpeg(const QString& path, int height = 32) {
    writeJpeg(path, {.width = 64, .height = height, .orientation = 1});
}

QSize jpegSize(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray bytes = file.readAll();
    const auto decoded = iv::test::decodeJpeg({bytes.begin(), bytes.end()});
    return {decoded.width, decoded.height};
}

}  // namespace

TEST(LosslessJpeg, IsOfferedOnlyWhenEveryFileIsAJpeg) {
    BrowseContext context;
    EXPECT_TRUE(context.allJpeg({QStringLiteral("/a/b.jpg"), QStringLiteral("/a/C.JPEG")}));
    EXPECT_FALSE(context.allJpeg({QStringLiteral("/a/b.jpg"), QStringLiteral("/a/c.png")}));
    EXPECT_FALSE(context.allJpeg({}));
}

TEST(LosslessJpeg, ChangesFilesInPlaceAndTellsWhatShowsThem) {
    FileOpsFixture f(2);
    writeWideJpeg(f.path(QStringLiteral("a.jpg")));
    writeWideJpeg(f.path(QStringLiteral("b.jpg")));
    QSignalSpy edited(&f.context, &BrowseContext::filesEdited);

    f.context.editJpegs({f.path(QStringLiteral("a.jpg")), f.path(QStringLiteral("b.jpg"))}, BrowseContext::RotateClockwise, false);
    f.settle([&] { return edited.count() == 1; });
    EXPECT_TRUE(f.problems.isEmpty()) << f.problems.join(u'|').toStdString();
    EXPECT_EQ(edited.front().front().toStringList(), (QStringList{f.path(QStringLiteral("a.jpg")), f.path(QStringLiteral("b.jpg"))}));
    EXPECT_EQ(jpegSize(f.path(QStringLiteral("a.jpg"))), QSize(32, 64));
    EXPECT_EQ(jpegSize(f.path(QStringLiteral("b.jpg"))), QSize(32, 64));

    // An upright file has no orientation to apply, so nothing changes and nothing is reported.
    f.context.editJpegs({f.path(QStringLiteral("a.jpg"))}, BrowseContext::ApplyOrientation, false);
    f.settle([&] { return true; });
    EXPECT_TRUE(f.problems.isEmpty());
    EXPECT_EQ(edited.count(), 1);
}

TEST(LosslessJpeg, ReportsAnEdgeThatNeedsTrimmingAndTrimsWhenAllowed) {
    FileOpsFixture f(1);
    writeWideJpeg(f.path(QStringLiteral("a.jpg")), 40);
    f.context.editJpegs({f.path(QStringLiteral("a.jpg"))}, BrowseContext::FlipVertical, false);
    f.settle([&] { return !f.problems.isEmpty(); });
    ASSERT_EQ(f.problems.size(), 1);
    EXPECT_TRUE(f.problems.front().startsWith(QStringLiteral("a.jpg: "))) << f.problems.front().toStdString();
    EXPECT_TRUE(f.problems.front().contains(QStringLiteral("trimmed"))) << f.problems.front().toStdString();
    EXPECT_EQ(jpegSize(f.path(QStringLiteral("a.jpg"))), QSize(64, 40));

    f.problems.clear();
    f.context.editJpegs({f.path(QStringLiteral("a.jpg"))}, BrowseContext::FlipVertical, true);
    f.settle([&] { return jpegSize(f.path(QStringLiteral("a.jpg"))) == QSize(64, 32); });
    EXPECT_TRUE(f.problems.isEmpty());
}

TEST(LosslessJpeg, ConvertsToJxlAndSelectsTheResult) {
    BrowseContext probe;
    if (!probe.canConvertToJxl()) {
        GTEST_SKIP() << "built without JPEG XL";
    }
    FileOpsFixture f(2);
    f.context.convertToJxl({f.path(QStringLiteral("a.jpg"))}, false);
    f.settle([&] { return f.context.fileCount() == 3; });
    EXPECT_TRUE(f.problems.isEmpty()) << f.problems.join(u'|').toStdString();
    EXPECT_TRUE(QFileInfo::exists(f.path(QStringLiteral("a.jxl"))));
    EXPECT_TRUE(QFileInfo::exists(f.path(QStringLiteral("a.jpg"))));
    EXPECT_EQ(f.context.selectedPaths(), QStringList{f.path(QStringLiteral("a.jxl"))});

    // Once more, the existing file is reported and left as it is.
    f.context.convertToJxl({f.path(QStringLiteral("a.jpg"))}, false);
    f.settle([&] { return !f.problems.isEmpty(); });
    EXPECT_TRUE(f.problems.front().contains(QStringLiteral("a.jxl already exists"))) << f.problems.front().toStdString();

    // The original goes to the trash only when asked, and only after its conversion.
    const QString unique = QStringLiteral("jxl-%1.jpg").arg(QCoreApplication::applicationPid());
    ASSERT_TRUE(QFile::rename(f.path(QStringLiteral("b.jpg")), f.path(unique)));
    f.problems.clear();
    f.context.convertToJxl({f.path(unique)}, true);
    f.settle([&] { return QFileInfo::exists(f.path(QFileInfo(unique).baseName() + QStringLiteral(".jxl"))) && !QFileInfo::exists(f.path(unique)); });
    EXPECT_TRUE(f.problems.isEmpty()) << f.problems.join(u'|').toStdString();
    EXPECT_TRUE(isInTrash(unique));
}

TEST(LosslessJpeg, AnImageOnScreenLoadsAgainAfterAnEdit) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.jpg"));
    writeWideJpeg(path);
    ViewFixture f;
    f.view->setSource(path);
    ASSERT_TRUE(waitUntil([&] { return f.view->status() == ImageView::Status::Ready; }));
    ASSERT_EQ(f.view->imageWidth(), 64);

    ASSERT_TRUE(iv::transformJpegFile(QFileInfo(path).filesystemFilePath(), iv::JpegEdit::RotateClockwise));
    f.view->reload();
    EXPECT_TRUE(waitUntil([&] { return f.view->status() == ImageView::Status::Ready && f.view->imageWidth() == 32; }));
    EXPECT_EQ(f.view->imageHeight(), 64);
}

TEST(LosslessJpeg, AsksFirstUnlessToldNotTo) {
    QTemporaryDir dir;
    writeWideJpeg(dir.filePath(QStringLiteral("a.jpg")));
    writeJpeg(dir.filePath(QStringLiteral("b.jpg")));
    QImage(8, 8, QImage::Format_RGB32).save(dir.filePath(QStringLiteral("c.png")));

    QQmlEngine engine;
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
            width: 800; height: 600; visible: true
            property alias context: context
            property alias actions: actions
            BrowseContext { id: context }
            FileActions { id: actions; context: context; viewing: false }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* context = root->property("context").value<BrowseContext*>();
    auto* actions = root->property("actions").value<QObject*>();
    context->openPath(dir.filePath(QStringLiteral("a.jpg")));
    ASSERT_TRUE(waitUntil([&] { return context->fileCount() == 3 && context->selectedCount() == 1; }));

    auto* rotate = actions->property("rotateClockwiseAction").value<QObject*>();
    EXPECT_TRUE(actions->property("jpegTargets").toBool());
    EXPECT_TRUE(rotate->property("enabled").toBool());
    context->selectAll();
    EXPECT_FALSE(actions->property("jpegTargets").toBool()) << "c.png is not a JPEG";
    EXPECT_FALSE(rotate->property("enabled").toBool());
    context->clickFile(0, false, false);
    EXPECT_TRUE(rotate->property("enabled").toBool());

    auto* dialog = actions->findChild<QObject*>(QStringLiteral("jpegEditDialog"));
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(rotate, "trigger");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    EXPECT_EQ(dialog->property("title").toString(), QStringLiteral("Rotate 90° Clockwise"));
    EXPECT_TRUE(dialog->property("text").toString().startsWith(QStringLiteral("a.jpg will change in place")));
    EXPECT_EQ(jpegSize(dir.filePath(QStringLiteral("a.jpg"))), QSize(64, 32)) << "nothing happens before the answer";

    auto* content = dialog->property("contentItem").value<QQuickItem*>();
    QQuickItem* dontAsk = findNamed(content, QStringLiteral("dontAskCheck"));
    ASSERT_NE(dontAsk, nullptr);
    dontAsk->setProperty("checked", true);
    QMetaObject::invokeMethod(dialog, "accept");
    ASSERT_TRUE(waitUntil([&] { return !context->busy() && jpegSize(dir.filePath(QStringLiteral("a.jpg"))) == QSize(32, 64); }));

    // From now on it goes straight ahead, until the menu turns asking back on.
    auto* ask = actions->property("askBeforeEditingAction").value<QObject*>();
    EXPECT_FALSE(ask->property("checked").toBool());
    QMetaObject::invokeMethod(rotate, "trigger");
    EXPECT_FALSE(dialog->property("visible").toBool());
    ASSERT_TRUE(waitUntil([&] { return !context->busy() && jpegSize(dir.filePath(QStringLiteral("a.jpg"))) == QSize(64, 32); }));
    QMetaObject::invokeMethod(ask, "toggle");
    EXPECT_TRUE(ask->property("checked").toBool());
    QMetaObject::invokeMethod(rotate, "trigger");
    EXPECT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    QMetaObject::invokeMethod(dialog, "reject");
}

TEST(LosslessJpeg, WarnsWhenTheOriginalsGoToTheTrash) {
    BrowseContext probe;
    if (!probe.canConvertToJxl()) {
        GTEST_SKIP() << "built without JPEG XL";
    }
    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("a.jpg")));
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 800; height: 600; visible: true
            property alias context: context
            property alias actions: actions
            BrowseContext { id: context }
            FileActions { id: actions; context: context; viewing: false }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* context = root->property("context").value<BrowseContext*>();
    auto* actions = root->property("actions").value<QObject*>();
    context->openPath(dir.filePath(QStringLiteral("a.jpg")));
    ASSERT_TRUE(waitUntil([&] { return context->selectedCount() == 1; }));

    auto* dialog = actions->findChild<QObject*>(QStringLiteral("jxlDialog"));
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(actions->property("convertToJxlAction").value<QObject*>(), "trigger");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    EXPECT_EQ(dialog->property("kind").toString(), QStringLiteral("question"));
    EXPECT_TRUE(dialog->property("text").toString().contains(QStringLiteral("is kept")));

    QQuickItem* trash = findNamed(dialog->property("contentItem").value<QQuickItem*>(), QStringLiteral("trashOriginalsCheck"));
    ASSERT_NE(trash, nullptr);
    QMetaObject::invokeMethod(trash, "toggle");
    QMetaObject::invokeMethod(trash, "toggled");
    EXPECT_EQ(dialog->property("kind").toString(), QStringLiteral("warning"));
    EXPECT_TRUE(dialog->property("text").toString().contains(QStringLiteral("then goes to the trash")));
    // Left as it was found, as the choice is remembered.
    QMetaObject::invokeMethod(trash, "toggle");
    QMetaObject::invokeMethod(trash, "toggled");
    QMetaObject::invokeMethod(dialog, "reject");
}

TEST(SaveAs, OffersOnlyWhatCanBeWritten) {
    BrowseContext context;
    EXPECT_TRUE(context.saveFilters().contains(QStringLiteral("PNG (*.png)")));
    EXPECT_EQ(context.saveFormat(QStringLiteral("/a/b.JPG")), QStringLiteral("jpeg"));
    EXPECT_EQ(context.saveFormat(QStringLiteral("/a/b.tiff")), QStringLiteral("tiff"));
    EXPECT_EQ(context.saveFormat(QStringLiteral("/a/b.bmp")), QString());
    EXPECT_EQ(context.saveFormat(QStringLiteral("/a/b")), QString());
}

TEST(SaveAs, WritesTheFormatAndMakesTheResultCurrent) {
    FileOpsFixture f(1);
    QSignalSpy edited(&f.context, &BrowseContext::filesEdited);
    f.context.saveAs(f.path(QStringLiteral("a.jpg")), 0, f.path(QStringLiteral("a.png")), {});
    f.settle([&] { return f.context.fileCount() == 2; });
    EXPECT_TRUE(f.problems.isEmpty()) << f.problems.join(u'|').toStdString();
    EXPECT_EQ(f.context.selectedPaths(), QStringList{f.path(QStringLiteral("a.png"))});
    EXPECT_EQ(f.context.currentName(), QStringLiteral("a.png"));
    ASSERT_EQ(edited.count(), 1);
    const auto opened = iv::openImageSource(QFileInfo(f.path(QStringLiteral("a.png"))).filesystemFilePath());
    ASSERT_TRUE(opened);
    EXPECT_EQ((*opened)->subimages().front().stored.container, "PNG");

    f.context.saveAs(f.path(QStringLiteral("a.jpg")), 0, f.path(QStringLiteral("a.bmp")), {});
    f.settle([&] { return !f.problems.isEmpty(); });
    ASSERT_EQ(f.problems.size(), 1);
    EXPECT_TRUE(f.problems.front().startsWith(QStringLiteral("a.bmp: "))) << f.problems.front().toStdString();
}

TEST(SaveAs, TheOptionsReachTheWriter) {
    FileOpsFixture f(1);
    writeJpeg(f.path(QStringLiteral("a.jpg")), {.width = 64, .height = 64, .pixel = [](int x, int y) {
                  return iv::test::Rgb{static_cast<std::uint8_t>(x * 4), static_cast<std::uint8_t>((x ^ y) * 4), static_cast<std::uint8_t>(y * 4)};
              }});
    const auto save = [&](const QString& name, const QVariantMap& options) {
        f.context.saveAs(f.path(QStringLiteral("a.jpg")), 0, f.path(name), options);
        f.settle([&] { return QFileInfo::exists(f.path(name)); });
        EXPECT_TRUE(f.problems.isEmpty()) << f.problems.join(u'|').toStdString();
    };
    save(QStringLiteral("low.jpg"), {{QStringLiteral("quality"), 20}, {QStringLiteral("progressive"), false}});
    save(QStringLiteral("high.jpg"), {{QStringLiteral("quality"), 98}, {QStringLiteral("chroma"), QStringLiteral("4:4:4")}});
    EXPECT_LT(FileOpsFixture::size(f.path(QStringLiteral("low.jpg"))), FileOpsFixture::size(f.path(QStringLiteral("high.jpg"))));
    const auto read = [&](const QString& name) {
        QFile file(f.path(name));
        EXPECT_TRUE(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        return iv::test::decodeJpeg({bytes.begin(), bytes.end()});
    };
    EXPECT_FALSE(read(QStringLiteral("low.jpg")).progressive);
    EXPECT_TRUE(read(QStringLiteral("high.jpg")).progressive);
}

TEST(SaveAs, TheOptionsDialogStartsTheSave) {
    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("a.jpg")));
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 800; height: 600; visible: true
            property alias context: context
            property alias actions: actions
            BrowseContext { id: context }
            FileActions { id: actions; context: context; viewing: false }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* context = root->property("context").value<BrowseContext*>();
    auto* actions = root->property("actions").value<QObject*>();
    context->openPath(dir.filePath(QStringLiteral("a.jpg")));
    ASSERT_TRUE(waitUntil([&] { return context->selectedCount() == 1; }));
    EXPECT_TRUE(actions->property("saveAsAction").value<QObject*>()->property("enabled").toBool());

    auto* dialog = actions->findChild<QObject*>(QStringLiteral("saveOptionsDialog"));
    ASSERT_NE(dialog, nullptr);
    const QString target = dir.filePath(QStringLiteral("b.jpg"));
    QMetaObject::invokeMethod(actions, "chooseSaveOptions", Q_ARG(QVariant, dir.filePath(QStringLiteral("a.jpg"))),
                              Q_ARG(QVariant, 0), Q_ARG(QVariant, target), Q_ARG(QVariant, QVariant()));
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    EXPECT_EQ(dialog->property("title").toString(), QStringLiteral("Save As JPEG"));
    auto* content = dialog->property("contentItem").value<QQuickItem*>();
    QQuickItem* note = findNamed(content, QStringLiteral("recompressNote"));
    ASSERT_NE(note, nullptr);
    EXPECT_TRUE(note->isVisible()) << "a JPEG saved as JPEG loses quality";
    QQuickItem* quality = findNamed(content, QStringLiteral("qualitySlider"));
    ASSERT_NE(quality, nullptr);
    EXPECT_EQ(quality->property("value").toInt(), 90);
    EXPECT_TRUE(findNamed(content, QStringLiteral("keepMetadataCheck"))->property("checked").toBool());

    QMetaObject::invokeMethod(dialog, "accept");
    ASSERT_TRUE(waitUntil([&] { return !context->busy() && context->currentName() == QStringLiteral("b.jpg"); }));
    EXPECT_TRUE(QFileInfo::exists(target));
}

namespace {

// Stored 64 x 32 and turned a quarter by EXIF, so upright it is 32 x 64.
QString writeTurnedJpeg(const QTemporaryDir& dir, const QString& name = QStringLiteral("turned.jpg")) {
    const QString path = dir.filePath(name);
    writeJpeg(path, {.width = 64, .height = 32, .orientation = 6});
    return path;
}

bool settled(ImageView* view) {
    return waitUntil([&] { return view->status() == ImageView::Status::Ready; });
}

}  // namespace

TEST(Editing, AGrayJpegIsShownAndEditedInOneChannel) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("gray.jpg"));
    const iv::ImageBuffer gray = iv::ImageBuffer::create({120, 80}, iv::PixelFormat::Gray8);
    for (int y = 0; y < 80; ++y) {
        std::memset(gray.row(y), y < 40 ? 60 : 200, 120);
    }
    ASSERT_TRUE(iv::writeImage(QFileInfo(path).filesystemFilePath(), gray, {}, iv::JpegWriteOptions{}, true));

    ViewFixture f;
    ASSERT_TRUE(f.load(path));
    ASSERT_EQ(f.view->status(), ImageView::Status::Ready);
    EXPECT_TRUE(f.view->contrastAdjustable());
    const QImage shot = f.grab();
    const QColor top = shot.pixelColor(200, 200 - 20);  // shown at 1:1 in the middle
    const QColor bottom = shot.pixelColor(200, 200 + 20);
    EXPECT_NEAR(top.red(), 60, 4);
    EXPECT_EQ(top.red(), top.blue());
    EXPECT_NEAR(bottom.green(), 200, 4);

    f.view->crop(QRect(10, 10, 100, 60));
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageWidth(), 100);
    f.view->resizeImage(50, 30, false);
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageWidth(), 50);
    f.view->grayscale(1);
    ASSERT_TRUE(settled(f.view));
    f.view->straighten(3, 0, 0, 0, true);
    ASSERT_TRUE(settled(f.view));
    EXPECT_LT(f.view->imageWidth(), 50);
    EXPECT_TRUE(f.view->modified());
}

TEST(Editing, CropUndoRedoAndDiscardChangeWhatIsShown) {
    QTemporaryDir dir;
    ViewFixture f;
    ASSERT_TRUE(f.load(writeTurnedJpeg(dir)));
    EXPECT_TRUE(f.view->editable());
    EXPECT_FALSE(f.view->modified());
    EXPECT_EQ(f.view->imageWidth(), 32);
    EXPECT_EQ(f.view->imageHeight(), 64);

    // Upright pixels, as shown.
    f.view->crop(QRect(0, 8, 32, 40));
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageWidth(), 32);
    EXPECT_EQ(f.view->imageHeight(), 40);
    EXPECT_TRUE(f.view->modified());
    EXPECT_TRUE(f.view->canUndo());
    EXPECT_FALSE(f.view->editedImage().isNull());

    f.view->undo();
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageHeight(), 64);
    EXPECT_FALSE(f.view->modified());
    EXPECT_TRUE(f.view->canRedo());
    EXPECT_TRUE(f.view->editedImage().isNull());

    f.view->redo();
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageHeight(), 40);

    f.view->discardEdits();
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageHeight(), 64);
    EXPECT_FALSE(f.view->modified());
    EXPECT_FALSE(f.view->canUndo());
}

TEST(Editing, CropsOutsideTheImageOrOfAllOfItAreIgnored) {
    QTemporaryDir dir;
    ViewFixture f;
    ASSERT_TRUE(f.load(writeTurnedJpeg(dir)));
    f.view->crop(QRect(0, 0, 32, 64));
    f.view->crop(QRect(10, 10, 30, 10));
    QCoreApplication::processEvents();
    EXPECT_FALSE(f.view->modified());
    EXPECT_EQ(f.view->status(), ImageView::Status::Ready);
}

TEST(Editing, SaveAsWritesTheEditedImageAndMarksItSaved) {
    QTemporaryDir dir;
    const QString path = writeTurnedJpeg(dir);
    ViewFixture f;
    ASSERT_TRUE(f.load(path));
    f.view->crop(QRect(4, 4, 20, 30));
    ASSERT_TRUE(settled(f.view));
    const QVariant edited = f.view->editedImage();
    ASSERT_TRUE(edited.isValid());

    BrowseContext context;
    QSignalSpy saved(&context, &BrowseContext::editedImageSaved);
    const QString target = dir.filePath(QStringLiteral("cropped.jpg"));
    context.saveAs(path, 0, target, {}, edited);
    ASSERT_TRUE(waitUntil([&] { return saved.count() == 1; }));
    // Upright, so the 20 x 30 rectangle is stored as it is shown.
    EXPECT_EQ(jpegSize(target), QSize(20, 30));

    // Edited further meanwhile, it stays unsaved.
    f.view->crop(QRect(0, 0, 10, 10));
    ASSERT_TRUE(settled(f.view));
    f.view->markSaved(saved.front().front());
    EXPECT_TRUE(f.view->modified());
    f.view->undo();
    ASSERT_TRUE(settled(f.view));
    f.view->markSaved(saved.front().front());
    EXPECT_TRUE(f.view->modified()) << "a handle from before is not the image now";
    f.view->markSaved(f.view->editedImage());
    EXPECT_FALSE(f.view->modified());
}

TEST(Editing, TheCropOverlayDragsToWholePixelsAndCropApplies) {
    QTemporaryDir dir;
    writeTurnedJpeg(dir);
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(700, 500);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    ASSERT_NE(page, nullptr);
    auto* image = page->property("view").value<ImageView*>();
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageHeight() == 64; }));

    QMetaObject::invokeMethod(page, "startCrop");
    EXPECT_TRUE(page->property("cropping").toBool());
    auto* overlay = page->findChild<QQuickItem*>(QStringLiteral("cropOverlay"));
    ASSERT_NE(overlay, nullptr);
    EXPECT_EQ(overlay->property("cropRect").toRect(), QRect(0, 0, 32, 64));
    // The panel slides in and the image refits to the room left.
    ASSERT_TRUE(waitUntil([&] {
        const QPointF corner = image->imageToItem(QPointF(32, 64));
        return image->width() < 500 && corner.x() < image->width();
    }));
    waitUntil([] { return false; }, 300);

    // Drags the bottom right corner to the image position (20.6, 30.4); the edges go to the
    // nearest pixel boundaries.
    const QPointF from = overlay->mapToScene(image->imageToItem(QPointF(32, 64)));
    const QPointF to = overlay->mapToScene(image->imageToItem(QPointF(20.6, 30.4)));
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, from.toPoint());
    QTest::mouseMove(&view, ((from + to) / 2).toPoint());
    QTest::mouseMove(&view, to.toPoint());
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, to.toPoint());
    EXPECT_EQ(overlay->property("cropRect").toRect(), QRect(0, 0, 21, 30));

    QMetaObject::invokeMethod(page, "applyCrop");
    EXPECT_FALSE(page->property("cropping").toBool());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageHeight() == 30; }));
    EXPECT_EQ(image->imageWidth(), 21);
    EXPECT_TRUE(image->modified());
}

TEST(Editing, APressOutsideTheImageTakesAnEdgeAndAClickDrawsNothing) {
    QTemporaryDir dir;
    writeTurnedJpeg(dir);
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(700, 500);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    auto* image = page->property("view").value<ImageView*>();
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageHeight() == 64; }));
    QMetaObject::invokeMethod(page, "startCrop");
    auto* overlay = page->findChild<QQuickItem*>(QStringLiteral("cropOverlay"));
    ASSERT_TRUE(waitUntil([&] { return image->width() < 500; }));
    waitUntil([] { return false; }, 300);
    overlay->setProperty("cropRect", QRect(8, 16, 16, 32));
    const auto at = [&](double x, double y) { return overlay->mapToScene(image->imageToItem(QPointF(x, y))).toPoint(); };

    // A click on the image beside the rectangle leaves it alone.
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, at(2, 4));
    EXPECT_EQ(overlay->property("cropRect").toRect(), QRect(8, 16, 16, 32));

    // Well left of the image and above it: the top left corner, wherever it was grabbed.
    const QPoint outside = at(0, 0) - QPoint(20, 20);
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, outside);
    QTest::mouseMove(&view, outside + (at(4, 8) - at(8, 16)) / 2);
    QTest::mouseMove(&view, outside + (at(4, 8) - at(8, 16)));
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, outside + (at(4, 8) - at(8, 16)));
    EXPECT_EQ(overlay->property("cropRect").toRect(), QRect(4, 8, 20, 40));
}

namespace {

// Stored 8 x 4 at 16 bits and turned a quarter, so upright it is 4 x 8. Red counts columns and
// green rows of the stored image.
QString writeTurnedTiff(const QTemporaryDir& dir) {
    const QString path = dir.filePath(QStringLiteral("turned.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(),
                        {{.width = 8, .height = 4, .bits = 16, .orientation = 6, .pixel = [](int x, int y) {
                              return Rgb{static_cast<std::uint8_t>(x * 20), static_cast<std::uint8_t>(y * 20), 100};
                          }}});
    return path;
}

}  // namespace

TEST(Probe, ReadsThePixelShownAsTheFileStoresIt) {
    QTemporaryDir dir;
    ViewFixture f;
    ASSERT_TRUE(f.load(writeTurnedTiff(dir)));
    ASSERT_EQ(f.view->imageWidth(), 4);
    QSignalSpy probed(f.view, &ImageView::probed);

    // Shown at 1, 5, which turned back is the stored pixel 5, 2.
    f.view->probe(QPointF(1.7, 5.2));
    ASSERT_TRUE(waitUntil([&] { return probed.count() == 1; }));
    const QVariantMap pixel = probed.front().front().toMap();
    EXPECT_EQ(pixel.value(QStringLiteral("x")).toInt(), 1);
    EXPECT_EQ(pixel.value(QStringLiteral("y")).toInt(), 5);
    EXPECT_EQ(pixel.value(QStringLiteral("depth")).toString(), QStringLiteral("16-bit"));
    EXPECT_EQ(pixel.value(QStringLiteral("values")).toString(), QStringLiteral("25700, 10280, 25700"));
    EXPECT_EQ(pixel.value(QStringLiteral("fractions")).toString(), QStringLiteral("0.39216, 0.15686, 0.39216"));
    EXPECT_EQ(pixel.value(QStringLiteral("hex")).toString(), QStringLiteral("#642864"));
    EXPECT_TRUE(pixel.value(QStringLiteral("srgbHex")).toString().isEmpty()) << "no profile, nothing to convert";

    // Outside what is shown, nothing is read.
    f.view->probe(QPointF(4.5, 1));
    f.view->probe(QPointF(-0.5, 1));
    waitUntil([] { return false; }, 200);
    EXPECT_EQ(probed.count(), 1);
}

TEST(Probe, AClickReadsAndRemembersAndArrowsMoveByAPixel) {
    QTemporaryDir dir;
    writeTurnedTiff(dir);
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(700, 500);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    ASSERT_NE(page, nullptr);
    auto* image = page->property("view").value<ImageView*>();
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageHeight() == 8; }));

    QMetaObject::invokeMethod(page, "startProbe");
    EXPECT_TRUE(page->property("probing").toBool());
    auto* panel = page->findChild<QObject*>(QStringLiteral("probePanel"));
    ASSERT_NE(panel, nullptr);
    QMetaObject::invokeMethod(panel, "clearHistory");
    auto* area = page->findChild<QQuickItem*>(QStringLiteral("probeArea"));
    ASSERT_NE(area, nullptr);
    // The panel slides in and the image refits to the room left.
    ASSERT_TRUE(waitUntil([&] { return image->width() < 500; }));
    waitUntil([] { return false; }, 300);

    // Small images are not enlarged to fit; at 100% a click would land on pixel edges.
    for (int i = 0; i < 12; ++i) {
        image->zoomIn();
    }
    ASSERT_GT(image->imageToItem(QPointF(1, 0)).x() - image->imageToItem(QPointF(0, 0)).x(), 8);
    const QPoint at = image->mapToScene(image->imageToItem(QPointF(2.5, 3.5))).toPoint();
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, at);
    ASSERT_TRUE(waitUntil([&] { return panel->property("entries").toList().size() == 1; }));
    const QVariantMap read = panel->property("probed").toMap();
    EXPECT_EQ(read.value(QStringLiteral("x")).toInt(), 2);
    EXPECT_EQ(read.value(QStringLiteral("y")).toInt(), 3);
    EXPECT_EQ(read.value(QStringLiteral("values")).toString(), QStringLiteral("15420, 5140, 25700"));

    // A pixel to the right reads the next one without remembering it; Enter remembers it.
    QTest::keyClick(&view, Qt::Key_Right);
    ASSERT_TRUE(waitUntil([&] { return panel->property("probed").toMap().value(QStringLiteral("x")).toInt() == 3; }));
    EXPECT_EQ(panel->property("entries").toList().size(), 1);
    QTest::keyClick(&view, Qt::Key_Return);
    ASSERT_EQ(panel->property("entries").toList().size(), 2);
    EXPECT_EQ(panel->property("entries").toList().front().toMap().value(QStringLiteral("x")).toInt(), 3);

    QMetaObject::invokeMethod(page, "stopProbe");
    EXPECT_FALSE(page->property("probing").toBool());
}

TEST(Editing, ResizeScalesToTheSizeAndRefusesOnesTooLarge) {
    QTemporaryDir dir;
    ViewFixture f;
    ASSERT_TRUE(f.load(writeTurnedJpeg(dir)));
    EXPECT_TRUE(f.view->resizeProblem(16, 32).isEmpty());
    EXPECT_FALSE(f.view->resizeProblem(0, 32).isEmpty());
    EXPECT_FALSE(f.view->resizeProblem(65535, 65535).isEmpty());

    f.view->resizeImage(16, 32, false);
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageWidth(), 16);
    EXPECT_EQ(f.view->imageHeight(), 32);
    EXPECT_TRUE(f.view->modified());
    f.view->undo();
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageHeight(), 64);
}

TEST(Editing, GrayscaleKeepsTheSizeAndUndoes) {
    QTemporaryDir dir;
    ViewFixture f;
    ASSERT_TRUE(f.load(writeTurnedJpeg(dir)));
    for (const int method : {0, 1}) {
        f.view->grayscale(method);
        ASSERT_TRUE(settled(f.view));
        EXPECT_TRUE(f.view->modified());
        EXPECT_EQ(f.view->imageHeight(), 64);
        f.view->undo();
        ASSERT_TRUE(settled(f.view));
        EXPECT_FALSE(f.view->modified());
    }
}

TEST(Editing, TheResizeDialogKeepsTheRatioOrFillsAnother) {
    QTemporaryDir dir;
    const QString path = writeTurnedJpeg(dir);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import ImageViewer
        Window {
            width: 600; height: 500; visible: true
            property alias view: image
            property alias dialog: dialog
            ImageView { id: image; anchors.fill: parent }
            ResizeDialog { id: dialog; view: image }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* image = root->property("view").value<ImageView*>();
    auto* dialog = root->property("dialog").value<QObject*>();
    image->setSource(path);
    ASSERT_TRUE(settled(image));
    auto* content = dialog->property("contentItem").value<QQuickItem*>();
    QQuickItem* width = findNamed(content, QStringLiteral("resizeWidth"));
    QQuickItem* height = findNamed(content, QStringLiteral("resizeHeight"));
    ASSERT_NE(width, nullptr);

    QMetaObject::invokeMethod(dialog, "open");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    EXPECT_EQ(width->property("value").toInt(), 32);
    EXPECT_EQ(height->property("value").toInt(), 64);
    // Typing a width sets the height for the ratio.
    width->setProperty("value", 16);
    EXPECT_EQ(height->property("value").toInt(), 32);
    EXPECT_FALSE(dialog->property("otherRatio").toBool());
    QMetaObject::invokeMethod(dialog, "accept");
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageWidth() == 16; }));
    EXPECT_EQ(image->imageHeight(), 32);

    // A screen of another ratio, filled by cropping the middle.
    QMetaObject::invokeMethod(dialog, "open");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    EXPECT_EQ(width->property("value").toInt(), 16) << "starts from the size the image has now";
    const QVariantMap screen{{QStringLiteral("width"), 40}, {QStringLiteral("height"), 30}};
    QMetaObject::invokeMethod(dialog, "applyPreset", Q_ARG(QVariant, screen));
    EXPECT_TRUE(dialog->property("otherRatio").toBool());
    EXPECT_EQ(dialog->property("target").toSizeF(), QSizeF(40, 30));
    QQuickItem* fill = findNamed(content, QStringLiteral("resizeFill"));
    ASSERT_NE(fill, nullptr);
    fill->setProperty("checked", true);
    QMetaObject::invokeMethod(fill, "toggled");
    QMetaObject::invokeMethod(dialog, "accept");
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageWidth() == 40; }));
    EXPECT_EQ(image->imageHeight(), 30);
}

TEST(Editing, LosslessJpegToolsWaitUntilTheEditsAreGone) {
    QTemporaryDir dir;
    const QString path = writeTurnedJpeg(dir);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 600; height: 500; visible: true
            property alias context: context
            property alias actions: actions
            property alias view: image
            BrowseContext { id: context }
            ImageView { id: image; anchors.fill: parent; source: context.currentPath }
            FileActions { id: actions; context: context; viewing: true; editView: image }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* context = root->property("context").value<BrowseContext*>();
    auto* actions = root->property("actions").value<QObject*>();
    auto* image = root->property("view").value<ImageView*>();
    context->openPath(path);
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageHeight() == 64; }));
    auto* rotate = actions->property("rotateClockwiseAction").value<QObject*>();
    EXPECT_TRUE(rotate->property("enabled").toBool());

    image->crop(QRect(0, 0, 16, 16));
    ASSERT_TRUE(settled(image));
    EXPECT_FALSE(rotate->property("enabled").toBool()) << "it would turn the file, not the crop shown";
    // Saved, the crop is still what is shown.
    image->markSaved(image->editedImage());
    EXPECT_FALSE(rotate->property("enabled").toBool());
    image->undo();
    ASSERT_TRUE(settled(image));
    EXPECT_TRUE(rotate->property("enabled").toBool());
}

TEST(Editing, SaveOverwritesTheFileAfterAWarning) {
    QTemporaryDir dir;
    const QString path = writeTurnedJpeg(dir);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 600; height: 500; visible: true
            property alias context: context
            property alias actions: actions
            property alias view: image
            BrowseContext { id: context; onEditedImageSaved: (image) => view.markSaved(image) }
            ImageView { id: image; anchors.fill: parent; source: context.currentPath }
            FileActions { id: actions; context: context; viewing: true; editView: image }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* context = root->property("context").value<BrowseContext*>();
    auto* actions = root->property("actions").value<QObject*>();
    auto* image = root->property("view").value<ImageView*>();
    context->openPath(path);
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageHeight() == 64; }));
    auto* save = actions->property("saveAction").value<QObject*>();
    EXPECT_FALSE(save->property("enabled").toBool()) << "nothing to save";

    image->crop(QRect(0, 0, 16, 24));
    ASSERT_TRUE(settled(image));
    ASSERT_TRUE(save->property("enabled").toBool());
    auto* dialog = actions->findChild<QObject*>(QStringLiteral("overwriteDialog"));
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(save, "trigger");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    EXPECT_TRUE(dialog->property("text").toString().startsWith(QStringLiteral("turned.jpg will be replaced")));
    EXPECT_EQ(jpegSize(path), QSize(64, 32)) << "nothing happens before the answer";

    QMetaObject::invokeMethod(dialog, "accept");
    // Stored upright now, as it was shown.
    ASSERT_TRUE(waitUntil([&] { return !context->busy() && jpegSize(path) == QSize(16, 24); }));
    ASSERT_TRUE(waitUntil([&] { return !image->modified() && image->status() == ImageView::Status::Ready; }));
    EXPECT_EQ(image->imageHeight(), 24);

    // Rename keeps the file and saves beside it, under a name not taken.
    image->crop(QRect(0, 0, 8, 8));
    ASSERT_TRUE(settled(image));
    QMetaObject::invokeMethod(save, "trigger");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    QMetaObject::invokeMethod(dialog, "applied");
    auto* names = actions->findChild<QObject*>(QStringLiteral("nameDialog"));
    ASSERT_TRUE(waitUntil([&] { return names->property("opened").toBool(); }));
    auto* content = names->property("contentItem").value<QQuickItem*>();
    QQuickItem* field = findNamed(content, QStringLiteral("nameField"));
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->property("text").toString(), QStringLiteral("turned-edited.jpg"));
    field->setProperty("text", QStringLiteral("turned.jpg"));
    QMetaObject::invokeMethod(names, "submit");
    EXPECT_TRUE(names->property("visible").toBool()) << "that name is taken";
    EXPECT_FALSE(findNamed(content, QStringLiteral("nameProblem"))->property("text").toString().isEmpty());
    field->setProperty("text", QStringLiteral("small.png"));
    QMetaObject::invokeMethod(names, "submit");
    const QString copy = dir.filePath(QStringLiteral("small.png"));
    ASSERT_TRUE(waitUntil([&] { return !context->busy() && QImage(copy).size() == QSize(8, 8); }));
    EXPECT_EQ(jpegSize(path), QSize(16, 24)) << "the file stays as it was";
}

namespace {

// 400 x 300, light with a dark rectangle turned 4 degrees clockwise around the middle.
QString writeTiltedJpeg(const QTemporaryDir& dir) {
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

}  // namespace

TEST(Straighten, FindsTheEdgesFitsTheAnglesAndApplies) {
    QTemporaryDir dir;
    ViewFixture f;
    ASSERT_TRUE(f.load(writeTiltedJpeg(dir)));
    QSignalSpy ready(f.view, &ImageView::straightenReady);
    EXPECT_TRUE(f.view->detectStraightLines().isEmpty()) << "nothing before it is prepared";
    f.view->prepareStraighten();
    ASSERT_TRUE(waitUntil([&] { return ready.count() == 1; }));

    const QVariantList lines = f.view->detectStraightLines();
    ASSERT_GE(lines.size(), 4);
    const QVariantMap fit = f.view->fitStraightLines(lines, 0, 0, true);
    EXPECT_NEAR(fit.value(QStringLiteral("roll")).toDouble(), -4, 0.3);
    EXPECT_EQ(fit.value(QStringLiteral("pitch")).toDouble(), 0) << "rotation only";

    const double roll = fit.value(QStringLiteral("roll")).toDouble();
    const QSize size = f.view->straightenedSize(roll, 0, 0, 0, true);
    EXPECT_LT(size.width(), 400);
    EXPECT_NEAR(static_cast<double>(size.width()) / size.height(), 4.0 / 3, 0.02);
    EXPECT_FALSE(f.view->straightenPreview({roll, 0, 0}, 0, true).isNull());

    f.view->straighten(roll, 0, 0, 0, true);
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageWidth(), size.width());
    EXPECT_EQ(f.view->imageHeight(), size.height());
    EXPECT_TRUE(f.view->modified());
    f.view->undo();
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageWidth(), 400);
}

TEST(Straighten, ALineDrawnOnTheImageSetsTheRotation) {
    QTemporaryDir dir;
    writeTiltedJpeg(dir);
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(900, 600);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    ASSERT_NE(page, nullptr);
    auto* image = page->property("view").value<ImageView*>();
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageWidth() == 400; }));

    QMetaObject::invokeMethod(page, "startStraighten");
    EXPECT_TRUE(page->property("straightening").toBool());
    auto* panel = page->findChild<QObject*>(QStringLiteral("straightenPanel"));
    auto* overlay = page->findChild<QQuickItem*>(QStringLiteral("straightenOverlay"));
    ASSERT_NE(panel, nullptr);
    ASSERT_NE(overlay, nullptr);
    ASSERT_TRUE(waitUntil([&] { return panel->property("ready").toBool() && image->width() < 700; }));
    waitUntil([] { return false; }, 300);

    // Along a line that falls 21 pixels over 300: levelled by turning 4 degrees anticlockwise.
    const auto at = [&](double x, double y) { return image->mapToScene(image->imageToItem(QPointF(x, y))).toPoint(); };
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, at(50, 100));
    QTest::mouseMove(&view, at(200, 110));
    QTest::mouseMove(&view, at(350, 121));
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, at(350, 121));
    ASSERT_EQ(overlay->property("lines").toList().size(), 1);
    EXPECT_NEAR(panel->property("roll").toDouble(), -4, 0.3);
    EXPECT_EQ(panel->property("pitch").toDouble(), 0);

    // A click draws nothing.
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, at(200, 250));
    EXPECT_EQ(overlay->property("lines").toList().size(), 1);

    QMetaObject::invokeMethod(page, "applyStraighten");
    EXPECT_FALSE(page->property("straightening").toBool());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageWidth() < 400; }));
    EXPECT_TRUE(image->modified());
}

TEST(Straighten, TheFocalLengthComesFromTheCameraData) {
    QTemporaryDir dir;
    iv::test::JpegOptions options;
    options.exif = iv::test::exampleExifBlock(true);  // 50 mm, 75 mm in 35mm
    const QString path = dir.filePath(QStringLiteral("camera.jpg"));
    iv::test::writeFile(QFileInfo(path).filesystemFilePath(), encodeJpeg(options));
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(900, 600);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    ASSERT_NE(page, nullptr);
    auto* image = page->property("view").value<ImageView*>();
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready; }));
    ASSERT_EQ(image->focalLength35mm(), 75);

    QMetaObject::invokeMethod(page, "startStraighten");
    auto* panel = page->findChild<QObject*>(QStringLiteral("straightenPanel"));
    auto* field = page->findChild<QObject*>(QStringLiteral("straightenFocal"));
    ASSERT_NE(panel, nullptr);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(panel->property("focal").toDouble(), 75);
    EXPECT_EQ(field->property("text").toString(), QStringLiteral("75"));
}

TEST(Main, StraightenFromTheBrowserOpensTheImageFirst) {
    QTemporaryDir dir;
    writeTiltedJpeg(dir);
    QFile::copy(dir.filePath(QStringLiteral("tilted.jpg")), dir.filePath(QStringLiteral("tilted2.jpg")));
    QQmlApplicationEngine engine;
    engine.addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
    engine.setInitialProperties({{QStringLiteral("initialPath"), dir.path()}});
    engine.loadFromModule("ImageViewer", "Main");
    ASSERT_EQ(engine.rootObjects().size(), 1);
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    ASSERT_NE(window, nullptr);
    window->requestActivate();
    ASSERT_TRUE(waitUntil([&] { return window->isExposed(); }));
    auto* context = window->findChild<BrowseContext*>();
    ASSERT_TRUE(waitUntil([&] { return context->fileCount() == 2; }));
    ASSERT_FALSE(window->property("viewing").toBool());

    // Two chosen: nothing to straighten.
    context->selectAll();
    QTest::keyClick(window, Qt::Key_S);
    EXPECT_FALSE(window->property("viewing").toBool());

    context->clickFile(1, false, false);
    ASSERT_EQ(context->selectedPaths().size(), 1);
    QTest::keyClick(window, Qt::Key_S);
    EXPECT_TRUE(window->property("viewing").toBool());
    auto* image = window->property("activeView").value<ImageView*>();
    ASSERT_TRUE(waitUntil([&] { return window->property("straightening").toBool(); }));
    EXPECT_TRUE(image->source().endsWith(QStringLiteral("tilted2.jpg")));
    EXPECT_EQ(image->status(), ImageView::Status::Ready);
}

TEST(Main, DeleteWhileStraighteningRemovesTheLineNotTheFile) {
    QTemporaryDir dir;
    writeTiltedJpeg(dir);
    const QString path = dir.filePath(QStringLiteral("tilted.jpg"));
    ASSERT_TRUE(QFile::exists(path));
    QQmlApplicationEngine engine;
    engine.addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
    engine.setInitialProperties({{QStringLiteral("initialPath"), path}});
    engine.loadFromModule("ImageViewer", "Main");
    ASSERT_EQ(engine.rootObjects().size(), 1);
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    ASSERT_NE(window, nullptr);
    window->requestActivate();
    ASSERT_TRUE(waitUntil([&] { return window->isExposed(); }));
    auto* image = window->property("activeView").value<ImageView*>();
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageWidth() == 400; }));

    QTest::keyClick(window, Qt::Key_S);
    ASSERT_TRUE(window->property("straightening").toBool());
    auto* overlay = window->findChild<QQuickItem*>(QStringLiteral("straightenOverlay"));
    ASSERT_NE(overlay, nullptr);
    waitUntil([] { return false; }, 300);
    const auto at = [&](double x, double y) { return image->mapToScene(image->imageToItem(QPointF(x, y))).toPoint(); };
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at(50, 100));
    QTest::mouseMove(window, at(200, 110));
    QTest::mouseMove(window, at(350, 121));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at(350, 121));
    ASSERT_EQ(overlay->property("lines").toList().size(), 1);
    ASSERT_EQ(overlay->property("selected").toInt(), 0);

    QTest::keyClick(window, Qt::Key_Delete);
    EXPECT_EQ(overlay->property("lines").toList().size(), 0);
    waitUntil([] { return false; }, 300);
    EXPECT_TRUE(QFile::exists(path));
    EXPECT_TRUE(window->property("straightening").toBool());
}

TEST(Main, LeavingAnEditedImageAsksFirst) {
    QTemporaryDir dir;
    writeTurnedJpeg(dir, QStringLiteral("a.jpg"));
    writeTurnedJpeg(dir, QStringLiteral("b.jpg"));
    QQmlApplicationEngine engine;
    engine.addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
    engine.setInitialProperties({{QStringLiteral("initialPath"), dir.filePath(QStringLiteral("a.jpg"))}});
    engine.loadFromModule("ImageViewer", "Main");
    ASSERT_EQ(engine.rootObjects().size(), 1);
    QObject* window = engine.rootObjects().front();
    auto* context = window->findChild<BrowseContext*>();
    ASSERT_NE(context, nullptr);
    ASSERT_TRUE(waitUntil([&] { return context->fileCount() == 2; }));
    auto* image = window->property("activeView").value<ImageView*>();
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->source().endsWith(QStringLiteral("a.jpg")); }));
    auto* dialog = window->findChild<QObject*>(QStringLiteral("unsavedDialog"));
    ASSERT_NE(dialog, nullptr);

    // Without changes it just goes.
    QMetaObject::invokeMethod(window, "next");
    EXPECT_EQ(context->currentIndex(), 1);
    EXPECT_FALSE(dialog->property("opened").toBool());
    QMetaObject::invokeMethod(window, "previous");
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->source().endsWith(QStringLiteral("a.jpg")); }));

    image->crop(QRect(0, 0, 16, 16));
    ASSERT_TRUE(waitUntil([&] { return image->modified() && image->status() == ImageView::Status::Ready; }));
    EXPECT_TRUE(window->property("title").toString().startsWith(QLatin1Char('*')));

    QMetaObject::invokeMethod(window, "next");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    EXPECT_EQ(context->currentIndex(), 0) << "stays until answered";
    QMetaObject::invokeMethod(dialog, "reject");
    ASSERT_TRUE(waitUntil([&] { return !dialog->property("visible").toBool(); }));
    EXPECT_EQ(context->currentIndex(), 0);
    EXPECT_TRUE(image->modified());

    QMetaObject::invokeMethod(window, "next");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    QMetaObject::invokeMethod(dialog, "discarded");
    EXPECT_EQ(context->currentIndex(), 1);
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->source().endsWith(QStringLiteral("b.jpg")); }));
    EXPECT_FALSE(image->modified());
}

TEST(Adjust, ThePanelPreviewsCancelsAndApplies) {
    QTemporaryDir dir;
    writeTiltedJpeg(dir);
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(900, 600);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    ASSERT_NE(page, nullptr);
    auto* image = page->property("view").value<ImageView*>();
    context.openPath(dir.path());
    ASSERT_TRUE(settled(image));
    EXPECT_FALSE(image->grayImage()) << "gray pixels in a colour JPEG are still colour";
    auto* panel = page->findChild<QObject*>(QStringLiteral("adjustPanel"));
    ASSERT_NE(panel, nullptr);

    QMetaObject::invokeMethod(page, "startAdjust");
    ASSERT_TRUE(page->property("adjusting").toBool());
    EXPECT_TRUE(page->property("toolActive").toBool());
    EXPECT_TRUE(image->colorPreviewActive());
    EXPECT_FALSE(panel->property("changes").toBool());
    panel->setProperty("saturation", 50);
    EXPECT_DOUBLE_EQ(image->colorPreviewChroma(), 0.5);
    EXPECT_TRUE(panel->property("changes").toBool());
    // Without shaders there is no preview, but the picture stays.
    waitUntil([] { return false; }, 300);
    const QPoint corner = image->mapToScene(image->imageToItem(QPointF(10, 10))).toPoint() * view.devicePixelRatio();
    EXPECT_GT(view.grabWindow().pixelColor(corner).red(), 180);

    QMetaObject::invokeMethod(page, "cancelAdjust");
    EXPECT_FALSE(image->colorPreviewActive());
    EXPECT_FALSE(image->modified());

    // Opening it again starts afresh.
    QMetaObject::invokeMethod(page, "startAdjust");
    EXPECT_DOUBLE_EQ(panel->property("saturation").toDouble(), 100);
    panel->setProperty("black", 40);
    EXPECT_TRUE(image->colorPreviewTone());
    QMetaObject::invokeMethod(page, "applyAdjust");
    EXPECT_FALSE(page->property("adjusting").toBool());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->modified(); }));
    EXPECT_FALSE(image->colorPreviewActive());
    image->undo();
    ASSERT_TRUE(settled(image));
    EXPECT_FALSE(image->modified());

    // Another image ends the tool.
    QMetaObject::invokeMethod(page, "startAdjust");
    image->setSource({});
    EXPECT_FALSE(page->property("adjusting").toBool());
    EXPECT_FALSE(image->colorPreviewActive());
}

TEST(Adjust, AGrayPictureTakesLevelsAndLightnessOnly) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("gray.png"));
    const iv::ImageBuffer gray = iv::ImageBuffer::create({60, 40}, iv::PixelFormat::Gray8);
    for (int y = 0; y < 40; ++y) {
        std::memset(gray.row(y), 30 + y * 4, 60);
    }
    ASSERT_TRUE(iv::writeImage(QFileInfo(path).filesystemFilePath(), gray, {}, iv::PngWriteOptions{}, true));
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(900, 600);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    auto* image = page->property("view").value<ImageView*>();
    context.openPath(path);
    ASSERT_TRUE(settled(image));
    ASSERT_TRUE(image->grayImage());
    auto* panel = page->findChild<QObject*>(QStringLiteral("adjustPanel"));
    auto* saturation = page->findChild<QQuickItem*>(QStringLiteral("adjustSaturation"));
    auto* load = page->findChild<QQuickItem*>(QStringLiteral("adjustLoadCube"));
    ASSERT_NE(saturation, nullptr);
    ASSERT_NE(load, nullptr);

    QMetaObject::invokeMethod(page, "startAdjust");
    EXPECT_FALSE(saturation->isEnabled());
    EXPECT_FALSE(load->isEnabled());
    panel->setProperty("saturation", 0);
    EXPECT_DOUBLE_EQ(image->colorPreviewChroma(), 1) << "the preview leaves chroma alone";
    EXPECT_FALSE(panel->property("changes").toBool());

    panel->setProperty("midtones", 1.5);
    EXPECT_TRUE(image->colorPreviewLightness());
    QMetaObject::invokeMethod(page, "applyAdjust");
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->modified(); }));
    EXPECT_TRUE(image->grayImage()) << "still one channel";
}

TEST(Adjust, LevelsHandlesDragOnTheGradients) {
    QTemporaryDir dir;
    writeTiltedJpeg(dir);
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(900, 700);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    auto* image = page->property("view").value<ImageView*>();
    context.openPath(dir.path());
    ASSERT_TRUE(settled(image));
    QMetaObject::invokeMethod(page, "startAdjust");
    auto* panel = page->findChild<QObject*>(QStringLiteral("adjustPanel"));
    auto* input = page->findChild<QQuickItem*>(QStringLiteral("levelsInput"));
    auto* output = page->findChild<QQuickItem*>(QStringLiteral("levelsOutput"));
    ASSERT_NE(input, nullptr);
    ASSERT_NE(output, nullptr);
    ASSERT_TRUE(waitUntil([&] { return image->histogram().isValid(); })) << "the histogram is on while adjusting";
    waitUntil([] { return false; }, 300);  // the panel slides in

    // Positions on a bar for a level, on the row of the handles.
    const auto at = [&](QQuickItem* bar, double level) {
        const double inset = 5.5;
        return bar->mapToScene(QPointF(inset + level / 255 * (bar->width() - 2 * inset), bar->height() - 4)).toPoint();
    };
    const auto drag = [&](QQuickItem* bar, double from, double to) {
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, at(bar, from));
        QTest::mouseMove(&view, at(bar, (from + to) / 2));
        QTest::mouseMove(&view, at(bar, to));
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, at(bar, to));
    };
    drag(input, 0, 51);
    EXPECT_NEAR(panel->property("black").toDouble(), 51, 1);
    // The gamma handle stands halfway between black and white; a quarter of the way is gamma 2.
    const double black = panel->property("black").toDouble();
    drag(input, (black + 255) / 2, black + (255 - black) / 4);
    EXPECT_NEAR(panel->property("gamma").toDouble(), 2, 0.05);
    drag(input, 255, 200);
    EXPECT_NEAR(panel->property("white").toDouble(), 200, 1);
    EXPECT_NEAR(panel->property("gamma").toDouble(), 2, 0.05) << "gamma stays when white moves";
    drag(output, 0, 64);
    EXPECT_NEAR(panel->property("outBlack").toDouble(), 64, 1);
    EXPECT_TRUE(image->colorPreviewTone());

    QTest::mouseDClick(&view, Qt::LeftButton, Qt::NoModifier, at(output, 64));
    EXPECT_EQ(panel->property("outBlack").toDouble(), 0) << "a double click puts it back";
}

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
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsFolder.path());
    QCoreApplication::setOrganizationName(QStringLiteral("ImageViewerTests"));
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

