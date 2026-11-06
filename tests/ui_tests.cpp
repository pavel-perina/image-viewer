#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QQuickTextureFactory>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QThread>

#include <functional>
#include <memory>

#include "test_jpeg.h"
#include "ui/browse_context.h"
#include "ui/directory_tree_model.h"
#include "ui/image_view.h"
#include "ui/thumbnail_provider.h"

using iv::BrowseContext;
using iv::ImageView;
using iv::test::encodeJpeg;
using iv::test::JpegOptions;
using iv::test::Rgb;

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

TEST(ThumbnailProvider, DeliversThumbnailsForAnyFileName) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("100% #1 ?é.jpg"));
    JpegOptions options;
    options.width = 1000;
    options.height = 500;
    writeJpeg(path, options);

    const QString url = iv::ThumbnailProvider::urlForPath(path);
    const QString id = url.mid(url.lastIndexOf(QLatin1Char('/')) + 1);

    iv::ThumbnailProvider provider;
    std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(id, {}));
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
    const QString url = iv::ThumbnailProvider::urlForPath(QStringLiteral("/does/not/exist.jpg"));
    std::unique_ptr<QQuickImageResponse> response(
        provider.requestImageResponse(url.mid(url.lastIndexOf(QLatin1Char('/')) + 1), {}));
    bool finished = false;
    QObject::connect(response.get(), &QQuickImageResponse::finished, response.get(),
                     [&] { finished = true; });
    ASSERT_TRUE(waitUntil([&] { return finished; }));
    EXPECT_FALSE(response->errorString().isEmpty());
}

// Qt deletes a response only after finished(), so a cancelled one must still emit it.
TEST(ThumbnailProvider, CancelledResponsesFinishExactlyOnce) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("large.jpg"));
    JpegOptions options;
    options.width = 2000;
    options.height = 2000;
    writeJpeg(path, options);

    const QString url = iv::ThumbnailProvider::urlForPath(path);
    iv::ThumbnailProvider provider;
    std::unique_ptr<QQuickImageResponse> response(
        provider.requestImageResponse(url.mid(url.lastIndexOf(QLatin1Char('/')) + 1), {}));
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

    const QString url = iv::ThumbnailProvider::urlForPath(path);
    const QString id = url.mid(url.lastIndexOf(QLatin1Char('/')) + 1);
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

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
