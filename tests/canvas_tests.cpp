#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "core/canvas.h"

using namespace iv;

namespace {

ImageBuffer filled(Size size, Rgba8 colour, bool alpha = false) {
    const ImageBuffer picture = ImageBuffer::create(size, PixelFormat::Bgrx8).withAlpha(alpha);
    for (int y = 0; y < size.height; ++y) {
        auto* p = reinterpret_cast<std::uint8_t*>(picture.row(y));
        for (int x = 0; x < size.width; ++x, p += 4) {
            p[0] = colour.b;
            p[1] = colour.g;
            p[2] = colour.r;
            p[3] = colour.a;
        }
    }
    return picture;
}

Rgba8 at(const ImageBuffer& image, int x, int y) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(image.row(y)) + x * 4;
    return {p[2], p[1], p[0], p[3]};
}

constexpr Rgba8 kRed{200, 30, 20, 255};
constexpr Rgba8 kWhite{255, 255, 255, 255};

}  // namespace

// Why: a strip is pictures put where the layout says, on the background chosen. Input: a red
// 4 x 3 picture at 2, 1 on a white 10 x 6 canvas, no shadow. The truth: the picture's pixels
// unchanged where it lies, white everywhere else, and no alpha in the result.
TEST(Canvas, PicturesGoWhereTheyArePlacedOnTheBackground) {
    const std::array<PlacedPicture, 1> pictures{PlacedPicture{filled({4, 3}, kRed), 2, 1}};
    const ImageBuffer canvas = composeCanvas({10, 6}, pictures, CanvasLook{});
    EXPECT_FALSE(canvas.hasAlpha());
    EXPECT_EQ(at(canvas, 2, 1), kRed);
    EXPECT_EQ(at(canvas, 5, 3), kRed);
    EXPECT_EQ(at(canvas, 6, 3), kWhite);
    EXPECT_EQ(at(canvas, 1, 1), kWhite);
    EXPECT_EQ(at(canvas, 2, 4), kWhite);
}

// Why: a strip for a web page may be wanted without a background, to sit on the page's own.
// Input: the same, with no background. The truth: the result has alpha; the empty part is
// fully transparent and the picture fully opaque in its own colours.
TEST(Canvas, WithoutABackgroundTheRestIsTransparent) {
    const std::array<PlacedPicture, 1> pictures{PlacedPicture{filled({4, 3}, kRed), 2, 1}};
    CanvasLook look;
    look.background.reset();
    const ImageBuffer canvas = composeCanvas({10, 6}, pictures, look);
    EXPECT_TRUE(canvas.hasAlpha());
    EXPECT_EQ(at(canvas, 0, 0).a, 0);
    EXPECT_EQ(at(canvas, 3, 2), kRed);
}

// Why: the shadow falls right of and below a picture and reaches as far as asked, which the
// layout keeps room for, and no further; above and left only a faint edge of it shows. Input:
// a red 20 x 20 picture at 10, 10 on a white 50 x 50 canvas, shadow 6 (offset 2, sigma 4/3).
// The truth: the picture is unchanged; next to its right and bottom edges it is dark; just
// left of and above it at most faint; 3 pixels away from those edges, and 2 pixels past the
// reach of 6, it stays white.
TEST(Canvas, AShadowFallsRightAndDownAsFarAsAsked) {
    const std::array<PlacedPicture, 1> pictures{PlacedPicture{filled({20, 20}, kRed), 10, 10}};
    CanvasLook look;
    look.shadow = 6;
    const ImageBuffer canvas = composeCanvas({50, 50}, pictures, look);
    EXPECT_EQ(at(canvas, 29, 29), kRed);
    EXPECT_LT(at(canvas, 31, 20).g, 200) << "right of the picture";
    EXPECT_LT(at(canvas, 20, 31).g, 200) << "below the picture";
    EXPECT_LT(at(canvas, 31, 31).g, 200) << "at the corner";
    EXPECT_GT(at(canvas, 9, 20).g, 240) << "just left of the picture, faint";
    EXPECT_GT(at(canvas, 20, 9).g, 240) << "just above the picture, faint";
    EXPECT_EQ(at(canvas, 7, 20), kWhite) << "left of the picture";
    EXPECT_EQ(at(canvas, 20, 7), kWhite) << "above the picture";
    EXPECT_EQ(at(canvas, 37, 20), kWhite) << "past the reach";
    EXPECT_EQ(at(canvas, 20, 37), kWhite);
}

// Why: on a transparent canvas the shadow is kept in alpha, so it falls on whatever the strip
// is put on. Input: the picture above, shadow 6, no background. The truth: beside the picture
// the pixel is black and partly opaque; past the reach fully transparent.
TEST(Canvas, OnATransparentCanvasTheShadowIsInTheAlpha) {
    const std::array<PlacedPicture, 1> pictures{PlacedPicture{filled({20, 20}, kRed), 10, 10}};
    CanvasLook look;
    look.background.reset();
    look.shadow = 6;
    const ImageBuffer canvas = composeCanvas({50, 50}, pictures, look);
    const Rgba8 shade = at(canvas, 31, 20);
    EXPECT_GT(shade.a, 40);
    EXPECT_LT(shade.a, 255);
    EXPECT_EQ(shade.r, 0);
    EXPECT_EQ(at(canvas, 37, 20).a, 0);
}

// Why: a picture with transparent parts casts a shadow only where it is, as a cut-out logo
// should, and its half transparent pixels let the background through. Input: a 20 x 20
// picture whose left half is fully transparent and right half red at half alpha, at 10, 10 on
// white, shadow 6. The truth: no shadow below the transparent half; below the red half a
// shadow; without a shadow, the red half is half way between red and white (with one, its
// own shadow shows through it, as it should).
TEST(Canvas, OnlyWhatAPictureCoversCastsAShadow) {
    ImageBuffer picture = filled({20, 20}, Rgba8{200, 30, 20, 128}, true);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 10; ++x) {
            reinterpret_cast<std::uint8_t*>(picture.row(y))[x * 4 + 3] = 0;
        }
    }
    const std::array<PlacedPicture, 1> pictures{PlacedPicture{picture, 10, 10}};
    CanvasLook look;
    look.shadow = 6;
    const ImageBuffer canvas = composeCanvas({50, 50}, pictures, look);
    EXPECT_EQ(at(canvas, 12, 32), kWhite) << "below the transparent half";
    EXPECT_LT(at(canvas, 26, 32).g, 255) << "below the red half";
    const ImageBuffer plain = composeCanvas({50, 50}, pictures, CanvasLook{});
    const Rgba8 half = at(plain, 25, 15);
    EXPECT_NEAR(half.r, (200 + 255) / 2, 2);
    EXPECT_NEAR(half.g, (30 + 255) / 2, 2);
}

namespace {

FrameLook threeFrames(int outer, int middle, int inner) {
    FrameLook look;
    look.frames = {FrameLook::Frame{outer, {0, 0, 0, 255}}, FrameLook::Frame{middle, {255, 255, 255, 255}},
                   FrameLook::Frame{inner, {128, 128, 128, 255}}};
    return look;
}

}  // namespace

// Why: the frames go around the picture, outer first, each as wide as set, and one of width 0
// is left out. Input: a red 4 x 3 picture in frames of 1 black, 2 white and 1 gray; then of 2
// black, none white and 1 gray. The truth: 12 x 11, black at the edge, white two pixels in,
// gray next, then the picture; without the white frame, gray straight after the black.
TEST(Frame, FramesGoAroundThePictureOuterFirst) {
    const ImageBuffer picture = filled({4, 3}, kRed);
    const FrameLook look = threeFrames(1, 2, 1);
    EXPECT_EQ(framedSize(picture.size(), look), (Size{12, 11}));
    const ImageBuffer result = framed(picture, look);
    ASSERT_EQ(result.size(), (Size{12, 11}));
    EXPECT_FALSE(result.hasAlpha());
    EXPECT_EQ(at(result, 0, 5), (Rgba8{0, 0, 0, 255}));
    EXPECT_EQ(at(result, 1, 5), kWhite);
    EXPECT_EQ(at(result, 2, 5), kWhite);
    EXPECT_EQ(at(result, 3, 5), (Rgba8{128, 128, 128, 255}));
    EXPECT_EQ(at(result, 4, 5), kRed);
    EXPECT_EQ(at(result, 11, 10), (Rgba8{0, 0, 0, 255})) << "the far corner";

    const ImageBuffer noMiddle = framed(picture, threeFrames(2, 0, 1));
    ASSERT_EQ(noMiddle.size(), (Size{10, 9}));
    EXPECT_EQ(at(noMiddle, 1, 4), (Rgba8{0, 0, 0, 255}));
    EXPECT_EQ(at(noMiddle, 2, 4), (Rgba8{128, 128, 128, 255}));
    EXPECT_EQ(at(noMiddle, 3, 4), kRed);
}

// Why: a frame is an edit, and edits keep a picture's depth, so a 16-bit picture is framed in
// 16 bits, its own pixels untouched. Input: a 16-bit 2 x 2 picture of value 1000, in a white
// frame of 1. The truth: Rgba16; the picture still 1000; the frame 65535, the full range's white.
TEST(Frame, AFramedPictureKeepsItsDepth) {
    const ImageBuffer deep = ImageBuffer::create({2, 2}, PixelFormat::Rgba16);
    for (int y = 0; y < 2; ++y) {
        auto* p = reinterpret_cast<std::uint16_t*>(deep.row(y));
        for (int i = 0; i < 8; ++i) {
            p[i] = i % 4 == 3 ? 65535 : 1000;
        }
    }
    FrameLook look;
    look.frames[0] = {1, {255, 255, 255, 255}};
    const ImageBuffer result = framed(deep, look);
    ASSERT_EQ(result.format(), PixelFormat::Rgba16);
    ASSERT_EQ(result.size(), (Size{4, 4}));
    EXPECT_EQ(reinterpret_cast<const std::uint16_t*>(result.row(1))[4], 1000);
    EXPECT_EQ(reinterpret_cast<const std::uint16_t*>(result.row(0))[0], 65535);
}

// Why: a gray picture stays gray in gray frames, and becomes colour only when a frame needs
// it, as annotations do. Input: a gray 2 x 2 picture in a gray frame, then in a red one. The
// truth: Gray8, then Bgrx8 with the frame red.
TEST(Frame, AGrayPictureBecomesColourOnlyForAColouredFrame) {
    const ImageBuffer gray = ImageBuffer::create({2, 2}, PixelFormat::Gray8);
    std::memset(gray.row(0), 90, 2);
    std::memset(gray.row(1), 90, 2);
    FrameLook look;
    look.frames[0] = {1, {200, 200, 200, 255}};
    EXPECT_EQ(framed(gray, look).format(), PixelFormat::Gray8);
    look.frames[0].colour = kRed;
    const ImageBuffer coloured = framed(gray, look);
    ASSERT_EQ(coloured.format(), PixelFormat::Bgrx8);
    EXPECT_EQ(at(coloured, 0, 0), kRed);
    EXPECT_EQ(at(coloured, 1, 1), (Rgba8{90, 90, 90, 255}));
}

// Why: room is kept for the shadow so none of it is cut off, and on transparency the result
// keeps alpha only around the picture, while a picture with alpha is put on white, as a frame
// stands for a print. Input: a 20 x 20 red picture in a black frame of 2, shadow 6 (reach 6
// right and below, faint edge (6 + 2) / 3 = 2 left and above), on white, then on transparency;
// and a fully transparent picture in the same frame without a shadow. The truth: 32 x 32 with
// the picture at 4, 4; on white no alpha; on transparency the corner clear, the shadow black
// and partly opaque, the frame and picture opaque; the transparent picture white and opaque.
TEST(Frame, AShadowGetsRoomAndOnlyItKeepsAlpha) {
    const ImageBuffer picture = filled({20, 20}, kRed);
    FrameLook look;
    look.frames[0] = {2, {0, 0, 0, 255}};
    look.shadow = 6;
    EXPECT_EQ(framedSize(picture.size(), look), (Size{32, 32}));
    EXPECT_EQ(framedPicture(picture.size(), look).x, 4);
    const ImageBuffer onWhite = framed(picture, look);
    EXPECT_FALSE(onWhite.hasAlpha());
    EXPECT_EQ(at(onWhite, 4, 4), kRed);
    EXPECT_LT(at(onWhite, 27, 15).g, 200) << "the shadow, right of the frame";

    look.transparent = true;
    const ImageBuffer clear = framed(picture, look);
    ASSERT_TRUE(clear.hasAlpha());
    EXPECT_EQ(at(clear, 0, 31).a, 0);
    EXPECT_GT(at(clear, 27, 15).a, 40);
    EXPECT_EQ(at(clear, 27, 15).r, 0);
    EXPECT_EQ(at(clear, 2, 15), (Rgba8{0, 0, 0, 255})) << "the frame";
    EXPECT_EQ(at(clear, 10, 10), kRed);

    FrameLook plain;
    plain.frames[0] = {2, {0, 0, 0, 255}};
    const ImageBuffer cutOut = framed(filled({4, 4}, Rgba8{200, 30, 20, 0}, true), plain);
    EXPECT_FALSE(cutOut.hasAlpha());
    EXPECT_EQ(at(cutOut, 3, 3), kWhite);
}
