#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>

#include <gtest/gtest.h>

#include "core/image_ops.h"
#include "core/perspective.h"

using namespace iv;

namespace {

constexpr Size kImage{1200, 800};
const double kFocal = focalPixels(kImage, 28);

// The lines that come out vertical and horizontal when the picture is turned by a tilt, as they
// were in the picture.
std::vector<GuideLine> linesStraightAfter(const Tilt& tilt) {
    const Homography back = tiltTransform(kImage, kFocal, tilt).inverse();
    const auto line = [&](PointD a, PointD b) { return GuideLine{back.apply(a), back.apply(b)}; };
    return {line({-300, -250}, {-300, 250}), line({350, -200}, {350, 280}),
            line({-400, -300}, {300, -300}), line({-350, 260}, {420, 260})};
}

// White with a black rectangle turned by an angle around the middle.
ImageBuffer turnedRectangle(double degrees) {
    const ImageBuffer image = ImageBuffer::create({600, 400}, PixelFormat::Bgrx8);
    const double a = degrees * std::numbers::pi / 180;
    for (int y = 0; y < 400; ++y) {
        auto* p = reinterpret_cast<std::uint8_t*>(image.row(y));
        for (int x = 0; x < 600; ++x, p += 4) {
            const double dx = x + 0.5 - 300;
            const double dy = y + 0.5 - 200;
            const double u = dx * std::cos(a) + dy * std::sin(a);
            const double v = -dx * std::sin(a) + dy * std::cos(a);
            const std::uint8_t value = std::abs(u) < 200 && std::abs(v) < 120 ? 20 : 235;
            p[0] = p[1] = p[2] = value;
            p[3] = 255;
        }
    }
    return image;
}

}  // namespace

TEST(Perspective, AHomographyAndItsInverseCancel) {
    const Homography h = tiltTransform(kImage, kFocal, {3, -7, 4});
    const PointD p = h.inverse().apply(h.apply({123, 456}));
    EXPECT_NEAR(p.x, 123, 1e-9);
    EXPECT_NEAR(p.y, 456, 1e-9);
}

TEST(Perspective, WithoutTiltOnlyTheOriginMoves) {
    const Homography h = tiltTransform(kImage, kFocal, {});
    const PointD p = h.apply({100, 50});
    EXPECT_NEAR(p.x, -500, 1e-9);
    EXPECT_NEAR(p.y, -350, 1e-9);
    const TiltPlan plan = planTilt(kImage, kFocal, {}, true);
    EXPECT_EQ(plan.size, kImage);
}

TEST(Perspective, ALineGoingDownToTheRightIsLevelledByTurningAnticlockwise) {
    const double rise = 400 * std::tan(3 * std::numbers::pi / 180);
    const GuideLine line{{400, 400}, {800, 400 + rise}};
    const TiltFit fit = fitTilt(std::span(&line, 1), kImage, kFocal, TiltMode::Full, false);
    EXPECT_NEAR(fit.tilt.roll, -3, 1e-3);
    EXPECT_EQ(fit.tilt.pitch, 0) << "one line sets the roll only";
    EXPECT_EQ(fit.tilt.yaw, 0);
}

TEST(Perspective, TwoVerticalsAndTwoHorizontalsGiveAllThreeAngles) {
    const Tilt truth{2, 5, -3};
    const auto lines = linesStraightAfter(truth);
    const TiltFit fit = fitTilt(lines, kImage, kFocal, TiltMode::Full, false);
    EXPECT_NEAR(fit.tilt.roll, truth.roll, 1e-3);
    EXPECT_NEAR(fit.tilt.pitch, truth.pitch, 1e-3);
    EXPECT_NEAR(fit.tilt.yaw, truth.yaw, 1e-3);

    // Verticals only: no yaw, whatever the horizontals say.
    const TiltFit vertical = fitTilt(lines, kImage, kFocal, TiltMode::RotationAndVerticals, false);
    EXPECT_EQ(vertical.tilt.yaw, 0);
    EXPECT_NE(vertical.tilt.pitch, 0);
}

TEST(Perspective, ARobustFitLeavesOutALineThatDisagrees) {
    const Tilt truth{1.5, 4, 0};
    auto lines = linesStraightAfter(truth);
    lines.push_back({{100, 700}, {700, 560}});  // leaning 13 degrees, not meant as level
    const TiltFit fit = fitTilt(lines, kImage, kFocal, TiltMode::Full, true);
    EXPECT_FALSE(fit.used.back());
    EXPECT_TRUE(fit.used.front());
    EXPECT_NEAR(fit.tilt.roll, truth.roll, 0.01);
    EXPECT_NEAR(fit.tilt.pitch, truth.pitch, 0.01);
}

TEST(Perspective, ARoughHorizontalDoesNotTakeTheGoodOnesWithIt) {
    // Six verticals, two true horizontals and a rough one beside the first. Leaving out one of
    // two horizontals fixes yaw, which once made the other lean and go too, all of them in turn.
    const Tilt truth{2, 5, -3};
    const Homography back = tiltTransform(kImage, kFocal, truth).inverse();
    const auto line = [&](PointD a, PointD b) { return GuideLine{back.apply(a), back.apply(b)}; };
    std::vector<GuideLine> lines;
    for (const double x : {-450.0, -300.0, -150.0, 100.0, 250.0, 400.0}) {
        lines.push_back(line({x, -250}, {x, 250}));
    }
    lines.push_back(line({-350, -300}, {350, -300}));
    lines.push_back(line({-350, 260}, {350, 260}));
    const double off = 250 * std::tan(2 * std::numbers::pi / 180);
    lines.push_back(line({-250, -280 - off}, {250, -280 + off}));
    const TiltFit fit = fitTilt(lines, kImage, kFocal, TiltMode::Full, true);
    EXPECT_TRUE(fit.used[6]);
    EXPECT_TRUE(fit.used[7]);
    EXPECT_FALSE(fit.used[8]);
    EXPECT_NEAR(fit.tilt.yaw, truth.yaw, 0.01);
}

TEST(Perspective, ALineTheUserDrewIsNeverLeftOut) {
    const Tilt truth{1.5, 4, 0};
    auto lines = linesStraightAfter(truth);
    lines.push_back({{100, 700}, {700, 560}, true});
    const TiltFit fit = fitTilt(lines, kImage, kFocal, TiltMode::Full, true);
    EXPECT_TRUE(fit.used.back());
}

TEST(Perspective, TheLargestRectangleFitsExactly) {
    const std::array<PointD, 4> diamond{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
    const RectD square = largestRectangle(diamond, 1);
    EXPECT_NEAR(square.width, 1, 1e-9);
    EXPECT_NEAR(square.height, 1, 1e-9);
    EXPECT_NEAR(square.x, -0.5, 1e-9);

    const std::array<PointD, 4> box{{{0, 0}, {4, 0}, {4, 2}, {0, 2}}};
    const RectD any = largestRectangle(box, 0);
    EXPECT_NEAR(any.width * any.height, 8, 1e-3);
}

TEST(Perspective, WhatIsKeptLiesInsideThePicture) {
    for (const Tilt& tilt : {Tilt{10, 0, 0}, Tilt{-2, 8, 0}, Tilt{1, -6, 5}}) {
        for (const bool keepRatio : {true, false}) {
            const TiltPlan plan = planTilt(kImage, kFocal, tilt, keepRatio);
            EXPECT_EQ(plan.size.width, static_cast<int>(plan.kept.width));
            if (keepRatio) {
                EXPECT_NEAR(static_cast<double>(plan.size.width) / plan.size.height, 1.5, 0.01);
            }
            const Homography back = plan.transform.inverse();
            const RectD k = plan.kept;
            for (const PointD corner : {PointD{k.x, k.y}, PointD{k.x + k.width, k.y}, PointD{k.x, k.y + k.height},
                                        PointD{k.x + k.width, k.y + k.height}}) {
                const PointD p = back.apply(corner);
                EXPECT_GE(p.x, -1e-6);
                EXPECT_GE(p.y, -1e-6);
                EXPECT_LE(p.x, kImage.width + 1e-6);
                EXPECT_LE(p.y, kImage.height + 1e-6);
            }
        }
    }
}

TEST(Perspective, WarpingWithoutAChangeKeepsEveryPixel) {
    const ImageBuffer image = turnedRectangle(7);
    const ImageBuffer same = warp(image, {1, 0, 0, 0, 1, 0, 0, 0, 1}, image.size());
    ASSERT_EQ(same.size(), image.size());
    for (int y = 0; y < image.height(); ++y) {
        ASSERT_EQ(std::memcmp(same.row(y), image.row(y), 600 * 4), 0) << "row " << y;
    }
}

TEST(Perspective, FindsTheEdgesOfATurnedRectangleAndLevelsThem) {
    const ImageBuffer image = turnedRectangle(4);
    const auto lines = detectLines(image);
    ASSERT_GE(lines.size(), 4U);
    const GuideLine& longest = lines.front();
    EXPECT_GT(longest.length(), 300);
    const double degrees = std::atan2(longest.b.y - longest.a.y, longest.b.x - longest.a.x) * 180 / std::numbers::pi;
    EXPECT_NEAR(std::abs(std::remainder(degrees, 180.0)), 4, 0.5);

    const TiltFit fit = fitTilt(lines, image.size(), focalPixels(image.size(), 28), TiltMode::Rotation, true);
    EXPECT_NEAR(fit.tilt.roll, -4, 0.2);
}
