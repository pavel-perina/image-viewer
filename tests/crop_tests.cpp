#include <cmath>
#include <random>

#include <gtest/gtest.h>

#include "core/crop.h"

using namespace iv;

namespace {

constexpr Size kImage{1000, 800};

bool insideImage(const PixelRect& r, Size image) {
    return r.width >= 1 && r.height >= 1 && r.x >= 0 && r.y >= 0 && r.right() <= image.width && r.bottom() <= image.height;
}

// Within a pixel of the ratio, the most that whole pixels allow.
bool hasRatio(const PixelRect& r, AspectRatio aspect) {
    const double wanted = static_cast<double>(r.width) * aspect.height / aspect.width;
    return std::abs(r.height - wanted) <= 1.0 + 1e-9 ||
           std::abs(r.width - static_cast<double>(r.height) * aspect.width / aspect.height) <= 1.0 + 1e-9;
}

}  // namespace

namespace iv {
void PrintTo(const PixelRect& r, std::ostream* os) {
    *os << r.x << "," << r.y << " " << r.width << "x" << r.height;
}
}  // namespace iv

TEST(Crop, FindsHandlesWithinReachOfTheEdges) {
    const PixelRect rect{100, 100, 200, 100};
    EXPECT_EQ(cropHandleAt(rect, {100, 100}, 5), CropHandle::TopLeft);
    EXPECT_EQ(cropHandleAt(rect, {303, 198}, 5), CropHandle::BottomRight);
    EXPECT_EQ(cropHandleAt(rect, {200, 96}, 5), CropHandle::Top);
    EXPECT_EQ(cropHandleAt(rect, {97, 150}, 5), CropHandle::Left);
    EXPECT_EQ(cropHandleAt(rect, {200, 150}, 5), CropHandle::Inside);
    EXPECT_EQ(cropHandleAt(rect, {50, 150}, 5), CropHandle::None);
    // Beyond the end of an edge it is not that edge.
    EXPECT_EQ(cropHandleAt(rect, {97, 50}, 5), CropHandle::None);
}

TEST(Crop, PicksTheNearerEdgeOfASmallRectangle) {
    const PixelRect rect{100, 100, 4, 4};
    EXPECT_EQ(cropHandleAt(rect, {101, 102}, 1.5), CropHandle::Left);
    EXPECT_EQ(cropHandleAt(rect, {103.5, 102}, 1.5), CropHandle::Right);
}

TEST(Crop, APointOutsideFacesAnEdgeOrACorner) {
    const PixelRect rect{100, 100, 200, 100};
    EXPECT_EQ(cropHandleToward(rect, {-5, 150}), CropHandle::Left);
    EXPECT_EQ(cropHandleToward(rect, {-5, -5}), CropHandle::TopLeft);
    EXPECT_EQ(cropHandleToward(rect, {200, 900}), CropHandle::Bottom);
    EXPECT_EQ(cropHandleToward(rect, {1005, 250}), CropHandle::BottomRight);
    EXPECT_EQ(cropHandleToward(rect, {150, 150}), CropHandle::Inside);
}

TEST(Crop, EdgesLandOnTheNearestPixelBoundary) {
    const PixelRect rect{100, 100, 200, 100};
    EXPECT_EQ(dragCrop(rect, CropHandle::Right, {10.4, 50}, kImage, {}), (PixelRect{100, 100, 210, 100}));
    EXPECT_EQ(dragCrop(rect, CropHandle::Right, {10.6, 0}, kImage, {}), (PixelRect{100, 100, 211, 100}));
    EXPECT_EQ(dragCrop(rect, CropHandle::Top, {7, -20.2}, kImage, {}), (PixelRect{100, 80, 200, 120}));
}

TEST(Crop, StaysInsideTheImage) {
    const PixelRect rect{100, 100, 200, 100};
    EXPECT_EQ(dragCrop(rect, CropHandle::Right, {5000, 0}, kImage, {}), (PixelRect{100, 100, 900, 100}));
    EXPECT_EQ(dragCrop(rect, CropHandle::TopLeft, {-500, -500}, kImage, {}), (PixelRect{0, 0, 300, 200}));
    EXPECT_EQ(dragCrop(rect, CropHandle::Inside, {-500, 5000}, kImage, {}), (PixelRect{0, 700, 200, 100}));
}

TEST(Crop, DraggingPastTheOppositeEdgeTurnsItOver) {
    const PixelRect rect{100, 100, 200, 100};
    EXPECT_EQ(dragCrop(rect, CropHandle::Right, {-250, 0}, kImage, {}), (PixelRect{50, 100, 50, 100}));
    EXPECT_EQ(dragCrop(rect, CropHandle::BottomRight, {-250, -150}, kImage, {}), (PixelRect{50, 50, 50, 50}));
    // Meeting it leaves one pixel rather than none.
    EXPECT_EQ(dragCrop(rect, CropHandle::Right, {-200, 0}, kImage, {}), (PixelRect{100, 100, 1, 100}));
}

TEST(Crop, AnEdgeWithARatioKeepsTheRectangleCentredAcrossIt) {
    const PixelRect rect{100, 100, 300, 200};
    const PixelRect wider = dragCrop(rect, CropHandle::Right, {150, 0}, kImage, {3, 2});
    EXPECT_EQ(wider, (PixelRect{100, 50, 450, 300}));
    // The image is 800 tall, so the width stops where the height fills it.
    const PixelRect widest = dragCrop(rect, CropHandle::Right, {5000, 0}, kImage, {3, 2});
    EXPECT_EQ(widest.height, 600);
    EXPECT_EQ(widest.width, 900);
    EXPECT_TRUE(insideImage(widest, kImage));
}

TEST(Crop, ACornerWithARatioFollowsTheSideThePointerAsksMoreOf) {
    const PixelRect rect{100, 100, 300, 200};
    EXPECT_EQ(dragCrop(rect, CropHandle::BottomRight, {300, 10}, kImage, {3, 2}), (PixelRect{100, 100, 600, 400}));
    EXPECT_EQ(dragCrop(rect, CropHandle::BottomRight, {0, 100}, kImage, {3, 2}), (PixelRect{100, 100, 450, 300}));
}

TEST(Crop, DrawsANewRectangleInAnyDirection) {
    EXPECT_EQ(drawCrop({500, 400}, {300.4, 299.6}, kImage, {}), (PixelRect{300, 300, 200, 100}));
    EXPECT_EQ(drawCrop({500, 400}, {700, 450}, kImage, {1, 1}), (PixelRect{500, 400, 200, 200}));
    EXPECT_EQ(drawCrop({500, 400}, {2000, 2000}, kImage, {}), (PixelRect{500, 400, 500, 400}));
}

TEST(Crop, FitsARatioInsideTheRectangle) {
    EXPECT_EQ(fitAspect({0, 0, 1000, 800}, {16, 9}), (PixelRect{0, 118, 1000, 563}));
    EXPECT_EQ(fitAspect({0, 0, 1000, 800}, {1, 1}), (PixelRect{100, 0, 800, 800}));
    EXPECT_EQ(fitAspect({10, 20, 30, 40}, {}), (PixelRect{10, 20, 30, 40}));
}

TEST(Crop, TypedFieldsAreKeptInsideTheImage) {
    const PixelRect rect{700, 100, 200, 100};
    EXPECT_EQ(withPosition(rect, 900, -5, kImage), (PixelRect{800, 0, 200, 100}));
    EXPECT_EQ(withWidth(rect, 400, kImage, {}), (PixelRect{600, 100, 400, 100}));
    EXPECT_EQ(withWidth(rect, 400, kImage, {16, 9}), (PixelRect{600, 100, 400, 225}));
    EXPECT_EQ(withHeight(rect, 5000, kImage, {}), (PixelRect{700, 0, 200, 800}));
    EXPECT_EQ(withHeight(rect, 1080, kImage, {16, 9}), (PixelRect{0, 100, 1000, 563}));
}

TEST(Crop, RandomDragsKeepItsPromises) {
    std::mt19937 random(7);
    std::uniform_real_distribution<double> delta(-1200, 1200);
    std::uniform_int_distribution<int> handle(static_cast<int>(CropHandle::Inside), static_cast<int>(CropHandle::BottomRight));
    const AspectRatio ratios[] = {{}, {1, 1}, {3, 2}, {16, 9}, {210, 297}, {7, 1}};
    for (int i = 0; i < 20000; ++i) {
        const AspectRatio aspect = ratios[i % std::size(ratios)];
        const PixelRect start = drawCrop({delta(random) / 2 + 500, delta(random) / 2 + 400},
                                         {delta(random) / 2 + 500, delta(random) / 2 + 400}, kImage, aspect);
        ASSERT_TRUE(insideImage(start, kImage));
        const auto h = static_cast<CropHandle>(handle(random));
        const PixelRect moved = dragCrop(start, h, {delta(random), delta(random)}, kImage, aspect);
        SCOPED_TRACE(testing::Message() << "handle " << static_cast<int>(h) << " ratio " << aspect.width << ":" << aspect.height);
        ASSERT_TRUE(insideImage(moved, kImage)) << testing::PrintToString(start) << " -> " << testing::PrintToString(moved);
        if (h == CropHandle::Inside) {
            EXPECT_EQ(moved.width, start.width);
            EXPECT_EQ(moved.height, start.height);
        } else if (!aspect.isFree()) {
            EXPECT_TRUE(hasRatio(moved, aspect)) << testing::PrintToString(moved);
        }
        // A corner turns about the opposite one, which stays a corner.
        if (h == CropHandle::BottomRight) {
            EXPECT_TRUE(moved.x == start.x || moved.right() == start.x);
            EXPECT_TRUE(moved.y == start.y || moved.bottom() == start.y);
        }
        // Without a ratio, a side leaves the other direction alone.
        if (aspect.isFree() && (h == CropHandle::Left || h == CropHandle::Right)) {
            EXPECT_EQ(moved.y, start.y);
            EXPECT_EQ(moved.height, start.height);
        }
    }
}
