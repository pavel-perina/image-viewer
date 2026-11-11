#include <gtest/gtest.h>

#include <lcms2.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#include "core/color.h"
#include "core/exif.h"
#include "core/image_details.h"
#include "core/image_ops.h"
#include "core/image_source.h"
#include "core/thumbnail.h"
#include "test_jpeg.h"
#include "test_tiff.h"

using namespace iv;

namespace {

// Everything is checked against the published formulas instead of lcms, which would only show
// that lcms agrees with itself.
double srgbEncode(double linear) {
    linear = std::clamp(linear, 0.0, 1.0);
    return linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
}

double adobeDecode(double encoded) { return std::pow(encoded, 563.0 / 256.0); }

// What Adobe RGB values, from 0 to 1, are in 8-bit sRGB.
std::array<int, 3> adobeInSrgb(double r, double g, double b) {
    const double lr = adobeDecode(r);
    const double lg = adobeDecode(g);
    const double lb = adobeDecode(b);
    return {static_cast<int>(std::lround(255 * srgbEncode(1.3983557 * lr - 0.3983557 * lg))),
            static_cast<int>(std::lround(255 * srgbEncode(lg))),
            static_cast<int>(std::lround(255 * srgbEncode(-0.0429290 * lg + 1.0429290 * lb)))};
}

std::array<int, 3> shownAt(const ImageBuffer& image, int x, int y) {
    EXPECT_EQ(image.format(), PixelFormat::Bgrx8);
    const auto* p = reinterpret_cast<const std::uint8_t*>(image.row(y)) + static_cast<std::size_t>(x) * 4;
    EXPECT_EQ(p[3], 255);
    return {p[2], p[1], p[0]};
}

void expectNear(const std::array<int, 3>& actual, const std::array<int, 3>& expected, int tolerance) {
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(actual[static_cast<std::size_t>(i)], expected[static_cast<std::size_t>(i)], tolerance) << "channel " << i;
    }
}

ImageBuffer onePixel8(int r, int g, int b) {
    const ImageBuffer image = ImageBuffer::create({1, 1}, PixelFormat::Bgrx8);
    auto* p = reinterpret_cast<std::uint8_t*>(image.row(0));
    p[0] = static_cast<std::uint8_t>(b);
    p[1] = static_cast<std::uint8_t>(g);
    p[2] = static_cast<std::uint8_t>(r);
    p[3] = 255;
    return image;
}

SubimageInfo rgbImage(IccBytes profile, PixelFormat format = PixelFormat::Bgrx8) {
    SubimageInfo info;
    info.format = format;
    info.stored.colorModel = "RGB";
    info.iccProfile = std::move(profile);
    return info;
}

IccBytes grayProfile(double gamma) {
    cmsToneCurve* const curve = cmsBuildGamma(nullptr, gamma);
    cmsHPROFILE const profile = cmsCreateGrayProfile(cmsD50_xyY(), curve);
    cmsFreeToneCurve(curve);
    cmsUInt32Number size = 0;
    cmsSaveProfileToMem(profile, nullptr, &size);
    IccBytes bytes(size);
    cmsSaveProfileToMem(profile, bytes.data(), &size);
    cmsCloseProfile(profile);
    return bytes;
}

std::filesystem::path testFile(const char* name) {
    static const std::filesystem::path folder = [] {
        auto path = std::filesystem::temp_directory_path() / ("iv_color_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(path);
        return path;
    }();
    return folder / name;
}

}  // namespace

TEST(Color, AdobeRgbIsConvertedLikeThePublishedFormula) {
    const auto transform = displayTransform(rgbImage(adobeRgbProfile()), {});
    ASSERT_NE(transform, nullptr);
    for (const auto& [r, g, b] : std::vector<std::array<int, 3>>{{128, 128, 128}, {60, 200, 90}, {200, 150, 100}, {30, 40, 220}, {240, 240, 10}}) {
        expectNear(shownAt(transform->apply(onePixel8(r, g, b)), 0, 0), adobeInSrgb(r / 255.0, g / 255.0, b / 255.0), 2);
    }
    // Pure Adobe green lies outside sRGB and clips to the edge of it.
    expectNear(shownAt(transform->apply(onePixel8(0, 255, 0)), 0, 0), {0, 255, 0}, 1);
}

TEST(Color, SixteenBitsAreConvertedAtFullPrecisionToo) {
    const auto transform = displayTransform(rgbImage(adobeRgbProfile(), PixelFormat::Rgba16), {});
    ASSERT_NE(transform, nullptr);
    // Two values a 16-bit step apart, which 8 bits would merge, stay apart.
    const ImageBuffer image = ImageBuffer::create({3, 1}, PixelFormat::Rgba16);
    auto* in = reinterpret_cast<std::uint16_t*>(image.row(0));
    const std::array<std::array<std::uint16_t, 3>, 3> pixels{{{32768, 32768, 32768}, {32800, 32800, 32800}, {15420, 51400, 23130}}};
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        in[4 * i] = pixels[i][0];
        in[4 * i + 1] = pixels[i][1];
        in[4 * i + 2] = pixels[i][2];
        in[4 * i + 3] = 65535;
    }
    const ImageBuffer shown = toDisplayWide(image, fullRange(PixelFormat::Rgba16), transform.get());
    ASSERT_EQ(shown.format(), PixelFormat::Rgba16);
    const auto* out = reinterpret_cast<const std::uint16_t*>(shown.row(0));
    EXPECT_LT(out[0], out[4]);
    EXPECT_EQ(out[3], 65535);
    // And they agree with the conversion that ends in 8 bits.
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        const std::array<int, 3> expected = adobeInSrgb(pixels[i][0] / 65535.0, pixels[i][1] / 65535.0, pixels[i][2] / 65535.0);
        expectNear({(out[4 * i] + 128) / 257, (out[4 * i + 1] + 128) / 257, (out[4 * i + 2] + 128) / 257}, expected, 2);
    }
}

TEST(Color, ADisplayProfileReceivesSrgb) {
    ColorTarget target;
    target.displayProfile = std::make_shared<const IccBytes>(adobeRgbProfile());
    const auto transform = displayTransform(rgbImage({}), target);
    ASSERT_NE(transform, nullptr);
    // A well known value: sRGB red in Adobe RGB.
    expectNear(shownAt(transform->apply(onePixel8(255, 0, 0)), 0, 0), {219, 0, 0}, 1);
}

TEST(Color, SrgbNeedsNoConversionAtAll) {
    EXPECT_EQ(displayTransform(rgbImage({}), {}), nullptr);
    // However it is labelled, and even though it is not byte for byte lcms' own.
    EXPECT_EQ(displayTransform(rgbImage(makeRgbProfile("My sRGB", kSrgbPrimaries, 0)), {}), nullptr);

    // The same display as the image, whichever it is.
    ColorTarget target;
    target.displayProfile = std::make_shared<const IccBytes>(adobeRgbProfile());
    EXPECT_EQ(displayTransform(rgbImage(adobeRgbProfile()), target), nullptr);

    // And an 8-bit image then comes through without a copy.
    const ImageBuffer image = onePixel8(1, 2, 3);
    EXPECT_EQ(toDisplay(image, fullRange(image.format()), nullptr).data(), image.data());
}

TEST(Color, OffMeansOff) {
    ColorTarget target;
    target.enabled = false;
    target.displayProfile = std::make_shared<const IccBytes>(adobeRgbProfile());
    EXPECT_EQ(displayTransform(rgbImage(adobeRgbProfile()), target), nullptr);
}

TEST(Color, UnusableProfilesCountAsSrgb) {
    EXPECT_EQ(displayTransform(rgbImage({1, 2, 3, 4}), {}), nullptr);
    IccBytes truncated = adobeRgbProfile();
    truncated.resize(truncated.size() / 2);
    EXPECT_EQ(displayTransform(rgbImage(truncated), {}), nullptr);
    // A gray profile says nothing about colour pixels.
    EXPECT_EQ(displayTransform(rgbImage(grayProfile(1.0)), {}), nullptr);
}

TEST(Color, GrayProfileAppliesToGrayPixels) {
    SubimageInfo info = rgbImage(grayProfile(1.0));
    info.stored.colorModel = "Grayscale";
    const auto transform = displayTransform(info, {});
    ASSERT_NE(transform, nullptr);
    const int expected = static_cast<int>(std::lround(255 * srgbEncode(128 / 255.0)));
    expectNear(shownAt(transform->apply(onePixel8(128, 128, 128)), 0, 0), {expected, expected, expected}, 2);
}

TEST(Color, SixteenBitsConvertWithoutGoingThroughEightFirst) {
    const auto transform = displayTransform(rgbImage(adobeRgbProfile(), PixelFormat::Rgba16), {});
    ASSERT_NE(transform, nullptr);
    const ImageBuffer image = ImageBuffer::create({1, 1}, PixelFormat::Rgba16);
    const std::array<std::uint16_t, 4> pixel{15000, 51000, 30000, 65535};
    std::memcpy(image.row(0), pixel.data(), sizeof(pixel));
    const auto result = shownAt(toDisplay(image, fullRange(image.format()), transform.get()), 0, 0);
    expectNear(result, adobeInSrgb(15000 / 65535.0, 51000 / 65535.0, 30000 / 65535.0), 1);

    // Reds that all round to 120 in 8 bits, which converting directly can still tell apart.
    const ImageBuffer ramp = ImageBuffer::create({257, 1}, PixelFormat::Rgba16);
    auto* in = reinterpret_cast<std::uint16_t*>(ramp.row(0));
    for (int x = 0; x < ramp.width(); ++x, in += 4) {
        in[0] = static_cast<std::uint16_t>(120 * 257 - 128 + x);
        in[1] = 0;
        in[2] = 0;
        in[3] = 65535;
    }
    const ImageBuffer out = toDisplay(ramp, fullRange(ramp.format()), transform.get());
    EXPECT_NE(shownAt(out, 0, 0)[0], shownAt(out, ramp.width() - 1, 0)[0]);
}

TEST(Color, StretchedSixteenBitsStillConvert) {
    const auto transform = displayTransform(rgbImage(adobeRgbProfile(), PixelFormat::Rgba16), {});
    ASSERT_NE(transform, nullptr);
    const ImageBuffer image = ImageBuffer::create({1, 1}, PixelFormat::Rgba16);
    const std::array<std::uint16_t, 4> pixel{5000, 20000, 10000, 65535};
    std::memcpy(image.row(0), pixel.data(), sizeof(pixel));
    const DisplayRange range{0, 25000};
    const auto stretched = shownAt(toDisplay(image, range), 0, 0);
    const auto expected = adobeInSrgb(stretched[0] / 255.0, stretched[1] / 255.0, stretched[2] / 255.0);
    expectNear(shownAt(toDisplay(image, range, transform.get()), 0, 0), expected, 2);
}

TEST(Color, FloatsConvertAtFullRangeOnly) {
    const auto transform = displayTransform(rgbImage(adobeRgbProfile(), PixelFormat::Rgba32F), {});
    ASSERT_NE(transform, nullptr);
    const ImageBuffer image = ImageBuffer::create({1, 1}, PixelFormat::Rgba32F);
    const std::array<float, 4> pixel{0.2F, 0.8F, 0.4F, 1.0F};
    std::memcpy(image.row(0), pixel.data(), sizeof(pixel));
    expectNear(shownAt(toDisplay(image, fullRange(image.format()), transform.get()), 0, 0), adobeInSrgb(0.2, 0.8, 0.4), 1);

    // Stretched, the numbers are data, so the transform is left out.
    const DisplayRange range{0, 2};
    expectNear(shownAt(toDisplay(image, range, transform.get()), 0, 0), shownAt(toDisplay(image, range), 0, 0), 0);
}

TEST(Color, GrayDataIsLeftAlone) {
    SubimageInfo info = rgbImage(adobeRgbProfile(), PixelFormat::Gray16);
    EXPECT_EQ(displayTransform(info, {}), nullptr);
    info.format = PixelFormat::Gray32F;
    EXPECT_EQ(displayTransform(info, {}), nullptr);
}

TEST(Color, OneTransformServesManyThreads) {
    const auto transform = displayTransform(rgbImage(adobeRgbProfile()), {});
    ASSERT_NE(transform, nullptr);
    const ImageBuffer image = ImageBuffer::create({256, 64}, PixelFormat::Bgrx8);
    for (int y = 0; y < image.height(); ++y) {
        auto* p = reinterpret_cast<std::uint8_t*>(image.row(y));
        for (int x = 0; x < image.width(); ++x, p += 4) {
            p[0] = static_cast<std::uint8_t>(x);
            p[1] = static_cast<std::uint8_t>(y * 4);
            p[2] = static_cast<std::uint8_t>(255 - x);
            p[3] = 255;
        }
    }
    const ImageBuffer reference = transform->apply(image);
    std::vector<std::jthread> threads;
    std::array<bool, 4> same{};
    for (std::size_t t = 0; t < same.size(); ++t) {
        threads.emplace_back([&, t] {
            bool ok = true;
            for (int i = 0; i < 20; ++i) {
                const ImageBuffer out = transform->apply(image);
                for (int y = 0; y < out.height(); ++y) {
                    ok = ok && std::memcmp(out.row(y), reference.row(y), static_cast<std::size_t>(out.width()) * 4) == 0;
                }
            }
            same[t] = ok;
        });
    }
    threads.clear();
    for (const bool ok : same) {
        EXPECT_TRUE(ok);
    }
}

TEST(Color, CachesSoAFolderDoesNotRebuildIt) {
    const IccBytes adobe = adobeRgbProfile();
    const auto first = displayTransform(rgbImage(adobe), {});
    EXPECT_EQ(first, displayTransform(rgbImage(adobe), {}));
    // But another display or format is another transform.
    ColorTarget target;
    target.displayProfile = std::make_shared<const IccBytes>(makeRgbProfile("Some display", kSrgbPrimaries, 2.6));
    EXPECT_NE(first, displayTransform(rgbImage(adobe), target));
    EXPECT_NE(first, displayTransform(rgbImage(adobe, PixelFormat::Rgba16), {}));
}

TEST(ColorProfiles, NamesComeFromTheProfile) {
    EXPECT_EQ(iccDescription(adobeRgbProfile()), "Adobe RGB (1998)");
    EXPECT_EQ(iccDescription(IccBytes{1, 2, 3, 4}), "");
    EXPECT_EQ(iccDescription(IccBytes{}), "");
}

TEST(ColorProfiles, Cicp) {
    EXPECT_TRUE(profileFromCicp(1, 13).empty());   // sRGB
    EXPECT_TRUE(profileFromCicp(1, 1).empty());    // BT.709
    EXPECT_TRUE(profileFromCicp(2, 2).empty());    // unspecified
    EXPECT_TRUE(profileFromCicp(9, 16).empty());   // HDR, which a profile cannot express
    EXPECT_TRUE(profileFromCicp(9, 18).empty());
    EXPECT_TRUE(profileFromCicp(77, 13).empty());  // unknown
    EXPECT_EQ(iccDescription(profileFromCicp(12, 13)), "Display P3");
    EXPECT_EQ(iccDescription(profileFromCicp(9, 1)), "BT.2020");
    EXPECT_EQ(iccDescription(profileFromCicp(1, 8)), "sRGB primaries, linear");

    // P3 red is more saturated than sRGB can show, so green and blue of sRGB go negative and
    // clip: P3 (255,0,0) stays (255,0,0), while a mid gray stays gray.
    const auto p3 = displayTransform(rgbImage(profileFromCicp(12, 13)), {});
    ASSERT_NE(p3, nullptr);
    expectNear(shownAt(p3->apply(onePixel8(255, 0, 0)), 0, 0), {255, 0, 0}, 1);
    expectNear(shownAt(p3->apply(onePixel8(128, 128, 128)), 0, 0), {128, 128, 128}, 1);

    // Linear with sRGB's primaries is just the curve.
    const auto linear = displayTransform(rgbImage(profileFromCicp(1, 8)), {});
    ASSERT_NE(linear, nullptr);
    const int mid = static_cast<int>(std::lround(255 * srgbEncode(128 / 255.0)));
    expectNear(shownAt(linear->apply(onePixel8(128, 128, 128)), 0, 0), {mid, mid, mid}, 1);
}

TEST(ColorProfiles, PngGamma) {
    EXPECT_TRUE(profileFromPngGamma(0, nullptr).empty());
    EXPECT_TRUE(profileFromPngGamma(0.45455, nullptr).empty());  // what encoders write for sRGB
    EXPECT_TRUE(profileFromPngGamma(0.45455, &kSrgbPrimaries).empty());
    EXPECT_FALSE(profileFromPngGamma(1.0, nullptr).empty());
    EXPECT_FALSE(profileFromPngGamma(0.45455, &kAdobeRgbPrimaries).empty());

    const auto linear = displayTransform(rgbImage(profileFromPngGamma(1.0, nullptr)), {});
    ASSERT_NE(linear, nullptr);
    const int mid = static_cast<int>(std::lround(255 * srgbEncode(128 / 255.0)));
    expectNear(shownAt(linear->apply(onePixel8(128, 128, 128)), 0, 0), {mid, mid, mid}, 1);
}

TEST(ColorProfiles, DisplayProfilesAreChecked) {
    EXPECT_EQ(displayProfileProblem(adobeRgbProfile()), "");
    EXPECT_NE(displayProfileProblem(IccBytes{1, 2, 3, 4}), "");
    EXPECT_NE(displayProfileProblem(grayProfile(2.2)), "");
}

TEST(ColorProfiles, AreReadableDetailsOfAJpeg) {
    test::JpegOptions options;
    options.iccProfile = adobeRgbProfile();
    const auto path = testFile("adobe.jpg");
    test::writeFile(path, test::encodeJpeg(options));
    const auto source = openImageSource(path);
    ASSERT_TRUE(source.has_value());
    bool found = false;
    for (const DetailRow& row : describeImage((*source)->subimages()[0])) {
        if (row.label == "Profile") {
            EXPECT_EQ(row.value, "Adobe RGB (1998)");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(ColorProfiles, DamagedProfileStillShowsThatOneExists) {
    SubimageInfo info = rgbImage({1, 2, 3, 4});
    bool found = false;
    for (const DetailRow& row : describeImage(info)) {
        if (row.label == "Profile") {
            EXPECT_EQ(row.value, "Embedded");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

namespace {

// A little-endian EXIF block with a colour space in the EXIF IFD and an interop index.
std::vector<std::uint8_t> exifWithColorSpace(int colorSpace, const char* interop) {
    std::vector<std::uint8_t> out{'E', 'x', 'i', 'f', 0, 0};
    const std::size_t base = out.size();
    const auto u16 = [&](unsigned v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const auto u32 = [&](unsigned v) {
        u16(v & 0xFFFF);
        u16(v >> 16);
    };
    out.push_back('I');
    out.push_back('I');
    u16(42);
    u32(8);
    u16(1);  // IFD0 at 8: a pointer to the EXIF IFD at 26
    u16(0x8769), u16(4), u32(1), u32(26);
    u32(0);
    EXPECT_EQ(out.size() - base, 26u);
    u16(2);  // EXIF IFD: colour space, and a pointer to the interop IFD at 56
    u16(0xA001), u16(3), u32(1), u32(static_cast<unsigned>(colorSpace));
    u16(0xA005), u16(4), u32(1), u32(56);
    u32(0);
    EXPECT_EQ(out.size() - base, 56u);
    u16(1);  // interop IFD: its index, which fits in the entry
    u16(0x0001), u16(2), u32(4);
    out.insert(out.end(), interop, interop + 3);
    out.push_back(0);
    u32(0);
    return out;
}

}  // namespace

TEST(ColorProfiles, ExifMarksAdobeRgbFromACamera) {
    EXPECT_TRUE(parseExif(exifWithColorSpace(0xFFFF, "R03")).adobeRgb);
    EXPECT_FALSE(parseExif(exifWithColorSpace(1, "R03")).adobeRgb);       // says sRGB outright
    EXPECT_FALSE(parseExif(exifWithColorSpace(0xFFFF, "R98")).adobeRgb);  // the sRGB rule set

    test::JpegOptions options;
    options.exif = exifWithColorSpace(0xFFFF, "R03");
    const auto path = testFile("camera_adobe.jpg");
    test::writeFile(path, test::encodeJpeg(options));
    const auto source = openImageSource(path);
    ASSERT_TRUE(source.has_value());
    EXPECT_EQ(iccDescription((*source)->subimages()[0].iccProfile), "Adobe RGB (1998)");

    // An embedded profile has the final say.
    options.iccProfile = makeRgbProfile("Mine", kSrgbPrimaries, 2.0);
    const auto both = testFile("camera_both.jpg");
    test::writeFile(both, test::encodeJpeg(options));
    const auto bothSource = openImageSource(both);
    ASSERT_TRUE(bothSource.has_value());
    EXPECT_EQ(iccDescription((*bothSource)->subimages()[0].iccProfile), "Mine");
}

TEST(ColorThumbnails, FollowTheProfileAndTheTarget) {
    test::JpegOptions options;
    options.iccProfile = adobeRgbProfile();
    options.pixel = [](int, int) { return test::Rgb{60, 200, 90}; };
    const auto path = testFile("thumb.jpg");
    test::writeFile(path, test::encodeJpeg(options));

    const auto managed = makeThumbnail(path, 32, {});
    ASSERT_TRUE(managed.has_value());
    expectNear(shownAt(*managed, 10, 10), adobeInSrgb(60 / 255.0, 200 / 255.0, 90 / 255.0), 3);

    ColorTarget off;
    off.enabled = false;
    const auto unmanaged = makeThumbnail(path, 32, {}, off);
    ASSERT_TRUE(unmanaged.has_value());
    expectNear(shownAt(*unmanaged, 10, 10), {60, 200, 90}, 3);
}

TEST(ColorThumbnails, SixteenBitTiffWithAProfile) {
    test::TiffImage image;
    image.bits = 16;
    image.iccProfile = adobeRgbProfile();
    image.pixel = [](int, int) { return test::Rgb{60, 200, 90}; };
    const auto path = testFile("thumb.tif");
    test::writeTiff(path, {image});
    const auto thumbnail = makeThumbnail(path, 32, {});
    ASSERT_TRUE(thumbnail.has_value());
    expectNear(shownAt(*thumbnail, 10, 10), adobeInSrgb(60 / 255.0, 200 / 255.0, 90 / 255.0), 2);
}
