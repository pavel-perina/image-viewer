// The dark edge and the shadow of annotations are made from how much of each pixel a shape
// covers (core/coverage.h). These check the three steps on planes small enough to work out by
// hand: growing by a distance that need not be whole, moving, and blurring.

#include <numeric>

#include <gtest/gtest.h>

#include "core/coverage.h"

using namespace iv;

namespace {

// A plane with the pixels from `left` to `right` in every row covered by the amounts given.
Coverage columns(int width, int height, int left, std::initializer_list<float> amounts) {
    Coverage plane(width, height);
    for (int y = 0; y < height; ++y) {
        int x = left;
        for (const float amount : amounts) {
            plane.at(x++, y) = amount;
        }
    }
    return plane;
}

}  // namespace

// Why: an outline two pixels wide must be two pixels wide. A shape filling columns 0 to 4
// has its edge between columns 4 and 5. Grown by 2 with the usual edge of one pixel, columns
// 5 and 6, whose middles are 0.5 and 1.5 outside, are covered, column 7 at 2.5 is not, and
// the shape itself stays covered.
TEST(Coverage, GrowsByWholePixels) {
    const Coverage grown = spread(columns(12, 5, 0, {1, 1, 1, 1, 1}), 2);
    for (const int x : {0, 4, 5, 6}) {
        EXPECT_FLOAT_EQ(grown.at(x, 2), 1) << x;
    }
    EXPECT_FLOAT_EQ(grown.at(7, 2), 0);
}

// Why: the width need not be whole, which is what lets a thin outline be 1.5 pixels. With
// the same edge between columns 4 and 5, growing by 1.5 covers column 5 (0.5 outside), half
// of column 6 (its middle 1.5 outside, exactly where the grown edge is) and none of column 7.
TEST(Coverage, GrowsByPartOfAPixel) {
    const Coverage grown = spread(columns(12, 5, 0, {1, 1, 1, 1, 1}), 1.5);
    EXPECT_FLOAT_EQ(grown.at(5, 2), 1);
    EXPECT_FLOAT_EQ(grown.at(6, 2), 0.5);
    EXPECT_FLOAT_EQ(grown.at(7, 2), 0);
}

// Why: shapes are drawn smoothly, so their edges are partly covered pixels, and the outline
// has to follow where the edge really is. Column 5 covered by a quarter puts the edge a
// quarter of a pixel into it. Grown by 1, the new edge is 1.25 into column 5's left side,
// that is a quarter into column 6: column 5 is covered and column 6 by a quarter.
TEST(Coverage, GrowsFromWhereAPartlyCoveredEdgeIs) {
    const Coverage grown = spread(columns(12, 5, 0, {1, 1, 1, 1, 1, 0.25F}), 1);
    EXPECT_FLOAT_EQ(grown.at(5, 2), 1);
    EXPECT_FLOAT_EQ(grown.at(6, 2), 0.25);
    EXPECT_FLOAT_EQ(grown.at(7, 2), 0);
}

// Why: a softer edge is wanted for small outlines. With `soft` 2 the grown edge fades over
// two pixels around the same place: for the edge between columns 4 and 5 grown by 2, the
// middle of column 6 is half a pixel inside the new edge, so 0.75, and column 7 half a pixel
// outside, so 0.25.
TEST(Coverage, ASofterEdgeFadesOverMorePixels) {
    const Coverage grown = spread(columns(12, 5, 0, {1, 1, 1, 1, 1}), 2, 2);
    EXPECT_FLOAT_EQ(grown.at(5, 2), 1);
    EXPECT_FLOAT_EQ(grown.at(6, 2), 0.75);
    EXPECT_FLOAT_EQ(grown.at(7, 2), 0.25);
    EXPECT_FLOAT_EQ(grown.at(8, 2), 0);
}

// Why: growing is the same in every direction, so a dot grows into a disc and not a square.
// One covered pixel in the middle, grown by 3: straight across, a pixel 3 away is 2.5 from
// the dot's edge and covered; on the diagonal, the pixel 3 across and 3 down is about 3.7
// from it and is not.
TEST(Coverage, GrowsRound) {
    Coverage dot(15, 15);
    dot.at(7, 7) = 1;
    const Coverage grown = spread(dot, 3);
    EXPECT_FLOAT_EQ(grown.at(10, 7), 1);
    EXPECT_FLOAT_EQ(grown.at(7, 4), 1);
    EXPECT_FLOAT_EQ(grown.at(10, 10), 0);
}

// Why: a shadow lies to one side by any distance, in each direction separately. Moved 2
// right and 1 up, a pixel lands there; moved by half a pixel, it is shared by two.
TEST(Coverage, MovesByWholeAndPartPixels) {
    Coverage dot(8, 8);
    dot.at(3, 3) = 1;
    const Coverage moved = shifted(dot, 2, -1);
    EXPECT_FLOAT_EQ(moved.at(5, 2), 1);
    EXPECT_FLOAT_EQ(moved.at(3, 3), 0);
    const Coverage half = shifted(dot, 0.5, 0);
    EXPECT_FLOAT_EQ(half.at(3, 3), 0.5);
    EXPECT_FLOAT_EQ(half.at(4, 3), 0.5);
}

// Why: blurring a shadow must spread it without making it darker or lighter overall, or
// moving it. One covered pixel well inside the plane keeps its total of 1, stays highest
// where it was and is the same on both sides; a sigma of nothing changes nothing.
TEST(Coverage, BlurSpreadsWithoutChangingTheTotal) {
    Coverage dot(21, 21);
    dot.at(10, 10) = 1;
    const Coverage soft = blurred(dot, 1.5);
    EXPECT_NEAR(std::accumulate(soft.values.begin(), soft.values.end(), 0.0), 1, 1e-4);
    EXPECT_GT(soft.at(10, 10), soft.at(11, 10));
    EXPECT_GT(soft.at(11, 10), soft.at(12, 10));
    EXPECT_FLOAT_EQ(soft.at(8, 10), soft.at(12, 10));
    EXPECT_FLOAT_EQ(soft.at(10, 8), soft.at(12, 10));
    EXPECT_EQ(blurred(dot, 0).values, dot.values);
}
