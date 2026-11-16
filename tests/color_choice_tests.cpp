#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "core/color_choice.h"
#include "core/palette.h"

using namespace iv;

// Why: the chooser shows a colour's lightness, chroma and hue, and setting them gives the
// colour back. Input: white, black, a mid gray, and sRGB red. The truth: white L 1 and black
// L 0, both with no chroma; the gray no chroma and hue 0; red at the published Oklch of sRGB
// red, L 0.628, C 0.258, H 29.2 (from Ottosson's Oklab and CSS Color 4); and red through
// fromOklch back to itself.
TEST(ColorChoice, AColourHasItsLightnessChromaAndHue) {
    EXPECT_NEAR(toOklch({255, 255, 255, 255}).L, 1, 1e-3);
    EXPECT_NEAR(toOklch({255, 255, 255, 255}).C, 0, 1e-3);
    EXPECT_NEAR(toOklch({0, 0, 0, 255}).L, 0, 1e-6);
    const Oklch gray = toOklch({128, 128, 128, 255});
    EXPECT_EQ(gray.C, 0);
    EXPECT_EQ(gray.H, 0);
    const Oklch red = toOklch({255, 0, 0, 255});
    EXPECT_NEAR(red.L, 0.628, 1e-3);
    EXPECT_NEAR(red.C, 0.258, 1e-3);
    EXPECT_NEAR(red.H, 29.23, 0.05);
    EXPECT_EQ(fromOklch(red.L, red.C, red.H), (Rgba8{255, 0, 0, 255}));
}

// Why: each slider's track shows what moving it would give with the other channels held.
// Input: the colour 40, 120, 200, its green ramp of 3 stops. The truth: green 0, 128 and 255
// with red and blue as they were, nothing marked outside sRGB.
TEST(ColorChoice, AnRgbRampMovesOneChannel) {
    const Rgba8 colour{40, 120, 200, 255};
    const auto ramp = channelRamp(ColorChannel::Green, colour, toOklch(colour), 3);
    ASSERT_EQ(ramp.size(), 3u);
    EXPECT_EQ(ramp[0].colour, (Rgba8{40, 0, 200, 255}));
    EXPECT_EQ(ramp[1].colour, (Rgba8{40, 128, 200, 255}));
    EXPECT_EQ(ramp[2].colour, (Rgba8{40, 255, 200, 255}));
    EXPECT_FALSE(ramp[1].outside);
}

// Why: on the lightness, chroma and hue tracks, colours sRGB cannot show are marked, and shown
// as the chooser would give them, with less chroma. Input: a strong blue's chroma ramp; the
// lightness ramp of a gray that keeps the hue it was given; the hue ramp of a gray. The truth:
// the chroma ramp starts gray and inside, and ends outside; a gray's lightness ramp runs from
// black to white, all inside; a gray's hue ramp is all one gray, as no hue shows without chroma.
TEST(ColorChoice, PolarRampsMarkWhatSrgbCannotShow) {
    const Oklch blue{0.45, 0.25, 264};
    const auto chroma = channelRamp(ColorChannel::Chroma, {0, 0, 255, 255}, blue, 16);
    EXPECT_FALSE(chroma.front().outside);
    EXPECT_EQ(chroma.front().colour.r, chroma.front().colour.b) << "no chroma is gray";
    EXPECT_TRUE(chroma.back().outside);

    const Oklch gray{0.5, 0, 120};
    const auto lightness = channelRamp(ColorChannel::Lightness, {99, 99, 99, 255}, gray, 5);
    EXPECT_EQ(lightness.front().colour, (Rgba8{0, 0, 0, 255}));
    EXPECT_EQ(lightness.back().colour, (Rgba8{255, 255, 255, 255}));
    EXPECT_TRUE(std::ranges::none_of(lightness, [](const RampStop& stop) { return stop.outside; }));

    const auto hue = channelRamp(ColorChannel::Hue, {99, 99, 99, 255}, gray, 5);
    EXPECT_TRUE(std::ranges::all_of(hue, [&](const RampStop& stop) { return stop.colour == hue.front().colour; }));
}
