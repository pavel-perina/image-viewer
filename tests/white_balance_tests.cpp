#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "core/color.h"
#include "core/color_edit.h"
#include "core/oklab.h"
#include "core/white_balance.h"

using namespace iv;

namespace {

using Rgb = std::array<double, 3>;

const Matrix3 kSrgb = rgbToXyz(kSrgbPrimaries);

// One float pixel of linear values, encoded as stored.
ImageBuffer floatPixel(const Rgb& linear) {
    const ImageBuffer image = ImageBuffer::create({1, 1}, PixelFormat::Rgba32F);
    auto* p = reinterpret_cast<float*>(image.row(0));
    for (std::size_t c = 0; c < 3; ++c) {
        p[c] = static_cast<float>(srgbEncode(linear[c]));
    }
    p[3] = 1;
    return image;
}

Rgb linearOf(const ImageBuffer& image) {
    const auto* p = reinterpret_cast<const float*>(image.row(0));
    return {srgbDecode(p[0]), srgbDecode(p[1]), srgbDecode(p[2])};
}

ColorEdit balanceEdit(WhiteBalance balance, const Matrix3& space = kSrgb) {
    ColorEdit edit;
    edit.whiteBalance = balance;
    edit.workingSpace = space;
    return edit;
}

// A gray as the light of that white balance would have tinted it: the correction undone.
Rgb tintedGray(WhiteBalance balance, const Matrix3& space, double level = 0.18) {
    return whiteBalanceMatrix(balance, space).inverse() * Rgb{level, level, level};
}

void expectNeutral(const Rgb& c, double tolerance = 1e-5) {
    EXPECT_NEAR(c[0], c[1], tolerance);
    EXPECT_NEAR(c[1], c[2], tolerance);
}

}  // namespace

// Why: the temperature and tint are placed on the published locus; a wrong formula shifts every
// correction. Input: 2856 K, CIE illuminant A, and 6500 K. The truth: A's chromaticity
// (x 0.4476, y 0.4074) and the black body's at 6500 K (x 0.3135, y 0.3237) in CIE 1960 uv;
// D65 (v 0.3122) lies just above the locus, which is why white balance is placed relative to it.
TEST(WhiteBalance, LocusPointsMatchKnownWhites) {
    const auto a = planckianUv(2856);
    EXPECT_NEAR(a[0], 0.2560, 5e-4);
    EXPECT_NEAR(a[1], 0.3495, 5e-4);
    const auto body = planckianUv(6500);
    EXPECT_NEAR(body[0], 0.2004, 5e-4);
    EXPECT_NEAR(body[1], 0.3104, 5e-4);
    EXPECT_LT(body[1], 0.3122);
}

// Why: a section left alone must cost nothing and change nothing. Input: 6500 K and tint 0 on
// every format. The truth: the identity matrix, and the same buffer back, untouched.
TEST(WhiteBalance, SixtyFiveHundredKelvinAndNoTintChangeNothing) {
    EXPECT_TRUE(WhiteBalance{}.isIdentity());
    EXPECT_EQ(whiteBalanceMatrix({}, kSrgb), Matrix3{});
    const ColorEdit edit = balanceEdit({});
    EXPECT_TRUE(edit.isIdentity());
    for (const PixelFormat format : {PixelFormat::Bgrx8, PixelFormat::Rgba16, PixelFormat::Rgba32F}) {
        const ImageBuffer image = ImageBuffer::create({2, 2}, format);
        EXPECT_EQ(applyColor(image, edit).data(), image.data());
    }
}

// Why: the numbers must move the picture the way other programs do. Input: a mid gray at 3200 K,
// 9000 K and tints of +50 and -50. The truth: tungsten makes it bluer, 9000 K warmer, a positive
// tint more magenta (green below red and blue) and a negative one greener.
TEST(WhiteBalance, DirectionsReadAsInOtherPrograms) {
    const Rgb gray{0.2, 0.2, 0.2};
    const Rgb tungsten = whiteBalanceMatrix({3200, 0}, kSrgb) * gray;
    EXPECT_GT(tungsten[2], tungsten[0] + 0.02);
    const Rgb shade = whiteBalanceMatrix({9000, 0}, kSrgb) * gray;
    EXPECT_GT(shade[0], shade[2] + 0.01);
    const Rgb magenta = whiteBalanceMatrix({6500, 50}, kSrgb) * gray;
    EXPECT_LT(magenta[1], magenta[0]);
    EXPECT_LT(magenta[1], magenta[2]);
    const Rgb green = whiteBalanceMatrix({6500, -50}, kSrgb) * gray;
    EXPECT_GT(green[1], green[0]);
    EXPECT_GT(green[1], green[2]);
}

// Why: the point of the stage: a gray under known light comes back neutral, and Pick gray
// finds that light from the gray alone. Input: grays tinted by several temperatures and tints,
// as floats, through applyColor. The truth: equal R, G and B out, and the temperature and tint
// that made them, found again.
TEST(WhiteBalance, AGrayUnderKnownLightComesBackNeutralAndPickingFindsTheLight) {
    for (const WhiteBalance balance : {WhiteBalance{2800, 0}, WhiteBalance{4000, 30}, WhiteBalance{9500, -40},
                                       WhiteBalance{14000, 80}}) {
        const Rgb tinted = tintedGray(balance, kSrgb);
        expectNeutral(linearOf(applyColor(floatPixel(tinted), balanceEdit(balance))));
        const WhiteBalanceFit found = whiteBalanceFor(tinted, kSrgb);
        EXPECT_NEAR(found.balance.kelvin, balance.kelvin, balance.kelvin * 1e-4) << balance.kelvin;
        EXPECT_NEAR(found.balance.tint, balance.tint, 0.01) << balance.kelvin;
        EXPECT_FALSE(found.clamped);
    }
    const WhiteBalanceFit neutral = whiteBalanceFor({0.3, 0.3, 0.3}, kSrgb);
    EXPECT_NEAR(neutral.balance.kelvin, 6500, 1);
    EXPECT_NEAR(neutral.balance.tint, 0, 0.01);
}

// Why: a picked colour far beyond the range must still give usable values, and say so.
// Input: a strongly green colour, and a gray under 1200 K light. The truth: values at the ends
// of the range, marked as clamped: green light takes the most magenta tint, which corrects it.
TEST(WhiteBalance, PickingBeyondTheRangeStopsAtItsEnds) {
    const WhiteBalanceFit green = whiteBalanceFor({0.1, 0.5, 0.1}, kSrgb);
    EXPECT_TRUE(green.clamped);
    EXPECT_EQ(green.balance.tint, kMaxTint);
    const WhiteBalanceFit candle = whiteBalanceFor(tintedGray({1200, 0}, kSrgb), kSrgb);
    EXPECT_TRUE(candle.clamped);
    EXPECT_EQ(candle.balance.kelvin, kMinKelvin);
    EXPECT_FALSE(whiteBalanceFor(tintedGray({2100, 0}, kSrgb), kSrgb).clamped);
}

// Why: floats keep what lies beyond the range, so the correction must be undoable on them.
// Input: colours, some beyond 0 to 1, through the matrix of a white and then its inverse. The
// truth: the colours that went in, within float precision.
TEST(WhiteBalance, TheAdaptationAndItsInverseGiveBackUnclippedFloats) {
    const Matrix3 m = whiteBalanceMatrix({3500, 25}, kSrgb);
    const Matrix3 back = m.inverse();
    for (const Rgb c : {Rgb{0.5, 0.2, 0.9}, Rgb{1.4, 0.1, -0.05}, Rgb{0.0, 0.0, 0.0}}) {
        const ImageBuffer once = applyColor(floatPixel(c), balanceEdit({3500, 25}));
        const Rgb restored = back * linearOf(once);
        for (std::size_t i = 0; i < 3; ++i) {
            EXPECT_NEAR(restored[i], c[i], 1e-5);
        }
    }
}

// Why: a wide-gamut picture is to be corrected in its own primaries, not as if it were sRGB.
// Input: the matrix of an Adobe RGB profile, as a picture would carry it, and a gray in it
// tinted by 3000 K. The truth: neutral again with 3000 K set in that space, and picking finds
// 3000 K there; the sRGB matrix taken for it would leave the gray tinted.
TEST(WhiteBalance, AWideGamutPictureIsCorrectedInItsOwnPrimaries) {
    const auto adobe = profileRgbToXyz(adobeRgbProfile());
    ASSERT_TRUE(adobe);
    const WhiteBalance balance{3000, 10};
    const Rgb tinted = tintedGray(balance, *adobe);
    expectNeutral(whiteBalanceMatrix(balance, *adobe) * tinted);
    EXPECT_NEAR(whiteBalanceFor(tinted, *adobe).balance.kelvin, 3000, 1);
    const Rgb wrong = whiteBalanceMatrix(balance, kSrgb) * tinted;
    EXPECT_GT(std::abs(wrong[0] - wrong[2]), 1e-3);
}

// Why: profiles keep their colorants adapted to D50; read as they are, a white moved by the
// locus's offsets from there lands beyond the colours that exist, and warm temperatures blew
// up and flipped to black and yellow. Most camera JPEGs carry an sRGB profile. Input: an sRGB
// profile made by the program, and none. The truth: the matrix of sRGB's own primaries, white
// at D65, and nothing without a profile.
TEST(WhiteBalance, AnSrgbProfileMeansWhatSrgbMeans) {
    const auto srgb = profileRgbToXyz(makeRgbProfile("sRGB", kSrgbPrimaries, 0));
    ASSERT_TRUE(srgb);
    for (std::size_t i = 0; i < 9; ++i) {
        EXPECT_NEAR(srgb->m[i], kSrgb.m[i], 2e-3) << i;
    }
    const Rgb d65 = kSrgb * Rgb{1, 1, 1};
    EXPECT_NEAR(d65[0], 0.9505, 1e-3);
    EXPECT_NEAR(d65[2], 1.089, 1e-3);
    EXPECT_FALSE(profileRgbToXyz({}));
}

// Why: white balance must change colour, not wreck it: anywhere on the slider no colour the
// picture holds may turn negative, which clipping shows as colours lost to blue, black or
// yellow, and a neutral keeps its brightness. Input: the corners and middle of the RGB cube at
// temperatures across the range and the strongest tints, in sRGB and in Adobe RGB. The truth:
// no negative channel, and a mid gray at the same luminance.
TEST(WhiteBalance, NoColourTurnsNegativeAndNeutralsKeepTheirBrightness) {
    const auto adobe = profileRgbToXyz(adobeRgbProfile());
    ASSERT_TRUE(adobe);
    for (const Matrix3& space : {kSrgb, *adobe}) {
        const Rgb weights{space.m[3], space.m[4], space.m[5]};
        for (const double kelvin : {kMinKelvin, 2500.0, 3200.0, 5000.0, 9000.0, kMaxKelvin}) {
            for (const double tint : {-kMaxTint, 0.0, kMaxTint}) {
                const Matrix3 m = whiteBalanceMatrix({kelvin, tint}, space);
                for (const Rgb c : {Rgb{1, 0, 0}, Rgb{0, 1, 0}, Rgb{0, 0, 1}, Rgb{1, 1, 0}, Rgb{0, 1, 1}, Rgb{1, 0, 1},
                                    Rgb{0.5, 0.3, 0.1}}) {
                    for (const double v : m * c) {
                        EXPECT_GE(v, 0) << kelvin << " " << tint;
                    }
                }
                const Rgb gray = m * Rgb{0.2, 0.2, 0.2};
                const double luminance = gray[0] * weights[0] + gray[1] * weights[1] + gray[2] * weights[2];
                EXPECT_NEAR(luminance, 0.2, 1e-9) << kelvin << " " << tint;
            }
        }
    }
}

// Why: an integer format cannot hold what the correction pushes beyond its range; fitting keeps
// the colour's luminance and hue direction instead of clipping channels apart. Input: colours
// beyond 0 to 1. The truth: inside the range, at the same luminance, on the line to their gray.
TEST(WhiteBalance, FittingByChromaKeepsLuminance) {
    const Rgb weights{kSrgb.m[3], kSrgb.m[4], kSrgb.m[5]};
    for (const Rgb c : {Rgb{1.3, 0.5, 0.2}, Rgb{-0.1, 0.4, 0.6}}) {
        const Rgb fitted = fitByChroma(c, weights);
        const auto luminance = [&](const Rgb& x) { return x[0] * weights[0] + x[1] * weights[1] + x[2] * weights[2]; };
        EXPECT_NEAR(luminance(fitted), luminance(c), 1e-9);
        for (const double v : fitted) {
            EXPECT_GE(v, -1e-9);
            EXPECT_LE(v, 1 + 1e-9);
        }
    }
    const Rgb inside{0.2, 0.5, 0.7};
    EXPECT_EQ(fitByChroma(inside, weights), inside);
}

// Why: a gray picture has no colour to correct. Input: an 8-bit gray pixel and a strong white
// balance. The truth: the same buffer back.
TEST(WhiteBalance, AGrayPictureIsLeftAlone) {
    const ImageBuffer gray = ImageBuffer::create({1, 1}, PixelFormat::Gray8);
    *reinterpret_cast<std::uint8_t*>(gray.row(0)) = 100;
    EXPECT_EQ(applyColor(gray, balanceEdit({3000, 50})).data(), gray.data());
}

// Why: 8-bit pictures are most of what is corrected, and must come out right in their own
// format. Input: an 8-bit gray tinted by 4000 K. The truth: within a step of neutral.
TEST(WhiteBalance, EightBitGraysComeBackNeutral) {
    const Rgb tinted = tintedGray({4000, 0}, kSrgb, 0.3);
    const ImageBuffer image = ImageBuffer::create({1, 1}, PixelFormat::Bgrx8);
    auto* p = reinterpret_cast<std::uint8_t*>(image.row(0));
    for (std::size_t c = 0; c < 3; ++c) {
        p[2 - c] = static_cast<std::uint8_t>(std::lround(srgbEncode(tinted[c]) * 255));
    }
    const ImageBuffer out = applyColor(image, balanceEdit({4000, 0}));
    const auto* q = reinterpret_cast<const std::uint8_t*>(out.row(0));
    EXPECT_NEAR(q[0], q[1], 1);
    EXPECT_NEAR(q[1], q[2], 1);
}

