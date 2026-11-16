#include <gtest/gtest.h>

#include <array>

#include "core/strip_layout.h"

using namespace iv;

namespace {

void expectRect(const PixelRect& actual, PixelRect expected, int index) {
    EXPECT_EQ(actual.x, expected.x) << "picture " << index;
    EXPECT_EQ(actual.y, expected.y) << "picture " << index;
    EXPECT_EQ(actual.width, expected.width) << "picture " << index;
    EXPECT_EQ(actual.height, expected.height) << "picture " << index;
}

}  // namespace

// Why: the strip builder shows the size of the result before anything is decoded, and puts
// each picture where the layout says. Input: a landscape 400 x 200, a portrait 100 x 200 and
// a square 300 x 300, brought to a height of 100 across, with 10 pixels between and a margin
// of 5. The truth, worked out by hand: widths follow each ratio (200, 50, 100), the pictures
// follow one another with the spacing, and the canvas holds them and the margin.
TEST(StripLayout, AcrossBringsEveryPictureToOneHeight) {
    const std::array<Size, 3> pictures{Size{400, 200}, Size{100, 200}, Size{300, 300}};
    StripOptions options;
    options.extent = 100;
    options.spacing = 10;
    options.margin = 5;
    const StripLayout layout = layoutStrip(pictures, options);
    ASSERT_EQ(layout.pictures.size(), 3u);
    expectRect(layout.pictures[0], {5, 5, 200, 100}, 0);
    expectRect(layout.pictures[1], {215, 5, 50, 100}, 1);
    expectRect(layout.pictures[2], {275, 5, 100, 100}, 2);
    EXPECT_EQ(layout.canvas, (Size{5 + 200 + 10 + 50 + 10 + 100 + 5, 110}));
}

// Why: down is across turned: one width for all, heights by ratio. Input: the same pictures
// brought to a width of 100, no spacing. The truth: heights 50, 200 and 100, one under another.
TEST(StripLayout, DownBringsEveryPictureToOneWidth) {
    const std::array<Size, 3> pictures{Size{400, 200}, Size{100, 200}, Size{300, 300}};
    StripOptions options;
    options.direction = StripDirection::Down;
    options.extent = 100;
    const StripLayout layout = layoutStrip(pictures, options);
    expectRect(layout.pictures[0], {0, 0, 100, 50}, 0);
    expectRect(layout.pictures[1], {0, 50, 100, 200}, 1);
    expectRect(layout.pictures[2], {0, 250, 100, 100}, 2);
    EXPECT_EQ(layout.canvas, (Size{100, 350}));
}

// Why: with "only reduce", a small picture is not blown up; it keeps its size and sits in the
// middle of the row, which is as tall as the tallest picture. Input: 400 x 400 and 60 x 40,
// to a height of 100. The truth: the first becomes 100 x 100, the second stays 60 x 40 and is
// moved down by (100 - 40) / 2 = 30.
TEST(StripLayout, OnlyReducingKeepsASmallPictureCentred) {
    const std::array<Size, 2> pictures{Size{400, 400}, Size{60, 40}};
    StripOptions options;
    options.extent = 100;
    options.onlyReduce = true;
    const StripLayout layout = layoutStrip(pictures, options);
    expectRect(layout.pictures[0], {0, 0, 100, 100}, 0);
    expectRect(layout.pictures[1], {100, 30, 60, 40}, 1);
    EXPECT_EQ(layout.canvas, (Size{160, 100}));
}

// Why: a shadow falls right and down of each picture, so room is kept for it, or it would be
// cut off or fall on the next picture. Input: two 100 x 100 pictures, a shadow of 8 and a
// spacing of 4. The truth: the second starts after the first, its shadow and the spacing; the
// canvas includes the last shadow on the right and the shadow below.
TEST(StripLayout, RoomIsKeptForTheShadow) {
    const std::array<Size, 2> pictures{Size{100, 100}, Size{100, 100}};
    StripOptions options;
    options.extent = 100;
    options.spacing = 4;
    options.shadow = 8;
    const StripLayout layout = layoutStrip(pictures, options);
    expectRect(layout.pictures[1], {112, 0, 100, 100}, 1);
    EXPECT_EQ(layout.canvas, (Size{220, 108}));
}

// Why: a contact sheet is the same layout wrapped after a number of pictures. Input: five
// 200 x 100 pictures at a height of 50, three per row, spacing 2. The truth: the fourth starts
// a new row under the first, and the canvas is as wide as the longer row.
TEST(StripLayout, WrappingMakesAGrid) {
    const std::array<Size, 5> pictures{Size{200, 100}, Size{200, 100}, Size{200, 100}, Size{200, 100}, Size{200, 100}};
    StripOptions options;
    options.extent = 50;
    options.spacing = 2;
    options.perLine = 3;
    const StripLayout layout = layoutStrip(pictures, options);
    expectRect(layout.pictures[2], {204, 0, 100, 50}, 2);
    expectRect(layout.pictures[3], {0, 52, 100, 50}, 3);
    expectRect(layout.pictures[4], {102, 52, 100, 50}, 4);
    EXPECT_EQ(layout.canvas, (Size{304, 102}));
}

// Why: the dialog works out the size as the user types, so odd input must give a sane answer,
// not a crash or a negative size. Input: no pictures; a picture of size nothing. The truth: an
// empty strip is just its margins; a picture of nothing counts as one pixel square.
TEST(StripLayout, OddInputGivesASaneSize) {
    StripOptions options;
    options.margin = 3;
    EXPECT_EQ(layoutStrip({}, options).canvas, (Size{6, 6}));
    const std::array<Size, 1> nothing{Size{0, 0}};
    options.extent = 10;
    const StripLayout layout = layoutStrip(nothing, options);
    expectRect(layout.pictures[0], {3, 3, 10, 10}, 0);
}
