#include <gtest/gtest.h>

#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <regex>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/exif.h"
#include "core/geometry.h"
#include "core/image_buffer.h"
#include "core/image_source.h"
#include "core/jpeg_source.h"
#include "core/libraries.h"
#include "core/overview_source.h"
#include "core/thumbnail.h"
#include "core/viewport.h"
#include "core/work_queue.h"
#include "test_jpeg.h"

using namespace iv;
using test::encodeJpeg;
using test::JpegOptions;
using test::Rgb;

namespace {

void expectNear(std::uint8_t actual, int expected) {
    EXPECT_NEAR(static_cast<int>(actual), expected, 6);
}

// B, G, R order, as PixelFormat::Bgrx8 stores it.
void expectPixel(const ImageBuffer& image, int x, int y, Rgb expected) {
    const auto* pixel = reinterpret_cast<const std::uint8_t*>(image.row(y)) + x * 4;
    expectNear(pixel[0], expected.b);
    expectNear(pixel[1], expected.g);
    expectNear(pixel[2], expected.r);
}

}  // namespace

TEST(ImageBuffer, ViewAliasesThePixelsOfItsParent) {
    const ImageBuffer image = ImageBuffer::create({8, 8}, PixelFormat::Bgrx8);
    const ImageBuffer part = image.view(2, 3, 4, 4);
    EXPECT_EQ(part.size(), (Size{4, 4}));
    EXPECT_EQ(part.stride(), image.stride());
    EXPECT_EQ(part.data(), image.data() + 3 * image.stride() + 2 * 4);
}

TEST(Orientation, SwapsSizeOnlyForQuarterTurns) {
    EXPECT_EQ(displaySize({4, 3}, Orientation::Normal), (Size{4, 3}));
    EXPECT_EQ(displaySize({4, 3}, Orientation::Rotate180), (Size{4, 3}));
    EXPECT_EQ(displaySize({4, 3}, Orientation::Rotate90CW), (Size{3, 4}));
    EXPECT_EQ(displaySize({4, 3}, Orientation::Rotate270CW), (Size{3, 4}));
}

struct CornerCase {
    Orientation orientation;
    double topLeftX, topLeftY;    // where the stored top-left corner is displayed
    double topRightX, topRightY;  // where the stored top-right corner is displayed
};

class OrientationCorners : public testing::TestWithParam<CornerCase> {};

TEST_P(OrientationCorners, MapsAccordingToExif) {
    const CornerCase c = GetParam();
    const Affine m = displayFromStored({4, 3}, c.orientation);
    double x, y;
    m.apply(0, 0, x, y);
    EXPECT_DOUBLE_EQ(x, c.topLeftX);
    EXPECT_DOUBLE_EQ(y, c.topLeftY);
    m.apply(4, 0, x, y);
    EXPECT_DOUBLE_EQ(x, c.topRightX);
    EXPECT_DOUBLE_EQ(y, c.topRightY);

    double backX, backY;
    m.inverse().apply(x, y, backX, backY);
    EXPECT_DOUBLE_EQ(backX, 4);
    EXPECT_DOUBLE_EQ(backY, 0);
}

INSTANTIATE_TEST_SUITE_P(AllEight, OrientationCorners, testing::Values(
    CornerCase{Orientation::Normal, 0, 0, 4, 0},
    CornerCase{Orientation::MirrorHorizontal, 4, 0, 0, 0},
    CornerCase{Orientation::Rotate180, 4, 3, 0, 3},
    CornerCase{Orientation::MirrorVertical, 0, 3, 4, 3},
    CornerCase{Orientation::Transpose, 0, 0, 0, 4},
    CornerCase{Orientation::Rotate90CW, 3, 0, 3, 4},
    CornerCase{Orientation::Transverse, 3, 4, 3, 0},
    CornerCase{Orientation::Rotate270CW, 0, 4, 0, 0}),
    [](const testing::TestParamInfo<CornerCase>& info) {
        return "Exif" + std::to_string(static_cast<int>(info.param.orientation));
    });

TEST(Exif, RejectsMalformedData) {
    const std::vector<std::uint8_t> notExif{'J', 'F', 'I', 'F', 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_EQ(parseExif(notExif).orientation, Orientation::Normal);
    EXPECT_EQ(parseExif({}).orientation, Orientation::Normal);

    // IFD offset beyond the end of the data.
    const std::vector<std::uint8_t> badOffset{'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 42, 0, 0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_EQ(parseExif(badOffset).orientation, Orientation::Normal);

    // Claims many entries but ends immediately.
    const std::vector<std::uint8_t> truncated{'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 42, 0, 8, 0, 0, 0, 0xFF, 0xFF};
    EXPECT_EQ(parseExif(truncated).orientation, Orientation::Normal);

    // Orientation value outside 1..8.
    const std::vector<std::uint8_t> badValue{'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 42, 0, 8, 0, 0, 0,
                                             1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, 9, 0, 0, 0};
    EXPECT_EQ(parseExif(badValue).orientation, Orientation::Normal);
}

TEST(Exif, ReadsBigEndian) {
    const std::vector<std::uint8_t> data{'E', 'x', 'i', 'f', 0, 0, 'M', 'M', 0, 42, 0, 0, 0, 8,
                                         0, 1, 0x01, 0x12, 0, 3, 0, 0, 0, 1, 0, 6, 0, 0};
    EXPECT_EQ(parseExif(data).orientation, Orientation::Rotate90CW);
}

namespace {

Viewport viewAt(double zoom, Size image = {1000, 1000}) {
    Viewport vp;
    vp.setImageSize(image);
    vp.setViewSize(400, 300);
    vp.setFitMode(FitMode::Manual);
    vp.zoomBy(1 / vp.zoom(), 200, 150);  // from wherever fitting left it, to 100%
    vp.zoomBy(zoom, 200, 150);
    return vp;
}

}  // namespace

TEST(Resample, OnlyWhereItChangesWhatIsShown) {
    EXPECT_FALSE(resampleGeometry(viewAt(1.0), {1000, 1000}, 1.0, 2.5).active);    // as they are
    EXPECT_FALSE(resampleGeometry(viewAt(0.5), {1000, 1000}, 0.5, 2.5).active);    // on a level
    EXPECT_FALSE(resampleGeometry(viewAt(2.0), {1000, 1000}, 1.0, 2.5).active);    // nearest
    EXPECT_FALSE(resampleGeometry(viewAt(3.0), {1000, 1000}, 1.0, 2.5).active);
    EXPECT_TRUE(resampleGeometry(viewAt(0.7), {1000, 1000}, 1.0, 2.5).active);
    EXPECT_TRUE(resampleGeometry(viewAt(1.5), {1000, 1000}, 1.0, 2.5).active);
    EXPECT_FALSE(resampleGeometry(Viewport{}, {1000, 1000}, 1.0, 2.5).active);
}

TEST(Resample, KernelStretchesOnlyWhenMinifying) {
    const auto down = resampleGeometry(viewAt(0.5), {1000, 1000}, 1.0, 2.5);
    EXPECT_DOUBLE_EQ(down.ratio, 2.0);
    EXPECT_DOUBLE_EQ(down.spacing, 2.0);
    const auto up = resampleGeometry(viewAt(1.5), {1000, 1000}, 1.0, 2.5);
    EXPECT_NEAR(up.ratio, 1 / 1.5, 1e-9);
    EXPECT_DOUBLE_EQ(up.spacing, 1.0);
}

TEST(Resample, TextureAlignsWithTheLevelAndCoversTheKernel) {
    Viewport vp = viewAt(0.7);
    vp.panBy(13.37, -4.21);
    const auto g = resampleGeometry(vp, {1000, 1000}, 1.0, 2.5);
    ASSERT_TRUE(g.active);
    // Pixels of the level land exactly on texels.
    EXPECT_DOUBLE_EQ(g.translateX, std::round(g.translateX));
    EXPECT_DOUBLE_EQ(g.translateY, std::round(g.translateY));
    // The view's corner maps to the same image point both ways.
    EXPECT_NEAR((g.phaseX - g.translateX) / g.scale, -vp.offsetX() / vp.zoom(), 1e-9);
    EXPECT_NEAR((g.phaseY - g.translateY) / g.scale, -vp.offsetY() / vp.zoom(), 1e-9);
    // The kernel at either edge of the view stays inside the texture.
    const double reach = 2.5 * g.spacing;
    EXPECT_GE(g.phaseX - reach, 0);
    EXPECT_GE(g.phaseY - reach, 0);
    EXPECT_LE(g.phaseX + vp.viewWidth() * g.ratio + reach, g.textureWidth);
    EXPECT_LE(g.phaseY + vp.viewHeight() * g.ratio + reach, g.textureHeight);
}

TEST(Resample, RefusesTexturesTooLargeForTheGpu) {
    // A hundredth of the zoom shown from full size, as a file with no levels would be.
    Viewport vp;
    vp.setImageSize({40000, 40000});
    vp.setViewSize(1000, 1000);
    EXPECT_FALSE(resampleGeometry(vp, {40000, 40000}, 1.0, 2.5).active);
    EXPECT_TRUE(resampleGeometry(vp, {40000, 40000}, 0.05, 2.5).active);
}

TEST(Viewport, ShrinkToFitDoesNotEnlarge) {
    Viewport vp;
    vp.setViewSize(1000, 1000);
    vp.setImageSize({100, 50});
    EXPECT_DOUBLE_EQ(vp.zoom(), 1.0);
    EXPECT_DOUBLE_EQ(vp.offsetX(), 450);
    EXPECT_DOUBLE_EQ(vp.offsetY(), 475);

    vp.setFitMode(FitMode::Fit);
    EXPECT_DOUBLE_EQ(vp.zoom(), 10.0);

    vp.setFitMode(FitMode::ShrinkToFit);
    vp.setImageSize({2000, 1000});
    EXPECT_DOUBLE_EQ(vp.zoom(), 0.5);
}

TEST(Viewport, ResizingKeepsFittingUntilTheUserZooms) {
    Viewport vp;
    vp.setImageSize({1000, 1000});
    vp.setViewSize(500, 500);
    EXPECT_DOUBLE_EQ(vp.zoom(), 0.5);
    vp.setViewSize(250, 250);
    EXPECT_DOUBLE_EQ(vp.zoom(), 0.25);

    vp.zoomBy(2.0, 125, 125);
    EXPECT_EQ(vp.fitMode(), FitMode::Manual);
    EXPECT_DOUBLE_EQ(vp.zoom(), 0.5);
    vp.setViewSize(100, 100);
    EXPECT_DOUBLE_EQ(vp.zoom(), 0.5);
}

TEST(Viewport, ReturnsToAStateItSaved) {
    Viewport vp;
    vp.setImageSize({2000, 1000});
    vp.setViewSize(500, 500);
    EXPECT_TRUE(vp.showsWholeImage());
    const Viewport::State fitted = vp.state();

    vp.setFitMode(FitMode::Manual);
    vp.zoomBy(1 / vp.zoom(), 100, 250);
    vp.panBy(-300, 0);
    EXPECT_DOUBLE_EQ(vp.zoom(), 1);
    EXPECT_FALSE(vp.showsWholeImage());
    const Viewport::State zoomed = vp.state();

    vp.restore(fitted);
    EXPECT_EQ(vp.fitMode(), FitMode::ShrinkToFit);
    EXPECT_DOUBLE_EQ(vp.zoom(), 0.25);
    EXPECT_TRUE(vp.showsWholeImage());

    // A fit mode fits the view as it is now, rather than as it was.
    vp.setFitMode(FitMode::Manual);
    vp.setViewSize(250, 250);
    vp.restore(fitted);
    EXPECT_DOUBLE_EQ(vp.zoom(), 0.125);

    vp.setViewSize(500, 500);
    vp.restore(zoomed);
    EXPECT_EQ(vp.fitMode(), FitMode::Manual);
    EXPECT_DOUBLE_EQ(vp.offsetX(), zoomed.offsetX);
}

TEST(Viewport, ZoomKeepsThePointUnderTheCursorInPlace) {
    Viewport vp;
    vp.setViewSize(100, 100);
    vp.setImageSize({1000, 1000});
    vp.zoomBy(10.0, 0, 0);  // 100%, showing the top-left corner
    ASSERT_DOUBLE_EQ(vp.zoom(), 1.0);
    vp.panBy(-300, -300);
    ASSERT_DOUBLE_EQ(vp.offsetX(), -300);

    // View (50,50) shows image (350,350); it must still do so at 200%.
    vp.zoomBy(2.0, 50, 50);
    EXPECT_DOUBLE_EQ(vp.zoom(), 2.0);
    EXPECT_DOUBLE_EQ((50 - vp.offsetX()) / vp.zoom(), 350);
    EXPECT_DOUBLE_EQ((50 - vp.offsetY()) / vp.zoom(), 350);
}

TEST(Viewport, VisibleFractionIsThePartOfTheImageInView) {
    Viewport vp;
    EXPECT_EQ(vp.visibleFraction(), (RectD{0, 0, 1, 1})) << "no image";

    vp.setViewSize(1000, 500);
    vp.setImageSize({4000, 2000});
    EXPECT_DOUBLE_EQ(vp.visibleFraction().width, 1.0) << "fitted, all of it shows";

    vp.zoomBy(10.0, 0, 0);  // 100%, stopping there
    ASSERT_DOUBLE_EQ(vp.zoom(), 1.0);
    vp.panBy(-1000, -500);
    // Pixels 1000-2000 of 4000 across, and 500-1000 of 2000 down.
    EXPECT_EQ(vp.visibleFraction(), (RectD{0.25, 0.25, 0.25, 0.25}));
}

TEST(Viewport, BilinearLevelNeverReducesByMuch) {
    const std::vector<LevelInfo> levels{{{4000, 2000}, {2048, 2048}},
                                        {{2000, 1000}, {2048, 2048}},
                                        {{1000, 500}, {2048, 2048}}};
    EXPECT_EQ(chooseBilinearLevel(levels, 2.0), 0);
    EXPECT_EQ(chooseBilinearLevel(levels, 1.0), 0);
    EXPECT_EQ(chooseBilinearLevel(levels, 0.64), 0) << "reduced by 1.56";
    EXPECT_EQ(chooseBilinearLevel(levels, 0.6), 1) << "enlarged by 1.2 rather than reduced by 1.67";
    EXPECT_EQ(chooseBilinearLevel(levels, 0.5), 1) << "exactly the level";
    EXPECT_EQ(chooseBilinearLevel(levels, 0.26), 2);
    EXPECT_EQ(chooseBilinearLevel(levels, 0.1), 2) << "nothing smaller exists";
}

TEST(Viewport, ZoomStopsAtOneHundredPercentWhenCrossingIt) {
    Viewport vp;
    vp.setViewSize(100, 100);
    vp.setImageSize({1000, 1000});
    ASSERT_DOUBLE_EQ(vp.zoom(), 0.1);
    vp.zoomBy(100.0, 50, 50);
    EXPECT_DOUBLE_EQ(vp.zoom(), 1.0);
    vp.zoomBy(2.0, 50, 50);
    EXPECT_DOUBLE_EQ(vp.zoom(), 2.0);
    vp.zoomBy(0.01, 50, 50);
    EXPECT_DOUBLE_EQ(vp.zoom(), 1.0);
}

TEST(Viewport, PanCannotLeaveTheImageAndCentresSmallImages) {
    Viewport vp;
    vp.setViewSize(100, 100);
    vp.setImageSize({1000, 50});
    vp.zoomBy(10.0, 0, 0);
    ASSERT_DOUBLE_EQ(vp.zoom(), 1.0);

    vp.panBy(500, 500);
    EXPECT_DOUBLE_EQ(vp.offsetX(), 0);
    EXPECT_DOUBLE_EQ(vp.offsetY(), 25);  // shorter than the view, so it stays centred

    vp.panBy(-5000, -5000);
    EXPECT_DOUBLE_EQ(vp.offsetX(), -900);
    EXPECT_DOUBLE_EQ(vp.offsetY(), 25);
}

TEST(Viewport, VisibleRectIsInImageCoordinates) {
    Viewport vp;
    vp.setViewSize(100, 100);
    vp.setImageSize({1000, 1000});
    vp.zoomBy(10.0, 0, 0);
    vp.panBy(-200, -300);
    EXPECT_EQ(vp.visibleRect(), (RectD{200, 300, 100, 100}));
}

TEST(Tiles, ChooseLevelNeverPicksTooFewPixels) {
    const std::vector<LevelInfo> levels{{{8000, 6000}, {2048, 2048}},
                                        {{4000, 3000}, {2048, 2048}},
                                        {{2000, 1500}, {2048, 2048}},
                                        {{1000, 750}, {2048, 2048}}};
    EXPECT_EQ(chooseLevel(levels, 4.0), 0);
    EXPECT_EQ(chooseLevel(levels, 1.0), 0);
    EXPECT_EQ(chooseLevel(levels, 0.75), 0);
    EXPECT_EQ(chooseLevel(levels, 0.5), 1);
    EXPECT_EQ(chooseLevel(levels, 0.3), 1);
    EXPECT_EQ(chooseLevel(levels, 0.25), 2);
    EXPECT_EQ(chooseLevel(levels, 0.125), 3);
    EXPECT_EQ(chooseLevel(levels, 0.01), 3);
}

TEST(Tiles, RangeCoversTheRectAndStopsOnBoundaries) {
    const LevelInfo level{{4000, 3000}, {1000, 1000}};
    const Size full{8000, 6000};

    TileRange r = tilesInRect(level, full, {0, 0, 8000, 6000});
    EXPECT_EQ(r.firstCol, 0);
    EXPECT_EQ(r.lastCol, 3);
    EXPECT_EQ(r.firstRow, 0);
    EXPECT_EQ(r.lastRow, 2);

    // Ends exactly on the boundary between tiles 0 and 1.
    r = tilesInRect(level, full, {0, 0, 2000, 2000});
    EXPECT_EQ(r.lastCol, 0);
    EXPECT_EQ(r.lastRow, 0);

    r = tilesInRect(level, full, {2500, 2500, 100, 100});
    EXPECT_EQ(r.firstCol, 1);
    EXPECT_EQ(r.lastCol, 1);

    r = tilesInRect(level, full, {0, 0, 0, 0});
    EXPECT_GT(r.firstCol, r.lastCol);
}

TEST(Tiles, RectsTileTheWholeImage) {
    const LevelInfo level{{2500, 1000}, {1000, 1000}};
    const Size full{5000, 2000};
    EXPECT_EQ(tileRect(level, full, 0, 0), (RectD{0, 0, 2000, 2000}));
    EXPECT_EQ(tileRect(level, full, 2, 0), (RectD{4000, 0, 1000, 2000}));
}

TEST(WorkQueue, RunsHighestPriorityFirstAndEqualPrioritiesInOrder) {
    WorkQueue queue(1);
    const auto group = queue.createGroup();

    std::promise<void> release;
    auto released = release.get_future().share();
    std::promise<void> started;
    queue.submit(group, 0, [&, released](std::stop_token) {
        started.set_value();
        released.wait();
    });
    // Until the only worker is busy it could pick up the tasks below as they arrive.
    started.get_future().wait();

    std::mutex mutex;
    std::vector<int> order;
    const auto record = [&](int value) {
        return [&, value](std::stop_token) {
            const std::scoped_lock lock(mutex);
            order.push_back(value);
        };
    };
    queue.submit(group, 1, record(1));
    queue.submit(group, 5, record(2));
    queue.submit(group, 5, record(3));
    queue.submit(group, 9, record(4));

    release.set_value();
    queue.waitGroup(group);
    EXPECT_EQ(order, (std::vector<int>{4, 2, 3, 1}));
}

TEST(WorkQueue, CancelRemovesQueuedTasksOnly) {
    WorkQueue queue(1);
    const auto group = queue.createGroup();

    std::promise<void> release;
    auto released = release.get_future().share();
    std::promise<void> started;
    std::atomic<bool> stopSeen = false;
    const auto running = queue.submit(group, 10, [&, released](std::stop_token stop) {
        started.set_value();
        released.wait();
        stopSeen = stop.stop_requested();
    });
    started.get_future().wait();

    std::atomic<int> ran = 0;
    const auto cancelled = queue.submit(group, 0, [&](std::stop_token) { ++ran; });
    queue.submit(group, 0, [&](std::stop_token) { ran += 10; });

    queue.cancel(cancelled);
    queue.cancel(running);
    release.set_value();
    queue.waitGroup(group);

    EXPECT_EQ(ran, 10);
    EXPECT_TRUE(stopSeen);
}

TEST(WorkQueue, CancelGroupLeavesOtherGroupsAlone) {
    WorkQueue queue(1);
    const auto mine = queue.createGroup();
    const auto other = queue.createGroup();

    std::promise<void> release;
    auto released = release.get_future().share();
    queue.submit(other, 10, [released](std::stop_token) { released.wait(); });

    std::atomic<int> ran = 0;
    queue.submit(mine, 0, [&](std::stop_token) { ran += 1; });
    queue.submit(other, 0, [&](std::stop_token) { ran += 10; });
    queue.cancelGroup(mine);
    queue.waitGroup(mine);  // must not block: only "other" still has work

    release.set_value();
    queue.waitGroup(other);
    EXPECT_EQ(ran, 10);
}

TEST(WorkQueue, DestructorDropsQueuedTasks) {
    std::atomic<int> ran = 0;
    {
        WorkQueue queue(1);
        const auto group = queue.createGroup();
        std::promise<void> started;
        queue.submit(group, 10, [&](std::stop_token stop) {
            started.set_value();
            while (!stop.stop_requested()) {
                std::this_thread::yield();
            }
        });
        started.get_future().wait();
        queue.submit(group, 0, [&](std::stop_token) { ++ran; });
    }
    EXPECT_EQ(ran, 0);
}

namespace {

std::shared_ptr<ImageSource> openBytes(std::vector<std::uint8_t> bytes) {
    auto source = openJpegSource(std::move(bytes));
    EXPECT_TRUE(source.has_value()) << (source ? "" : source.error());
    return source ? *source : nullptr;
}

}  // namespace

TEST(Jpeg, ExposesOnlyTheFullSizeAndLeavesTheLevelsToHalving) {
    JpegOptions options;
    options.width = 4000;
    options.height = 3000;
    const auto source = openBytes(encodeJpeg(options));
    ASSERT_TRUE(source);

    ASSERT_EQ(source->subimages().size(), 1u);
    const SubimageInfo& info = source->subimages()[0];
    EXPECT_EQ(info.role, SubimageRole::Page);
    EXPECT_EQ(info.format, PixelFormat::Bgrx8);
    ASSERT_EQ(info.levels.size(), 1u);
    EXPECT_EQ(info.levels[0].size, (Size{4000, 3000}));

    // The reduced levels come from halving, the same as for other formats.
    const auto withLevels = withOverviews(source);
    const auto& levels = withLevels->subimages()[0].levels;
    ASSERT_EQ(levels.size(), 3u);
    EXPECT_EQ(levels[1].size, (Size{2000, 1500}));
    EXPECT_EQ(levels[2].size, (Size{1000, 750}));
    EXPECT_TRUE(levels[2].generated);
}

TEST(Jpeg, GivesARoughLevelOfAnySizeTheLevelsHave) {
    JpegOptions options;
    options.width = 1001;
    options.height = 603;
    options.pixel = [](int, int) { return Rgb{220, 40, 90}; };
    const auto source = openBytes(encodeJpeg(options));
    ASSERT_TRUE(source);

    // Halving rounds up, so these are the sizes of the levels 1/2 to 1/32: scaled decoding
    // gives the first three, and the 1/8 one halved the rest.
    for (const Size size : {Size{501, 302}, Size{251, 151}, Size{126, 76}, Size{63, 38}, Size{32, 19}}) {
        const auto rough = source->roughLevel(0, size, {});
        ASSERT_TRUE(rough.has_value()) << size.width << ": " << rough.error();
        EXPECT_EQ(rough->size(), size);
        expectPixel(*rough, 0, 0, {220, 40, 90});
    }
    EXPECT_FALSE(source->roughLevel(0, {300, 300}, {}).has_value());
    EXPECT_FALSE(source->roughLevel(1, {501, 302}, {}).has_value());
}

TEST(Jpeg, TinyImagesHaveNoDuplicateLevels) {
    JpegOptions options;
    options.width = 1;
    options.height = 1;
    const auto source = openBytes(encodeJpeg(options));
    ASSERT_TRUE(source);
    EXPECT_EQ(source->subimages()[0].levels.size(), 1u);
}

TEST(Jpeg, DecodesColoursInTheRightChannels) {
    JpegOptions options;
    options.pixel = [](int, int) { return Rgb{220, 40, 90}; };
    const auto source = openBytes(encodeJpeg(options));
    ASSERT_TRUE(source);

    const auto tile = source->readTile({0, 0, 0, 0}, {});
    ASSERT_TRUE(tile.has_value()) << tile.error();
    expectPixel(*tile, 0, 0, {220, 40, 90});
    EXPECT_EQ(static_cast<std::uint8_t>(tile->data()[3]), 255);

    const auto rough = source->roughLevel(0, {32, 32}, {});
    ASSERT_TRUE(rough.has_value()) << rough.error();
    expectPixel(*rough, 0, 0, {220, 40, 90});
}

TEST(Jpeg, SplitsLargeLevelsIntoTilesAtTheRightPositions) {
    JpegOptions options;
    options.width = 3000;
    options.height = 2500;
    // Left and right of the tile boundary at x=2048 are different colours.
    options.pixel = [](int x, int) { return x < 2048 ? Rgb{200, 30, 30} : Rgb{30, 30, 200}; };
    const auto source = openBytes(encodeJpeg(options));
    ASSERT_TRUE(source);

    const auto left = source->readTile({0, 0, 0, 0}, {});
    const auto right = source->readTile({0, 0, 1, 0}, {});
    const auto bottomRight = source->readTile({0, 0, 1, 1}, {});
    ASSERT_TRUE(left && right && bottomRight);

    EXPECT_EQ(left->size(), (Size{2048, 2048}));
    EXPECT_EQ(right->size(), (Size{952, 2048}));
    EXPECT_EQ(bottomRight->size(), (Size{952, 452}));
    expectPixel(*left, 100, 100, {200, 30, 30});
    expectPixel(*right, 100, 100, {30, 30, 200});
}

TEST(Jpeg, RejectsTilesThatDoNotExist) {
    const auto source = openBytes(encodeJpeg({}));
    ASSERT_TRUE(source);
    EXPECT_FALSE(source->readTile({1, 0, 0, 0}, {}));
    EXPECT_FALSE(source->readTile({0, 99, 0, 0}, {}));
    EXPECT_FALSE(source->readTile({0, 0, 1, 0}, {}));
    EXPECT_FALSE(source->readTile({0, 0, -1, 0}, {}));
}

TEST(Jpeg, ReadsOrientationAndIccProfile) {
    JpegOptions options;
    options.orientation = 6;
    options.iccProfile = {1, 2, 3, 4, 5};
    const auto source = openBytes(encodeJpeg(options));
    ASSERT_TRUE(source);
    EXPECT_EQ(source->subimages()[0].orientation, Orientation::Rotate90CW);
    EXPECT_EQ(source->subimages()[0].iccProfile, options.iccProfile);
}

TEST(Jpeg, StopTokenAbortsDecodingAndALaterReadStillWorks) {
    JpegOptions options;
    options.width = 256;
    options.height = 256;
    const auto source = openBytes(encodeJpeg(options));
    ASSERT_TRUE(source);

    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = source->readTile({0, 0, 0, 0}, stop.get_token());
    ASSERT_FALSE(cancelled.has_value());
    EXPECT_EQ(cancelled.error(), "cancelled");

    EXPECT_TRUE(source->readTile({0, 0, 0, 0}, {}).has_value());
}

TEST(Jpeg, ConcurrentReadsAcrossLevelsAndTilesAreCorrect) {
    JpegOptions options;
    options.width = 2600;
    options.height = 300;
    // Colour changes exactly on the tile boundary of level 0.
    options.pixel = [](int x, int) { return x < 2048 ? Rgb{200, 30, 30} : Rgb{30, 30, 200}; };
    const auto source = withOverviews(openBytes(encodeJpeg(options)));
    ASSERT_TRUE(source);

    std::atomic<int> wrong = 0;
    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&, t] {
                for (int i = 0; i < 6; ++i) {
                    const int level = (t + i) % 2;
                    const int col = level == 0 ? (t + i / 2) % 2 : 0;
                    const auto tile = source->readTile({0, level, col, 0}, {});
                    if (!tile) {
                        ++wrong;
                        continue;
                    }
                    const auto* pixel = reinterpret_cast<const std::uint8_t*>(tile->row(10));
                    const bool isBlue = pixel[4 * 10] > pixel[4 * 10 + 2];  // B vs R, away from the edge
                    if (isBlue != (level == 0 && col == 1)) {
                        ++wrong;
                    }
                }
            });
        }
    }
    EXPECT_EQ(wrong, 0);
}

TEST(Jpeg, ReportsErrorsForDamagedFiles) {
    std::vector<std::uint8_t> bytes = encodeJpeg({});

    // Header cut short: there is no image to show.
    const std::vector<std::uint8_t> headerOnly(bytes.begin(), bytes.begin() + 10);
    EXPECT_FALSE(openJpegSource(headerOnly).has_value());

    EXPECT_FALSE(openJpegSource(std::vector<std::uint8_t>{0xFF, 0xD8, 0xFF}).has_value());
}

TEST(Jpeg, ToleratesTruncatedImageData) {
    JpegOptions options;
    options.width = 256;
    options.height = 256;
    std::vector<std::uint8_t> bytes = encodeJpeg(options);
    bytes.resize(bytes.size() * 3 / 4);

    const auto source = openBytes(std::move(bytes));
    ASSERT_TRUE(source);
    EXPECT_TRUE(source->readTile({0, 0, 0, 0}, {}).has_value());
}

namespace {

// Writes a JPEG to a temporary file for as long as the object lives.
class TempJpeg {
public:
    explicit TempJpeg(const JpegOptions& options)
        : m_path(std::filesystem::temp_directory_path() /
                 ("iv_thumb_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".jpg")) {
        test::writeFile(m_path, encodeJpeg(options));
    }
    ~TempJpeg() { std::filesystem::remove(m_path); }
    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

}  // namespace

TEST(Thumbnail, FitsTheLongerEdgeAndKeepsAspectRatio) {
    JpegOptions options;
    options.width = 2000;
    options.height = 1000;
    const TempJpeg file(options);
    const auto thumbnail = makeThumbnail(file.path(), 256, {});
    ASSERT_TRUE(thumbnail.has_value()) << thumbnail.error();
    EXPECT_EQ(thumbnail->size(), (Size{256, 128}));
}

TEST(Thumbnail, NeverEnlarges) {
    JpegOptions options;
    options.width = 40;
    options.height = 20;
    const TempJpeg file(options);
    const auto thumbnail = makeThumbnail(file.path(), 256, {});
    ASSERT_TRUE(thumbnail.has_value()) << thumbnail.error();
    EXPECT_EQ(thumbnail->size(), (Size{40, 20}));
}

TEST(Thumbnail, KeepsAVisibleDimensionForExtremeAspectRatios) {
    JpegOptions options;
    options.width = 4000;
    options.height = 8;
    const TempJpeg file(options);
    const auto thumbnail = makeThumbnail(file.path(), 256, {});
    ASSERT_TRUE(thumbnail.has_value()) << thumbnail.error();
    EXPECT_EQ(thumbnail->width(), 256);
    EXPECT_GE(thumbnail->height(), 1);
}

TEST(Thumbnail, AppliesOrientationToThePixels) {
    JpegOptions options;
    options.width = 800;
    options.height = 400;
    options.orientation = 6;  // rotate 90 degrees clockwise to display
    options.pixel = [](int x, int) { return x < 400 ? Rgb{220, 30, 30} : Rgb{30, 30, 220}; };
    const TempJpeg file(options);

    const auto thumbnail = makeThumbnail(file.path(), 200, {});
    ASSERT_TRUE(thumbnail.has_value()) << thumbnail.error();
    EXPECT_EQ(thumbnail->size(), (Size{100, 200}));
    expectPixel(*thumbnail, 50, 20, {220, 30, 30});   // stored left half is now on top
    expectPixel(*thumbnail, 50, 180, {30, 30, 220});
}

TEST(Thumbnail, AssemblesLevelsThatSpanSeveralTiles) {
    JpegOptions options;
    options.width = 5000;
    options.height = 100;
    options.pixel = [](int x, int) { return x < 2500 ? Rgb{220, 30, 30} : Rgb{30, 30, 220}; };
    const TempJpeg file(options);

    // Needs full resolution, which is wider than one tile.
    const auto thumbnail = makeThumbnail(file.path(), 4000, {});
    ASSERT_TRUE(thumbnail.has_value()) << thumbnail.error();
    EXPECT_EQ(thumbnail->width(), 4000);
    expectPixel(*thumbnail, 500, 40, {220, 30, 30});
    expectPixel(*thumbnail, 3500, 40, {30, 30, 220});
}

TEST(Thumbnail, ReportsErrorsAndCancellation) {
    EXPECT_FALSE(makeThumbnail("/does/not/exist.jpg", 256, {}).has_value());

    const TempJpeg file(JpegOptions{});
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = makeThumbnail(file.path(), 256, stop.get_token());
    ASSERT_FALSE(cancelled.has_value());
    EXPECT_EQ(cancelled.error(), "cancelled");
}

TEST(ImageSource, DetectsFormatFromContentsNotExtension) {
    const auto dir = std::filesystem::temp_directory_path() / "iv_core_tests";
    std::filesystem::create_directories(dir);

    const auto disguised = dir / "photo.txt";
    test::writeFile(disguised, encodeJpeg({}));
    EXPECT_TRUE(openImageSource(disguised).has_value());

    const auto notImage = dir / "fake.jpg";
    test::writeFile(notImage, {'h', 'e', 'l', 'l', 'o'});
    const auto rejected = openImageSource(notImage);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error(), "unsupported image format");

    EXPECT_FALSE(openImageSource(dir / "does-not-exist.jpg").has_value());
    std::filesystem::remove_all(dir);
}

TEST(Libraries, NameTheFormatsOfThisBuildWithTheirVersions) {
    const std::vector<iv::Library> list = iv::libraries();
    const auto has = [&](std::string_view name) {
        return std::ranges::any_of(list, [&](const iv::Library& library) { return library.name == name; });
    };
    for (const iv::Library& library : list) {
        SCOPED_TRACE(library.name);
        EXPECT_FALSE(library.licence.empty());
        EXPECT_TRUE(library.url.starts_with("https://"));
        // A number with at least one dot, and nothing after it such as a copyright line.
        EXPECT_TRUE(std::regex_match(library.version, std::regex(R"(\d+(\.\d+)+)"))) << library.version;
    }
    EXPECT_TRUE(has("LibTIFF"));
    EXPECT_TRUE(has("Little CMS"));
    EXPECT_TRUE(has("SQLite"));
#ifdef IV_HAVE_PNG
    EXPECT_TRUE(has("libpng"));
#endif
#ifdef IV_HAVE_WEBP
    EXPECT_TRUE(has("libwebp"));
#else
    EXPECT_FALSE(has("libwebp"));
#endif
#ifdef IV_HAVE_AVIF
    EXPECT_TRUE(has("libavif"));
#endif
#ifdef IV_HAVE_JXL
    EXPECT_TRUE(has("libjxl"));
#endif
}
