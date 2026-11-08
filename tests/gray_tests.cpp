#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>

#include <gtest/gtest.h>

#include "core/histogram.h"
#include "core/image_ops.h"
#include "core/image_source.h"
#include "core/image_writer.h"
#include "core/pixel_probe.h"

using namespace iv;

namespace {

// One pixel per colour, each given as R, G, B in 8 bits.
ImageBuffer pixels8(std::initializer_list<std::array<std::uint8_t, 3>> colours) {
    const ImageBuffer image = ImageBuffer::create({static_cast<int>(colours.size()), 1}, PixelFormat::Bgrx8);
    auto* p = reinterpret_cast<std::uint8_t*>(image.row(0));
    for (const auto& c : colours) {
        p[0] = c[2];
        p[1] = c[1];
        p[2] = c[0];
        p[3] = 77;
        p += 4;
    }
    return image;
}

std::array<std::uint8_t, 4> at8(const ImageBuffer& image, int x) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(image.row(0)) + x * 4;
    return {p[0], p[1], p[2], p[3]};
}

}  // namespace

TEST(Gray, LumaWeighsTheSamplesAsJpegDoes) {
    const ImageBuffer gray = toGray(pixels8({{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {10, 200, 30}}), GrayMethod::Luma);
    ASSERT_EQ(gray.format(), PixelFormat::Bgrx8);
    EXPECT_EQ(at8(gray, 0), (std::array<std::uint8_t, 4>{76, 76, 76, 77})) << "the fourth byte stays";
    EXPECT_EQ(at8(gray, 1)[0], 150);
    EXPECT_EQ(at8(gray, 2)[0], 29);
    EXPECT_EQ(at8(gray, 3)[0], 255);
    EXPECT_EQ(at8(gray, 4)[0], 124);  // 0.299 * 10 + 0.587 * 200 + 0.114 * 30 = 123.8
}

TEST(Gray, LightnessKeepsEveryGrayAsItIs) {
    for (int value = 0; value < 256; ++value) {
        const auto v = static_cast<std::uint8_t>(value);
        const ImageBuffer gray = toGray(pixels8({{v, v, v}}), GrayMethod::Lightness);
        ASSERT_EQ(at8(gray, 0)[0], value);
    }
}

TEST(Gray, LightnessMakesBlueDarkAndYellowLight) {
    // Oklab lightness of sRGB red 0.628, green 0.866, blue 0.452, yellow 0.968, cubed and encoded.
    const ImageBuffer gray = toGray(pixels8({{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}}), GrayMethod::Lightness);
    EXPECT_NEAR(at8(gray, 0)[0], 136, 1);
    EXPECT_NEAR(at8(gray, 1)[0], 211, 1);
    EXPECT_NEAR(at8(gray, 2)[0], 86, 1);
    EXPECT_NEAR(at8(gray, 3)[0], 244, 1);
    const auto pixel = at8(gray, 2);
    EXPECT_EQ(pixel[0], pixel[1]);
    EXPECT_EQ(pixel[1], pixel[2]);
}

TEST(Gray, DeepFormatsKeepTheirFormatAndAlpha) {
    const ImageBuffer deep = ImageBuffer::create({1, 1}, PixelFormat::Rgba16);
    const std::array<std::uint16_t, 4> red{65535, 0, 0, 1234};
    std::memcpy(deep.row(0), red.data(), sizeof(red));
    const ImageBuffer gray = toGray(deep, GrayMethod::Luma);
    ASSERT_EQ(gray.format(), PixelFormat::Rgba16);
    const auto* p = reinterpret_cast<const std::uint16_t*>(gray.row(0));
    EXPECT_EQ(p[0], 19595);
    EXPECT_EQ(p[1], 19595);
    EXPECT_EQ(p[2], 19595);
    EXPECT_EQ(p[3], 1234);

    const ImageBuffer floats = ImageBuffer::create({1, 1}, PixelFormat::Rgba32F);
    const std::array<float, 4> mid{0.5F, 0.5F, 0.5F, 1};
    std::memcpy(floats.row(0), mid.data(), sizeof(mid));
    const ImageBuffer grayFloats = toGray(floats, GrayMethod::Lightness);
    EXPECT_NEAR(reinterpret_cast<const float*>(grayFloats.row(0))[0], 0.5F, 1e-5);
}

TEST(Gray, AGrayFormatIsLeftAsItIs) {
    const ImageBuffer gray = ImageBuffer::create({4, 4}, PixelFormat::Gray16);
    EXPECT_EQ(toGray(gray, GrayMethod::Lightness).data(), gray.data());
}

namespace {

// A ramp across, 0 at the left to 255 at the right.
ImageBuffer ramp8(Size size) {
    const ImageBuffer image = ImageBuffer::create(size, PixelFormat::Gray8);
    for (int y = 0; y < size.height; ++y) {
        auto* row = reinterpret_cast<std::uint8_t*>(image.row(y));
        for (int x = 0; x < size.width; ++x) {
            row[x] = static_cast<std::uint8_t>(x * 255 / (size.width - 1));
        }
    }
    return image;
}

ImageBuffer flat8(Size size, std::uint8_t value) {
    const ImageBuffer image = ImageBuffer::create(size, PixelFormat::Gray8);
    for (int y = 0; y < size.height; ++y) {
        std::memset(image.row(y), value, static_cast<std::size_t>(size.width));
    }
    return image;
}

std::uint8_t at(const ImageBuffer& gray, int x, int y) { return reinterpret_cast<const std::uint8_t*>(gray.row(y))[x]; }

}  // namespace

TEST(Gray8, ProcessingKeepsOneChannelAndFlatValues) {
    for (const int value : {0, 1, 77, 128, 254, 255}) {
        const ImageBuffer flat = flat8({40, 30}, static_cast<std::uint8_t>(value));
        const ImageBuffer results[] = {halve(flat), resize(flat, {17, 9}), resize(flat, {90, 61}),
                                       warp(flat, {1, 0, 0.3, 0, 1, 0.7, 0, 0, 1}, {20, 20})};
        for (const ImageBuffer& result : results) {
            ASSERT_EQ(result.format(), PixelFormat::Gray8);
            EXPECT_EQ(at(result, 0, 0), value);
            EXPECT_EQ(at(result, result.width() - 1, result.height() - 1), value);
        }
    }
    const ImageBuffer turned = applyOrientation(ramp8({40, 30}), Orientation::Rotate180);
    ASSERT_EQ(turned.format(), PixelFormat::Gray8);
    EXPECT_EQ(at(turned, 0, 0), 255);
    EXPECT_EQ(at(turned, 39, 0), 0);
}

TEST(Gray8, HalvingMixesInLinearLightAsColourDoes) {
    // Black and white stripes come out much lighter than the 128 of averaging the values, and as
    // light as from an 8-bit colour image.
    const ImageBuffer stripes = ImageBuffer::create({8, 8}, PixelFormat::Gray8);
    const ImageBuffer colour = pixels8({{0, 0, 0}, {255, 255, 255}, {0, 0, 0}, {255, 255, 255}, {0, 0, 0}, {255, 255, 255}, {0, 0, 0}, {255, 255, 255}});
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            reinterpret_cast<std::uint8_t*>(stripes.row(y))[x] = x % 2 ? 255 : 0;
        }
    }
    const ImageBuffer half = halve(halve(stripes));
    const ImageBuffer colourHalf = halve(halve(colour));
    EXPECT_GT(at(half, 1, 1), 170);
    EXPECT_EQ(at(half, 1, 0), at8(colourHalf, 1)[0]);
}

TEST(Gray8, IsShownThroughItsRange) {
    const ImageBuffer gray = ramp8({256, 2});
    const ImageBuffer shown = toDisplay(gray, fullRange(PixelFormat::Gray8));
    ASSERT_EQ(shown.format(), PixelFormat::Bgrx8);
    const auto* p = reinterpret_cast<const std::uint8_t*>(shown.row(0)) + 100 * 4;
    EXPECT_EQ(p[0], 100);
    EXPECT_EQ(p[1], 100);
    EXPECT_EQ(p[2], 100);
    EXPECT_EQ(p[3], 255);

    // A dim picture, 50 to 100, is stretched by auto contrast.
    const ImageBuffer dim = ImageBuffer::create({51, 4}, PixelFormat::Gray8);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 51; ++x) {
            reinterpret_cast<std::uint8_t*>(dim.row(y))[x] = static_cast<std::uint8_t>(50 + x);
        }
    }
    const ImageBuffer samples[] = {dim};
    const DisplayRange range = autoRange(samples);
    EXPECT_EQ(range.low, 50);
    EXPECT_EQ(range.high, 100);
    const ImageBuffer stretchedImage = toDisplay(dim, range);
    const auto* stretched = reinterpret_cast<const std::uint8_t*>(stretchedImage.row(0));
    EXPECT_EQ(stretched[0], 0);
    EXPECT_EQ(stretched[50 * 4], 255);
}

TEST(Gray8, HasAGrayHistogram) {
    const ImageBuffer tiles[] = {flat8({10, 10}, 30), flat8({10, 10}, 200)};
    const Histogram histogram = histogramOf(tiles);
    EXPECT_TRUE(histogram.gray);
    EXPECT_EQ(histogram.bitsPerBin, 8);
    EXPECT_EQ(histogram.channels[0][30], 100u);
    EXPECT_EQ(histogram.channels[0][200], 100u);
}

class Gray8Files : public ::testing::Test {
protected:
    void SetUp() override {
        m_folder = std::filesystem::temp_directory_path() /
                   ("iv_gray8_" + std::to_string(::getpid()) + "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(m_folder);
        std::filesystem::create_directories(m_folder);
    }
    void TearDown() override { std::filesystem::remove_all(m_folder); }
    std::filesystem::path m_folder;
};

TEST_F(Gray8Files, GrayFilesOpenAsOneChannelAndSaveAsGray) {
    const ImageBuffer image = ramp8({64, 48});
    const WriteOptions all[] = {JpegWriteOptions{}, PngWriteOptions{}, TiffWriteOptions{}, JxlWriteOptions{.lossless = true}};
    for (const WriteOptions& options : all) {
        SCOPED_TRACE(std::string(extensionFor(options)));
        if (!canWrite(options)) {
            continue;
        }
        const auto path = m_folder / ("gray." + std::string(extensionFor(options)));
        ASSERT_TRUE(writeImage(path, image, {}, options, true));
        auto source = openImageSource(path);
        ASSERT_TRUE(source) << source.error();
        const SubimageInfo& info = (*source)->subimages().front();
        EXPECT_EQ(info.format, PixelFormat::Gray8);
        EXPECT_EQ(info.stored.colorModel.substr(0, 9), "Grayscale");
        auto tile = (*source)->readTile({0, 0, 0, 0}, {});
        ASSERT_TRUE(tile) << tile.error();
        ASSERT_EQ(tile->format(), PixelFormat::Gray8);
        EXPECT_NEAR(at(*tile, 63, 10), 255, 2);
        EXPECT_NEAR(at(*tile, 32, 10), 129, 3);

        const auto probe = probePixel(**source, 0, 63, 10, {});
        ASSERT_TRUE(probe);
        EXPECT_EQ(probe->channels, 1);
        EXPECT_EQ(probe->format, PixelFormat::Gray8);
    }
}
