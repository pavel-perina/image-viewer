#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <random>
#include <string>

#include <gtest/gtest.h>

#include "core/color_edit.h"
#include "core/oklab.h"

using namespace iv;

namespace {

ImageBuffer colours8() {
    const ImageBuffer image = ImageBuffer::create({4, 1}, PixelFormat::Bgrx8);
    const std::uint8_t bgrx[] = {30, 60, 220, 7, 200, 180, 20, 8, 128, 128, 128, 9, 0, 255, 0, 10};
    std::memcpy(image.row(0), bgrx, sizeof(bgrx));
    return image;
}

std::string cubeText(const ColorCube& cube) {
    std::string text = "TITLE \"test\"\n# a comment\nLUT_3D_SIZE " + std::to_string(cube.size) + "\n";
    for (std::size_t i = 0; i < cube.rgb.size(); i += 3) {
        text += std::to_string(cube.rgb[i]) + " " + std::to_string(cube.rgb[i + 1]) + " " + std::to_string(cube.rgb[i + 2]) + "\n";
    }
    return text;
}

}  // namespace

TEST(ColorEdit, AnEmptyEditChangesNothingOnAnyFormat) {
    const ColorEdit none;
    EXPECT_TRUE(none.isIdentity());
    for (const PixelFormat format : {PixelFormat::Bgrx8, PixelFormat::Gray8, PixelFormat::Gray16, PixelFormat::Rgba16,
                                     PixelFormat::Gray32F, PixelFormat::Rgba32F}) {
        const ImageBuffer image = ImageBuffer::create({3, 3}, format);
        EXPECT_EQ(applyColor(image, none).data(), image.data());
    }
    EXPECT_TRUE(levelsCurve(0, 1, 1).isIdentity());
}

TEST(ColorEdit, LevelsSetTheBlackAndWhitePointsAndTheMidtones) {
    const Curve curve = levelsCurve(0.2, 0.8, 1);
    EXPECT_FLOAT_EQ(curve(0.1F), 0);
    EXPECT_NEAR(curve(0.5F), 0.5F, 1e-6);
    EXPECT_FLOAT_EQ(curve(0.9F), 1);
    EXPECT_NEAR(levelsCurve(0, 1, 2)(0.25F), 0.5F, 1e-4) << "gamma 2 lightens the midtones";
}

TEST(ColorEdit, CurvesKeepSixteenBitPrecision) {
    const ImageBuffer gray = ImageBuffer::create({2, 1}, PixelFormat::Gray16);
    auto* p = reinterpret_cast<std::uint16_t*>(gray.row(0));
    p[0] = 30000;
    p[1] = 30001;
    ColorEdit edit;
    edit.tone = levelsCurve(0.1, 0.9, 1.3);
    const ImageBuffer out = applyColor(gray, edit);
    ASSERT_EQ(out.format(), PixelFormat::Gray16);
    const auto* q = reinterpret_cast<const std::uint16_t*>(out.row(0));
    EXPECT_NE(q[0], q[1]) << "neighbouring values stay apart";
    const double expected = std::pow((30000 / 65535.0 - 0.1) / 0.8, 1 / 1.3) * 65535;
    EXPECT_NEAR(q[0], expected, 2);
}

TEST(ColorEdit, AnIdentityCubeOfAnySizeKeepsEveryColour) {
    std::mt19937 random(7);
    std::uniform_real_distribution<float> unit(0, 1);
    for (const int size : {2, 17, 33, 65}) {
        const ColorCube cube = identityCube(size);
        for (int i = 0; i < 200; ++i) {
            const float r = unit(random);
            const float g = unit(random);
            const float b = unit(random);
            const auto out = cube(r, g, b);
            ASSERT_NEAR(out[0], r, 1e-5);
            ASSERT_NEAR(out[1], g, 1e-5);
            ASSERT_NEAR(out[2], b, 1e-5);
        }
    }
}

TEST(ColorEdit, GrayGoesThroughACubeOnTheGrayDiagonalOnly) {
    // Colours anywhere off the diagonal, gray on it: a gray input must not pick up any colour.
    ColorCube cube = identityCube(5);
    std::mt19937 random(3);
    std::uniform_real_distribution<float> unit(0, 1);
    for (int b = 0; b < 5; ++b) {
        for (int g = 0; g < 5; ++g) {
            for (int r = 0; r < 5; ++r) {
                if (r != g || g != b) {
                    float* entry = &cube.rgb[static_cast<std::size_t>(((b * 5 + g) * 5 + r) * 3)];
                    entry[0] = unit(random);
                    entry[1] = unit(random);
                    entry[2] = unit(random);
                }
            }
        }
    }
    for (const float v : {0.0F, 0.1F, 0.37F, 0.5F, 0.83F, 1.0F}) {
        const auto out = cube(v, v, v);
        EXPECT_FLOAT_EQ(out[0], out[1]);
        EXPECT_FLOAT_EQ(out[1], out[2]);
        EXPECT_NEAR(out[0], v, 1e-6);
    }
}

TEST(ColorEdit, ReadsCubeFiles) {
    const auto identity = parseCube(cubeText(identityCube(3)));
    ASSERT_TRUE(identity) << identity.error();
    EXPECT_EQ(identity->size, 3);
    const auto out = (*identity)(0.25F, 0.5F, 0.75F);
    EXPECT_NEAR(out[0], 0.25F, 1e-5);
    EXPECT_NEAR(out[2], 0.75F, 1e-5);

    // A 1D table that inverts each channel, over a domain of 0 to 2.
    const auto inverted = parseCube("LUT_1D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 2 2 2\n1 1 1\n0 0 0\n");
    ASSERT_TRUE(inverted) << inverted.error();
    const auto flipped = (*inverted)(0.5F, 1.0F, 2.0F);
    EXPECT_NEAR(flipped[0], 0.75F, 1e-5);
    EXPECT_NEAR(flipped[1], 0.5F, 1e-5);
    EXPECT_NEAR(flipped[2], 0.0F, 1e-5);

    EXPECT_FALSE(parseCube("LUT_3D_SIZE 2\n0 0 0\n"));
    EXPECT_FALSE(parseCube("0 0 0\n1 1 1\n"));
    EXPECT_FALSE(parseCube("LUT_3D_SIZE 2\nnonsense here\n"));
    EXPECT_FALSE(parseCube("LUT_3D_SIZE 500\n"));
}

TEST(ColorEdit, ChromaAtZeroLeavesGrayAndKeepsTheFourthByte) {
    ColorEdit edit;
    edit.chroma = 0;
    const ImageBuffer out = applyColor(colours8(), edit);
    const auto* p = reinterpret_cast<const std::uint8_t*>(out.row(0));
    for (int x = 0; x < 4; ++x, p += 4) {
        EXPECT_NEAR(p[0], p[1], 1);
        EXPECT_NEAR(p[1], p[2], 1);
        EXPECT_EQ(p[3], 7 + x);
    }
    const auto* mid = reinterpret_cast<const std::uint8_t*>(out.row(0)) + 8;
    EXPECT_EQ(mid[0], 128) << "gray stays as it was";
}

TEST(ColorEdit, MoreChromaMakesColoursStrongerAndLeavesGray) {
    ColorEdit edit;
    edit.chroma = 1.5;
    const ImageBuffer out = applyColor(colours8(), edit);
    const auto* p = reinterpret_cast<const std::uint8_t*>(out.row(0));
    EXPECT_GT(p[2] - p[0], 220 - 30) << "red pulls further from blue";
    EXPECT_EQ(p[8], 128);
    EXPECT_EQ(p[9], 128);
    EXPECT_EQ(p[10], 128);
}

TEST(ColorEdit, GrayPicturesTakeOnlyTheCurves) {
    const ImageBuffer gray = ImageBuffer::create({1, 1}, PixelFormat::Gray8);
    *reinterpret_cast<std::uint8_t*>(gray.row(0)) = 100;
    ColorEdit colourOnly;
    colourOnly.chroma = 0.5;
    colourOnly.cube = std::make_shared<ColorCube>(identityCube(2));
    EXPECT_EQ(applyColor(gray, colourOnly).data(), gray.data());

    ColorEdit lighter;
    lighter.lightness = levelsCurve(0, 0.8, 1);
    const ImageBuffer out = applyColor(gray, lighter);
    ASSERT_EQ(out.format(), PixelFormat::Gray8);
    EXPECT_GT(*reinterpret_cast<const std::uint8_t*>(out.row(0)), 100);
}

TEST(ColorEdit, TheCubeComesLast) {
    // A cube that swaps red and blue, after a tone curve that darkens.
    ColorCube swap = identityCube(2);
    for (std::size_t i = 0; i < swap.rgb.size(); i += 3) {
        std::swap(swap.rgb[i], swap.rgb[i + 2]);
    }
    ColorEdit edit;
    edit.cube = std::make_shared<ColorCube>(swap);
    edit.tone = levelsCurve(0, 1, 0.5);
    const ImageBuffer out = applyColor(colours8(), edit);
    const auto* p = reinterpret_cast<const std::uint8_t*>(out.row(0));
    const auto darkened = [](int v) { return static_cast<int>(std::lround(std::pow(v / 255.0, 2) * 255)); };
    EXPECT_NEAR(p[0], darkened(220), 1) << "red went to blue";
    EXPECT_NEAR(p[2], darkened(30), 1);
}

// Why: looks are often made strong, and are toned down by mixing the table's result with the
// colour it was given, as other programs' Intensity or Mix does. Input: a cube that swaps red and
// blue, at strengths 1, 0.5 and 0, on known pixels. The truth: swapped at 1; halfway between the
// pixel and its swap at 0.5, so grays stay; and at 0, no change at all, the edit an identity.
TEST(ColorEdit, TheCubesStrengthMixesItsResultWithItsInput) {
    ColorCube swap = identityCube(2);
    for (std::size_t i = 0; i < swap.rgb.size(); i += 3) {
        std::swap(swap.rgb[i], swap.rgb[i + 2]);
    }
    ColorEdit edit;
    edit.cube = std::make_shared<ColorCube>(swap);
    edit.cubeStrength = 0.5;
    const ImageBuffer half = applyColor(colours8(), edit);
    const auto* p = reinterpret_cast<const std::uint8_t*>(half.row(0));
    EXPECT_NEAR(p[2], (220 + 30) / 2, 1) << "red halfway to blue's value";
    EXPECT_NEAR(p[0], (30 + 220) / 2, 1);
    EXPECT_NEAR(p[1], 60, 1) << "green kept";
    EXPECT_NEAR(p[8], 128, 1) << "gray kept";

    edit.cubeStrength = 0;
    EXPECT_TRUE(edit.isIdentity());
    const ImageBuffer source = colours8();
    EXPECT_EQ(applyColor(source, edit).data(), source.data()) << "handed back untouched";
    ColorEdit full;
    full.cube = edit.cube;
    EXPECT_FALSE(edit == full) << "strength is part of the edit";
}

// Why: vibrance livens dull colours and leaves strong ones, so nothing is pushed out of range,
// unlike saturation's even gain; lowering tames the strong ones most. Input: chromas from dull to
// the most there is, at a blue hue and a mid lightness, at vibrance 1 and -1. The truth: the dull
// gain nearly double and the most saturated nothing; nothing ends above maxChroma; order kept;
// lowering halves the most saturated and leaves the dull nearly alone.
TEST(ColorEdit, VibranceLivensDullColoursMostAndPushesNoneOut) {
    const double L = 0.6;
    const double hue = 260 * std::numbers::pi / 180;
    const double most = maxChroma(L, hue);
    ASSERT_GT(most, 0.1);
    EXPECT_EQ(maxChroma(0, hue), 0) << "black has no colour";
    EXPECT_EQ(maxChroma(1, hue), 0);
    EXPECT_NEAR(vibrantChroma(L, 0.01, hue, 1, false), 0.02, 0.001) << "dull nearly doubles";
    EXPECT_NEAR(vibrantChroma(L, most, hue, 1, false), most, 1e-9) << "the most saturated stays";
    double previous = 0;
    for (double c = 0.005; c <= most; c += 0.005) {
        const double raised = vibrantChroma(L, c, hue, 1, false);
        EXPECT_LE(raised, most + 1e-9) << c;
        EXPECT_GT(raised, previous) << "order kept at " << c;
        previous = raised;
        const double lowered = vibrantChroma(L, c, hue, -1, false);
        EXPECT_LT(lowered, c);
        EXPECT_GT(lowered, c / 2 - 1e-9);
    }
    EXPECT_NEAR(vibrantChroma(L, most, hue, -1, false), most / 2, 1e-9) << "the strongest halved";
    EXPECT_NEAR(vibrantChroma(L, 0.01, hue, -1, false), 0.01, 0.001) << "the dull nearly kept";
    EXPECT_EQ(vibrantChroma(L, 0, hue, 1, false), 0) << "gray stays gray";
}

// Why: faces turn orange first when colours are livened, and skin of every complexion shares a
// narrow band of hue at low chroma, differing mostly in lightness (Xiao et al. 2017). Input:
// three light skins picked by hand (#bd8167, #d3a188, #dcac95) and a dark one (#3b2219), as Oklab
// lightness, chroma and hue; a blue as dull; a saturated orange. The truth: skin gains a third or
// a little more of what it gains unprotected, light or dark; the blue and the saturated orange
// are not held back; unticked, skin gains in full.
TEST(ColorEdit, VibranceSparesSkinOfAnyLightnessUnlessAskedNotTo) {
    const auto gain = [](double L, double c, double hueDegrees, bool protect) {
        return vibrantChroma(L, c, hueDegrees * std::numbers::pi / 180, 0.5, protect) / c - 1;
    };
    struct Skin {
        double L, c, hue;
    };
    for (const Skin skin : {Skin{0.658, 0.084, 44.3}, Skin{0.749, 0.069, 47.8}, Skin{0.782, 0.065, 46.9}, Skin{0.282, 0.042, 40.5}}) {
        const double share = gain(skin.L, skin.c, skin.hue, true) / gain(skin.L, skin.c, skin.hue, false);
        EXPECT_GT(share, 0.3) << skin.L;
        EXPECT_LT(share, 0.55) << skin.L;
    }
    EXPECT_NEAR(gain(0.6, 0.06, 50, true) / gain(0.6, 0.06, 50, false), 1.0 / 3, 1e-9) << "a third at the centre";
    EXPECT_DOUBLE_EQ(gain(0.6, 0.06, 250, true), gain(0.6, 0.06, 250, false)) << "blue is not skin";
    EXPECT_DOUBLE_EQ(gain(0.7, 0.21, 55, true), gain(0.7, 0.21, 55, false)) << "nor a saturated orange";
    EXPECT_EQ(skinWeight(0.06, 50 + 360), skinWeight(0.06, 50)) << "hue wraps";
}

// Why: vibrance runs in the Oklab stage of the edit, and is part of what makes two edits equal.
// Input: the test pixels at vibrance 0.6. The truth: the gray pixel kept, the others changed,
// and an edit differing only in vibrance or skin protection not equal.
TEST(ColorEdit, VibranceIsAStageOfTheEdit) {
    ColorEdit edit;
    edit.vibrance = 0.6;
    EXPECT_FALSE(edit.isIdentity());
    EXPECT_TRUE(edit.usesOklab());
    const ImageBuffer source = colours8();
    const ImageBuffer out = applyColor(source, edit);
    const auto* in = reinterpret_cast<const std::uint8_t*>(source.row(0));
    const auto* p = reinterpret_cast<const std::uint8_t*>(out.row(0));
    EXPECT_NEAR(p[8], 128, 1) << "gray kept";
    EXPECT_NEAR(p[9], 128, 1);
    EXPECT_NE(std::memcmp(p, in, 8), 0) << "colours changed";
    ColorEdit other = edit;
    other.protectSkin = false;
    EXPECT_FALSE(edit == other);
    other = edit;
    other.vibrance = 0.5;
    EXPECT_FALSE(edit == other);
}

TEST(ColorEdit, OutputLevelsLiftBlackAndLowerWhite) {
    const Curve curve = levelsCurve(0, 1, 1, 0.2, 0.9);
    EXPECT_NEAR(curve(0), 0.2F, 1e-6) << "black turns gray";
    EXPECT_NEAR(curve(1), 0.9F, 1e-6);
    EXPECT_NEAR(curve(0.5F), 0.55F, 1e-4);
    EXPECT_FALSE(curve.isIdentity());
}

TEST(ColorEdit, BrightnessMovesBlacksAndHighlightsAlikeAndContrastPivotsOnTheMiddle) {
    EXPECT_TRUE(lightnessCurve(0, 1, 1).isIdentity());
    const Curve brighter = lightnessCurve(0.1, 1, 1);
    EXPECT_NEAR(brighter(0), 0.1F, 1e-6);
    EXPECT_NEAR(brighter(0.5F), 0.6F, 1e-4);
    EXPECT_FLOAT_EQ(brighter(0.95F), 1) << "clipped at the top";
    const Curve stronger = lightnessCurve(0, 2, 1);
    EXPECT_NEAR(stronger(0.5F), 0.5F, 1e-4);
    EXPECT_NEAR(stronger(0.6F), 0.7F, 1e-4);
    EXPECT_NEAR(stronger(0.3F), 0.1F, 1e-4);

    // On a picture: black lifted to a neutral gray.
    const ImageBuffer black = ImageBuffer::create({1, 1}, PixelFormat::Bgrx8);
    std::memset(black.row(0), 0, 4);
    ColorEdit edit;
    edit.lightness = lightnessCurve(0.3, 1, 1);
    const ImageBuffer lifted = applyColor(black, edit);
    const auto* p = reinterpret_cast<const std::uint8_t*>(lifted.row(0));
    EXPECT_GT(p[0], 40);
    EXPECT_NEAR(p[0], p[1], 1);
    EXPECT_NEAR(p[1], p[2], 1);
}

namespace {

double oklabHue(const std::uint8_t* bgrx) {
    const auto decode = [](int v) {
        const double c = v / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    const Oklab lab = linearToOklab(decode(bgrx[2]), decode(bgrx[1]), decode(bgrx[0]));
    return std::atan2(lab.b, lab.a);
}

}  // namespace

TEST(ColorEdit, StrongColoursKeepTheirHueWhenFittedByChroma) {
    // A saturated sky blue and a barley yellow, at twice the chroma.
    const ImageBuffer image = ImageBuffer::create({3, 1}, PixelFormat::Bgrx8);
    const std::uint8_t bgrx[] = {230, 140, 40, 0, 40, 200, 230, 0, 128, 128, 128, 0};
    std::memcpy(image.row(0), bgrx, sizeof(bgrx));
    ColorEdit edit;
    edit.chroma = 2;
    const ImageBuffer fitted = applyColor(image, edit);
    edit.gamutMap = false;
    const ImageBuffer clipped = applyColor(image, edit);
    const auto* f = reinterpret_cast<const std::uint8_t*>(fitted.row(0));
    const auto* c = reinterpret_cast<const std::uint8_t*>(clipped.row(0));
    for (int i = 0; i < 2; ++i) {
        const double before = oklabHue(bgrx + i * 4);
        const double fittedTurn = std::abs(std::remainder(oklabHue(f + i * 4) - before, 2 * std::numbers::pi));
        const double clippedTurn = std::abs(std::remainder(oklabHue(c + i * 4) - before, 2 * std::numbers::pi));
        EXPECT_LT(fittedTurn, 0.03) << "pixel " << i;
        EXPECT_GT(clippedTurn, fittedTurn) << "pixel " << i;
    }
    EXPECT_EQ(f[8], 128) << "gray is untouched";

    // Colours that stay inside are the same either way.
    const ImageBuffer muted = ImageBuffer::create({1, 1}, PixelFormat::Bgrx8);
    const std::uint8_t tan[] = {100, 120, 150, 0};
    std::memcpy(muted.row(0), tan, 4);
    edit.chroma = 1.2;
    edit.gamutMap = false;
    const ImageBuffer mild = applyColor(muted, edit);
    edit.gamutMap = true;
    const ImageBuffer mildFitted = applyColor(muted, edit);
    EXPECT_NE(std::memcmp(mild.row(0), tan, 3), 0) << "the edit did something";
    EXPECT_EQ(std::memcmp(mild.row(0), mildFitted.row(0), 4), 0);
}
