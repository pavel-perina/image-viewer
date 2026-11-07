#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <lcms2.h>

#include "core/exif.h"
#include "core/file_io.h"
#include "core/geometry.h"
#include "core/jpeg_transform.h"
#ifdef IV_HAVE_JXL
#include "core/jxl_transcode.h"
#endif
#include "test_exif.h"
#include "test_jpeg.h"

namespace iv {
namespace {

namespace fs = std::filesystem;
using test::DecodedJpeg;
using test::Rgb;

// Different in every direction, so that any wrong turn or mirror shows.
Rgb pattern(int x, int y) {
    const bool corner = x < 24 && y < 16;
    return {static_cast<std::uint8_t>(x * 3), static_cast<std::uint8_t>(y * 4), static_cast<std::uint8_t>(corner ? 240 : 30)};
}

Orientation asOrientation(JpegEdit edit) {
    switch (edit) {
    case JpegEdit::RotateClockwise:        return Orientation::Rotate90CW;
    case JpegEdit::RotateCounterclockwise: return Orientation::Rotate270CW;
    case JpegEdit::Rotate180:              return Orientation::Rotate180;
    case JpegEdit::FlipHorizontal:         return Orientation::MirrorHorizontal;
    case JpegEdit::FlipVertical:           return Orientation::MirrorVertical;
    case JpegEdit::ApplyOrientation:       return Orientation::Normal;
    }
    return Orientation::Normal;
}

// Turns decoded pixels the way an orientation shows them.
DecodedJpeg turned(const DecodedJpeg& image, Orientation orientation) {
    const Size size = displaySize({image.width, image.height}, orientation);
    const Affine map = displayFromStored({image.width, image.height}, orientation);
    DecodedJpeg out;
    out.width = size.width;
    out.height = size.height;
    out.pixels.resize(image.pixels.size());
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            double dx = 0;
            double dy = 0;
            map.apply(x + 0.5, y + 0.5, dx, dy);
            out.pixels[static_cast<std::size_t>(std::floor(dy)) * out.width + static_cast<std::size_t>(std::floor(dx))] = image.at(x, y);
        }
    }
    return out;
}

int largestDifference(const DecodedJpeg& a, const DecodedJpeg& b) {
    int largest = 0;
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        largest = std::max({largest, std::abs(a.pixels[i].r - b.pixels[i].r), std::abs(a.pixels[i].g - b.pixels[i].g),
                            std::abs(a.pixels[i].b - b.pixels[i].b)});
    }
    return largest;
}

Orientation orientationOf(const DecodedJpeg& image) {
    return image.exif.empty() ? Orientation::Normal : parseExif(image.exif).orientation;
}

constexpr JpegEdit kEdits[] = {JpegEdit::RotateClockwise, JpegEdit::RotateCounterclockwise, JpegEdit::Rotate180,
                               JpegEdit::FlipHorizontal,  JpegEdit::FlipVertical,           JpegEdit::ApplyOrientation};

void expectEditsShowAsExpected(bool progressive) {
    for (int value = 1; value <= 8; ++value) {
        const auto orientation = static_cast<Orientation>(value);
        const auto jpeg = test::encodeJpeg({.width = 64, .height = 48, .orientation = value, .progressive = progressive, .pixel = pattern});
        const DecodedJpeg original = test::decodeJpeg(jpeg);
        for (const JpegEdit edit : kEdits) {
            SCOPED_TRACE("orientation " + std::to_string(value) + ", edit " + std::to_string(static_cast<int>(edit)));
            const auto result = transformJpeg(jpeg, edit);
            ASSERT_TRUE(result) << result.error();
            if (orientation == Orientation::Normal && edit == JpegEdit::ApplyOrientation) {
                EXPECT_TRUE(result->empty());
                continue;
            }
            const DecodedJpeg edited = test::decodeJpeg(*result);
            const DecodedJpeg expected = turned(turned(original, orientation), asOrientation(edit));
            ASSERT_EQ(edited.width, expected.width);
            ASSERT_EQ(edited.height, expected.height);
            // Only rounding in the inverse DCT may differ, as the blocks are the same.
            EXPECT_LE(largestDifference(edited, expected), 4);
            EXPECT_EQ(orientationOf(edited), Orientation::Normal);
            EXPECT_EQ(edited.progressive, progressive);
        }
    }
}

TEST(JpegTransform, EveryEditOfEveryOrientationShowsAsExpected) { expectEditsShowAsExpected(false); }

TEST(JpegTransform, AProgressiveFileStaysProgressive) { expectEditsShowAsExpected(true); }

TEST(JpegTransform, AnUprightFileNeedsNoOrientationApplied) {
    const auto jpeg = test::encodeJpeg({.pixel = pattern});
    const auto result = transformJpeg(jpeg, JpegEdit::ApplyOrientation);
    ASSERT_TRUE(result);
    EXPECT_TRUE(result->empty());
}

// "Exif\0\0", then a little-endian TIFF structure: IFD0 with the orientation and a link to the
// EXIF directory, which holds the pixel dimensions, and IFD1 for a thumbnail.
std::vector<std::uint8_t> exifWithThumbnail() {
    std::vector<std::uint8_t> b{'E', 'x', 'i', 'f', 0, 0};
    const auto u16 = [&](unsigned v) { b.insert(b.end(), {std::uint8_t(v), std::uint8_t(v >> 8)}); };
    const auto u32 = [&](unsigned v) { u16(v & 0xFFFF); u16(v >> 16); };
    const auto entry = [&](unsigned tag, unsigned type, unsigned value) {
        u16(tag);
        u16(type);
        u32(1);
        if (type == 3) {
            u16(value);
            u16(0);
        } else {
            u32(value);
        }
    };
    b.insert(b.end(), {'I', 'I', 42, 0});
    u32(8);
    u16(2);                  // IFD0 at 8
    entry(0x0112, 3, 6);     // rotated 90 degrees clockwise
    entry(0x8769, 4, 38);    // the EXIF directory
    u32(68);                 // IFD1
    u16(2);                  // the EXIF directory at 38
    entry(0xA002, 4, 64);    // width, a LONG
    entry(0xA003, 3, 48);    // height, a SHORT
    u32(0);
    u32(0);                  // padding up to 68
    u16(1);                  // IFD1 at 68
    entry(0x0103, 3, 6);     // JPEG compression
    u32(0);
    return b;
}

TEST(JpegTransform, ResetsTheOrientationSwapsTheDimensionsAndDropsTheThumbnail) {
    const auto jpeg = test::encodeJpeg({.width = 64, .height = 48, .exif = exifWithThumbnail(), .pixel = pattern});
    const auto result = transformJpeg(jpeg, JpegEdit::ApplyOrientation);
    ASSERT_TRUE(result) << result.error();
    const DecodedJpeg edited = test::decodeJpeg(*result);
    EXPECT_EQ(edited.width, 48);
    EXPECT_EQ(edited.height, 64);
    ASSERT_EQ(edited.exif.size(), exifWithThumbnail().size());

    const auto at = [&](std::size_t offset) { return edited.exif[6 + offset] | edited.exif[6 + offset + 1] << 8; };
    EXPECT_EQ(at(10 + 8), 1);       // the orientation
    EXPECT_EQ(at(34), 0);           // the link to IFD1
    EXPECT_EQ(at(36), 0);
    EXPECT_EQ(at(40 + 8), 48);      // the width, now the old height
    EXPECT_EQ(at(52 + 8), 64);      // and the other way round
}

TEST(JpegTransform, AnEdgeThatWouldMoveMustEndOnAWholeBlock) {
    // 40 is not a multiple of 16, the block height with 4:2:0 chroma.
    const auto jpeg = test::encodeJpeg({.width = 64, .height = 40, .pixel = pattern});
    const DecodedJpeg original = test::decodeJpeg(jpeg);

    // The bottom edge would become the left one, which moves to x = 0.
    const auto clockwise = transformJpeg(jpeg, JpegEdit::RotateClockwise);
    ASSERT_FALSE(clockwise);
    EXPECT_NE(clockwise.error().find("multiple of 16"), std::string::npos) << clockwise.error();
    EXPECT_FALSE(transformJpeg(jpeg, JpegEdit::FlipVertical));
    EXPECT_FALSE(transformJpeg(jpeg, JpegEdit::Rotate180));

    // Here the bottom edge stays at the end, on the right.
    const auto counterclockwise = transformJpeg(jpeg, JpegEdit::RotateCounterclockwise);
    ASSERT_TRUE(counterclockwise) << counterclockwise.error();
    const DecodedJpeg edited = test::decodeJpeg(*counterclockwise);
    EXPECT_LE(largestDifference(edited, turned(original, Orientation::Rotate270CW)), 4);

    const auto mirrored = transformJpeg(jpeg, JpegEdit::FlipHorizontal);
    ASSERT_TRUE(mirrored) << mirrored.error();
    EXPECT_LE(largestDifference(test::decodeJpeg(*mirrored), turned(original, Orientation::MirrorHorizontal)), 4);
}

DecodedJpeg cropped(const DecodedJpeg& image, int left, int top, int width, int height) {
    DecodedJpeg out;
    out.width = width;
    out.height = height;
    for (int y = top; y < top + height; ++y) {
        for (int x = left; x < left + width; ++x) {
            out.pixels.push_back(image.at(x, y));
        }
    }
    return out;
}

TEST(JpegTransform, CanTrimAPartialEdgeThatWouldMove) {
    const auto jpeg = test::encodeJpeg({.width = 64, .height = 40, .orientation = 1, .pixel = pattern});
    const DecodedJpeg original = test::decodeJpeg(jpeg);

    // The partial bottom row of blocks would become the left column, so it goes.
    const auto clockwise = transformJpeg(jpeg, JpegEdit::RotateClockwise, true);
    ASSERT_TRUE(clockwise) << clockwise.error();
    const DecodedJpeg edited = test::decodeJpeg(*clockwise);
    ASSERT_EQ(edited.width, 32);
    ASSERT_EQ(edited.height, 64);
    EXPECT_LE(largestDifference(edited, cropped(turned(original, Orientation::Rotate90CW), 8, 0, 32, 64)), 4);

    const auto upsideDown = transformJpeg(jpeg, JpegEdit::Rotate180, true);
    ASSERT_TRUE(upsideDown) << upsideDown.error();
    const DecodedJpeg turnedOver = test::decodeJpeg(*upsideDown);
    ASSERT_EQ(turnedOver.width, 64);
    ASSERT_EQ(turnedOver.height, 32);
    EXPECT_LE(largestDifference(turnedOver, cropped(turned(original, Orientation::Rotate180), 0, 8, 64, 32)), 4);

    // An edge that stays where it is keeps its partial blocks.
    const auto counterclockwise = transformJpeg(jpeg, JpegEdit::RotateCounterclockwise, true);
    ASSERT_TRUE(counterclockwise) << counterclockwise.error();
    EXPECT_EQ(test::decodeJpeg(*counterclockwise).width, 40);
}

TEST(JpegTransform, KeepsTheColorProfile) {
    std::vector<std::uint8_t> profile(3000);
    for (std::size_t i = 0; i < profile.size(); ++i) {
        profile[i] = static_cast<std::uint8_t>(i * 7);
    }
    const auto jpeg = test::encodeJpeg({.iccProfile = profile, .pixel = pattern});
    const auto result = transformJpeg(jpeg, JpegEdit::Rotate180);
    ASSERT_TRUE(result) << result.error();
    EXPECT_EQ(test::decodeJpeg(*result).iccProfile, profile);
}

TEST(JpegTransform, ResetsTheOrientationInXmp) {
    const std::string xmp = R"(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">)"
                            R"(<rdf:Description xmlns:tiff="http://ns.adobe.com/tiff/1.0/" tiff:Orientation="6" tiff:Make="X"/>)"
                            R"(</rdf:RDF></x:xmpmeta>)";
    const auto jpeg = test::encodeJpeg({.orientation = 6, .xmp = xmp, .pixel = pattern});
    const auto result = transformJpeg(jpeg, JpegEdit::RotateClockwise);
    ASSERT_TRUE(result) << result.error();
    std::string expected = xmp;
    expected.replace(expected.find("\"6\""), 3, "\"1\"");
    EXPECT_EQ(test::decodeJpeg(*result).xmp, expected);
}

TEST(JpegTransform, RefusesWhatIsNotAJpeg) {
    const std::vector<std::uint8_t> bytes(100, 7);
    EXPECT_FALSE(transformJpeg(bytes, JpegEdit::Rotate180));
}

class JpegTransformFile : public ::testing::Test {
protected:
    void SetUp() override {
        m_folder = fs::temp_directory_path() / ("iv_jpeg_transform_" + std::to_string(::getpid()) + "_" +
                                                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(m_folder);
        fs::create_directories(m_folder);
    }
    void TearDown() override { fs::remove_all(m_folder); }

    fs::path m_folder;
};

TEST_F(JpegTransformFile, IsReplacedKeepingItsPermissions) {
    const fs::path path = m_folder / "photo.jpg";
    test::writeFile(path, test::encodeJpeg({.width = 64, .height = 48, .pixel = pattern}));
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);

    const auto changed = transformJpegFile(path, JpegEdit::RotateClockwise);
    ASSERT_TRUE(changed) << changed.error();
    EXPECT_TRUE(*changed);
    EXPECT_EQ(fs::status(path).permissions(), fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
    const DecodedJpeg edited = test::decodeJpeg(*readFile(path));
    EXPECT_EQ(edited.width, 48);
    EXPECT_EQ(edited.height, 64);
    EXPECT_EQ(std::distance(fs::directory_iterator(m_folder), fs::directory_iterator()), 1);
}

TEST_F(JpegTransformFile, IsLeftAloneWhenThereIsNothingToDo) {
    const fs::path path = m_folder / "photo.jpg";
    const auto jpeg = test::encodeJpeg({.pixel = pattern});
    test::writeFile(path, jpeg);
    const auto changed = transformJpegFile(path, JpegEdit::ApplyOrientation);
    ASSERT_TRUE(changed) << changed.error();
    EXPECT_FALSE(*changed);
    EXPECT_EQ(*readFile(path), jpeg);
}

TEST_F(JpegTransformFile, IsLeftAloneWhenItCannotBeDone) {
    const fs::path path = m_folder / "photo.jpg";
    const auto jpeg = test::encodeJpeg({.width = 64, .height = 40, .pixel = pattern});
    test::writeFile(path, jpeg);
    EXPECT_FALSE(transformJpegFile(path, JpegEdit::FlipVertical));
    EXPECT_EQ(*readFile(path), jpeg);
}

TEST(JpegTransform, KnowsJpegExtensions) {
    EXPECT_TRUE(hasJpegExtension("a/b.jpg"));
    EXPECT_TRUE(hasJpegExtension("b.JPEG"));
    EXPECT_TRUE(hasJpegExtension("b.Jpe"));
    EXPECT_FALSE(hasJpegExtension("b.png"));
    EXPECT_FALSE(hasJpegExtension("jpg"));
}

#ifdef IV_HAVE_JXL
std::vector<std::uint8_t> srgbProfile() {
    cmsHPROFILE const profile = cmsCreate_sRGBProfile();
    cmsUInt32Number size = 0;
    cmsSaveProfileToMem(profile, nullptr, &size);
    std::vector<std::uint8_t> bytes(size);
    cmsSaveProfileToMem(profile, bytes.data(), &size);
    cmsCloseProfile(profile);
    return bytes;
}

std::vector<std::uint8_t> photoLikeJpeg() {
    const auto profile = srgbProfile();
    return test::encodeJpeg({.width = 256, .height = 192, .exif = test::exampleExifBlock(true), .iccProfile = profile,
                             .pixel = [](int x, int y) { return pattern(x / 4, y / 4); }});
}

TEST(JpegToJxl, RebuildsTheOriginalByteForByte) {
    const auto jpeg = photoLikeJpeg();
    const auto jxl = jpegToJxl(jpeg);
    ASSERT_TRUE(jxl) << jxl.error();
    EXPECT_LT(jxl->size(), jpeg.size());
    const auto rebuilt = jxlToJpeg(*jxl);
    ASSERT_TRUE(rebuilt) << rebuilt.error();
    EXPECT_EQ(*rebuilt, jpeg);
}

TEST(JpegToJxl, KeepsDataAfterTheImageOrRefuses) {
    auto jpeg = photoLikeJpeg();
    for (int i = 0; i < 500; ++i) {
        jpeg.push_back(static_cast<std::uint8_t>(i));
    }
    // Either way, nothing is ever converted that would not come back the same.
    const auto jxl = jpegToJxl(jpeg);
    if (jxl) {
        EXPECT_EQ(*jxlToJpeg(*jxl), jpeg);
    }
}

TEST(JpegToJxl, RefusesWhatIsNotAJpeg) {
    EXPECT_FALSE(jpegToJxl(std::vector<std::uint8_t>(100, 7)));
    EXPECT_FALSE(jxlToJpeg(std::vector<std::uint8_t>(100, 7)));
}

TEST_F(JpegTransformFile, IsConvertedToJxlNextToIt) {
    const fs::path path = m_folder / "photo.jpg";
    const auto jpeg = photoLikeJpeg();
    test::writeFile(path, jpeg);
    const auto modified = fs::last_write_time(path) - std::chrono::hours(24);
    fs::last_write_time(path, modified);

    const auto created = convertJpegFileToJxl(path);
    ASSERT_TRUE(created) << created.error();
    EXPECT_EQ(*created, m_folder / "photo.jxl");
    EXPECT_EQ(fs::last_write_time(*created), modified);
    EXPECT_EQ(*jxlToJpeg(*readFile(*created)), jpeg);
    EXPECT_EQ(*readFile(path), jpeg);

    // The viewer opens it like any other picture, turned by libjxl as the EXIF says.
    const auto source = openImageSource(*created);
    ASSERT_TRUE(source) << source.error();
    EXPECT_EQ((*source)->subimages().front().levels.front().size, (Size{192, 256}));

    const auto again = convertJpegFileToJxl(path);
    ASSERT_FALSE(again);
    EXPECT_NE(again.error().find("photo.jxl already exists"), std::string::npos);
}
#endif

}  // namespace
}  // namespace iv
