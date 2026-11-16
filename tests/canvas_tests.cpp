#include <gtest/gtest.h>

#include <array>
#include <cstdint>

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
// layout keeps room for, and no further; nor does it come out above or left. Input: a red
// 20 x 20 picture at 10, 10 on a white 50 x 50 canvas, shadow 6. The truth: the picture is
// unchanged; next to its right and bottom edges it is dark; left of and above it, and 2
// pixels past the reach of 6, it stays white.
TEST(Canvas, AShadowFallsRightAndDownAsFarAsAsked) {
    const std::array<PlacedPicture, 1> pictures{PlacedPicture{filled({20, 20}, kRed), 10, 10}};
    CanvasLook look;
    look.shadow = 6;
    const ImageBuffer canvas = composeCanvas({50, 50}, pictures, look);
    EXPECT_EQ(at(canvas, 29, 29), kRed);
    EXPECT_LT(at(canvas, 31, 20).g, 200) << "right of the picture";
    EXPECT_LT(at(canvas, 20, 31).g, 200) << "below the picture";
    EXPECT_LT(at(canvas, 31, 31).g, 200) << "at the corner";
    EXPECT_EQ(at(canvas, 9, 20), kWhite) << "left of the picture";
    EXPECT_EQ(at(canvas, 20, 9), kWhite) << "above the picture";
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
// shadow; on its top row, which its own shadow, moved down, does not reach, the red half is
// half way between red and white. Further down, its own shadow shows through it, as it should.
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
    const Rgba8 half = at(canvas, 25, 10);
    EXPECT_NEAR(half.r, (200 + 255) / 2, 2);
    EXPECT_NEAR(half.g, (30 + 255) / 2, 2);
}
