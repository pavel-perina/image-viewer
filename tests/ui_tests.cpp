#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QThread>

#include <functional>

#include "test_jpeg.h"
#include "ui/browse_context.h"
#include "ui/image_view.h"

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
