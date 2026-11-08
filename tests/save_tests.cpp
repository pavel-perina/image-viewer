#include <cstdint>
#include <filesystem>
#include <string>

#include <unistd.h>

#include <gtest/gtest.h>

#include "core/color.h"
#include "core/exif.h"
#include "core/image_ops.h"
#include "core/image_source.h"
#include "core/save_image.h"
#include "test_exif.h"
#include "test_jpeg.h"
#include "test_tiff.h"

namespace iv {
namespace {

namespace fs = std::filesystem;

const std::string kXmp = R"(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">)"
                         R"(<rdf:Description xmlns:tiff="http://ns.adobe.com/tiff/1.0/" xmlns:exif="http://ns.adobe.com/exif/1.0/")"
                         R"( xmlns:xmp="http://ns.adobe.com/xap/1.0/" tiff:Orientation="6" exif:GPSLatitude="49,11.51N")"
                         R"( xmp:Rating="4"/></rdf:RDF></x:xmpmeta>)";

test::Rgb pattern(int x, int y) {
    return {static_cast<std::uint8_t>(x * 3), static_cast<std::uint8_t>(y * 4), static_cast<std::uint8_t>(x < 20 && y < 12 ? 240 : 30)};
}

bool sameRgb(const ImageBuffer& a, const ImageBuffer& b) {
    if (a.size() != b.size() || a.format() != PixelFormat::Bgrx8 || b.format() != PixelFormat::Bgrx8) {
        return false;
    }
    for (int y = 0; y < a.height(); ++y) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(a.row(y));
        const auto* q = reinterpret_cast<const std::uint8_t*>(b.row(y));
        for (int x = 0; x < a.width() * 4; ++x) {
            if (x % 4 != 3 && p[x] != q[x]) {
                return false;
            }
        }
    }
    return true;
}

class Save : public ::testing::Test {
protected:
    void SetUp() override {
        m_folder = fs::temp_directory_path() / ("iv_save_" + std::to_string(::getpid()) + "_" +
                                                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(m_folder);
        fs::create_directories(m_folder);
        test::JpegOptions options;
        options.width = 64;
        options.height = 48;
        options.exif = test::exampleExifBlock(true);  // orientation 6, a camera and a position
        options.xmp = kXmp;
        options.pixel = pattern;
        m_jpeg = m_folder / "photo.jpg";
        test::writeFile(m_jpeg, test::encodeJpeg(options));
    }
    void TearDown() override { fs::remove_all(m_folder); }

    std::shared_ptr<ImageSource> open(const fs::path& path) {
        auto source = openImageSource(path);
        EXPECT_TRUE(source) << source.error();
        return source ? *source : nullptr;
    }

    // Saves the JPEG as a PNG and opens the result.
    std::shared_ptr<ImageSource> saved(const SaveOptions& options) {
        const auto source = open(m_jpeg);
        const fs::path path = m_folder / "saved.png";
        const auto result = saveSubimage(*source, 0, path, PngWriteOptions{}, options, true);
        EXPECT_TRUE(result) << result.error();
        return open(path);
    }

    fs::path m_folder;
    fs::path m_jpeg;
};

TEST_F(Save, WritesThePixelsUprightAsShown) {
    const auto source = open(m_jpeg);
    const auto stored = readLevel(*source, 0, 0, {});
    ASSERT_TRUE(stored);
    const ImageBuffer shown = applyOrientation(*stored, Orientation::Rotate90CW);

    const auto result = saved({});
    const SubimageInfo& info = result->subimages().front();
    EXPECT_EQ(info.orientation, Orientation::Normal);
    EXPECT_EQ(info.levels.front().size, (Size{48, 64}));
    const auto pixels = readLevel(*result, 0, 0, {});
    ASSERT_TRUE(pixels);
    EXPECT_TRUE(sameRgb(*pixels, shown));
}

TEST_F(Save, KeepsTheMetadataWithTheOrientationReset) {
    const SubimageInfo info = saved({}).get()->subimages().front();
    EXPECT_EQ(info.exif.model, "Canon EOS 5D");
    EXPECT_EQ(info.exif.orientation, Orientation::Normal);
    EXPECT_TRUE(info.exif.latitude);
    const std::string xmp(info.xmp.begin(), info.xmp.end());
    EXPECT_NE(xmp.find(R"(tiff:Orientation="1")"), std::string::npos);
    EXPECT_NE(xmp.find("GPSLatitude"), std::string::npos);
    EXPECT_NE(xmp.find(R"(xmp:Rating="4")"), std::string::npos);
}

TEST_F(Save, CanRemoveTheLocation) {
    const SubimageInfo info = saved({.keepMetadata = true, .removeLocation = true})->subimages().front();
    EXPECT_EQ(info.exif.model, "Canon EOS 5D");
    EXPECT_FALSE(info.exif.latitude);
    EXPECT_FALSE(info.exif.longitude);
    EXPECT_FALSE(info.exif.altitude);
    // Not even the numbers are left: 49 degrees as the RATIONAL 49/1, little-endian.
    const std::uint8_t degrees[] = {49, 0, 0, 0, 1, 0, 0, 0};
    EXPECT_EQ(std::ranges::search(info.rawExif, degrees).begin(), info.rawExif.end());
    const std::string xmp(info.xmp.begin(), info.xmp.end());
    EXPECT_EQ(xmp.find("GPS"), std::string::npos);
    EXPECT_NE(xmp.find(R"(xmp:Rating="4")"), std::string::npos);
}

TEST_F(Save, CanLeaveTheMetadataOutButKeepsTheProfile) {
    test::JpegOptions options;
    options.exif = test::exampleExifBlock(true);
    options.xmp = kXmp;
    options.iccProfile = adobeRgbProfile();
    options.pixel = [](int, int) { return test::Rgb{200, 60, 30}; };  // gray would get a gray profile
    test::writeFile(m_jpeg, test::encodeJpeg(options));
    const SubimageInfo info = saved({.keepMetadata = false})->subimages().front();
    EXPECT_FALSE(info.exif.present);
    EXPECT_TRUE(info.rawExif.empty());
    EXPECT_TRUE(info.xmp.empty());
    EXPECT_EQ(info.iccProfile, adobeRgbProfile());
}

TEST_F(Save, WritesTheChosenPage) {
    test::TiffImage first;
    first.width = 40;
    test::TiffImage second;
    second.width = 24;
    second.height = 16;
    second.pixel = [](int, int) { return test::Rgb{10, 200, 30}; };
    const fs::path tiff = m_folder / "pages.tif";
    test::writeTiff(tiff, {first, second});

    const auto source = open(tiff);
    const fs::path path = m_folder / "page.png";
    ASSERT_TRUE(saveSubimage(*source, 1, path, PngWriteOptions{}, {}, true));
    const auto result = open(path);
    EXPECT_EQ(result->subimages().front().levels.front().size, (Size{24, 16}));
    EXPECT_FALSE(saveSubimage(*source, 2, path, PngWriteOptions{}, {}, true));
}

TEST_F(Save, NeverReplacesAFileUnlessTold) {
    const auto source = open(m_jpeg);
    EXPECT_FALSE(saveSubimage(*source, 0, m_jpeg, PngWriteOptions{}, {}, false));
    EXPECT_TRUE(saveSubimage(*source, 0, m_folder / "new.png", PngWriteOptions{}, {}, false));
}

}  // namespace
}  // namespace iv
