#include <gtest/gtest.h>

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickTextureFactory>
#include <QQuickView>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QtQml/QQmlExtensionPlugin>

#include <functional>
#include <memory>

#include "test_jpeg.h"
#include "test_tiff.h"
#include "core/color.h"
#include "ui/browse_context.h"
#include "ui/color_settings.h"
#include "ui/directory_tree_model.h"
#include "ui/histogram_handle.h"
#include "ui/histogram_view.h"
#include "ui/image_view.h"
#include "ui/thumbnail_provider.h"

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
        EXPECT_TRUE(waitUntil([&] { return context.fileCount() == count && context.selectedCount() == 1; }));
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

TEST(BrowserPage, TreeExpandsTheOpenedFolder) {
    TreeFixture f;
    const QString inner = f.path(QStringLiteral("inner"));
    EXPECT_TRUE(waitUntil([&] { return f.rowOf(inner) > f.rowOf(f.context.directory()); }));
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

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    // Tests run side by side, and must neither see each other's settings nor touch the user's.
    const QTemporaryDir settingsFolder;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsFolder.path());
    QCoreApplication::setOrganizationName(QStringLiteral("ImageViewerTests"));
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
