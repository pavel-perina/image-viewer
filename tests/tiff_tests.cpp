#include <gtest/gtest.h>

#include <tiffio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "core/image_ops.h"
#include "core/image_source.h"
#include "core/thumbnail.h"
#include "test_jpeg.h"
#include "test_tiff.h"

using namespace iv;
using test::Rgb;
using test::TiffImage;
using test::writeTiff;

namespace {

class TempDir {
public:
    TempDir() {
        static std::atomic<int> counter = 0;
        m_path = std::filesystem::temp_directory_path() /
                 ("iv_tiff_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + "_" + std::to_string(++counter));
        std::filesystem::create_directories(m_path);
    }
    ~TempDir() { std::filesystem::remove_all(m_path); }
    std::filesystem::path file(const std::string& name) const { return m_path / name; }

private:
    std::filesystem::path m_path;
};

std::shared_ptr<ImageSource> open(const std::filesystem::path& path) {
    auto source = openImageSource(path);
    EXPECT_TRUE(source.has_value()) << (source ? "" : source.error());
    return source ? *source : nullptr;
}

// The colour shown for a level pixel, reading whichever tile holds it.
Rgb shownPixel(ImageSource& source, int subimage, int levelIndex, int x, int y, std::optional<DisplayRange> range = {}) {
    const SubimageInfo& info = source.subimages()[static_cast<std::size_t>(subimage)];
    const LevelInfo& level = info.levels[static_cast<std::size_t>(levelIndex)];
    const auto tile = source.readTile({subimage, levelIndex, x / level.tileSize.width, y / level.tileSize.height}, {});
    if (!tile) {
        ADD_FAILURE() << tile.error();
        return {};
    }
    const ImageBuffer shown = toDisplay(*tile, range.value_or(fullRange(info.format)));
    const auto* pixel = reinterpret_cast<const std::uint8_t*>(shown.row(y % level.tileSize.height)) +
                        static_cast<std::size_t>(x % level.tileSize.width) * 4;
    return {pixel[2], pixel[1], pixel[0]};
}

void expectColor(const Rgb& actual, const Rgb& expected, int tolerance = 3) {
    EXPECT_NEAR(actual.r, expected.r, tolerance);
    EXPECT_NEAR(actual.g, expected.g, tolerance);
    EXPECT_NEAR(actual.b, expected.b, tolerance);
}

Rgb leftRight(int x, int boundary) { return x < boundary ? Rgb{200, 40, 20} : Rgb{20, 40, 200}; }

}  // namespace

struct FormatCase {
    const char* name;
    int bits;
    int samples;
    int sampleFormat;
    int tile;
    int planar;
    int compression;
    PixelFormat expected;
};

class TiffFormats : public testing::TestWithParam<FormatCase> {};

TEST_P(TiffFormats, DecodesToTheRightFormatAndColours) {
    const FormatCase c = GetParam();
    TempDir dir;
    TiffImage image;
    image.width = 300;
    image.height = 280;
    image.bits = c.bits;
    image.samples = c.samples;
    image.sampleFormat = c.sampleFormat;
    image.tileSize = c.tile;
    image.planar = c.planar;
    image.compression = c.compression;
    // Colour changes on the 256 tile boundary, and differs per channel to catch swaps.
    image.pixel = [](int x, int) { return leftRight(x, 256); };
    if (c.samples == 1) {
        image.pixel = [](int x, int) { return x < 256 ? Rgb{200, 200, 200} : Rgb{40, 40, 40}; };
    }
    writeTiff(dir.file("t.tif"), {image});

    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    const SubimageInfo& info = source->subimages()[0];
    EXPECT_EQ(info.format, c.expected);
    EXPECT_EQ(info.levels[0].size, (Size{300, 280}));

    const std::optional<DisplayRange> range =
        c.sampleFormat == SAMPLEFORMAT_INT || c.bits != 32 || c.sampleFormat == SAMPLEFORMAT_IEEEFP
            ? std::nullopt
            : std::optional<DisplayRange>(DisplayRange{0, 4294967295.0});
    const Rgb left = c.samples == 1 ? Rgb{200, 200, 200} : Rgb{200, 40, 20};
    const Rgb right = c.samples == 1 ? Rgb{40, 40, 40} : Rgb{20, 40, 200};
    expectColor(shownPixel(*source, 0, 0, 100, 100, range), left);
    expectColor(shownPixel(*source, 0, 0, 280, 100, range), right);
    // The bottom edge, where a tile is only partly covered by the image.
    expectColor(shownPixel(*source, 0, 0, 280, 279, range), right);
}

INSTANTIATE_TEST_SUITE_P(
    Matrix, TiffFormats,
    testing::Values(
        FormatCase{"Rgb8Strips", 8, 3, SAMPLEFORMAT_UINT, 0, PLANARCONFIG_CONTIG, COMPRESSION_NONE, PixelFormat::Bgrx8},
        FormatCase{"Rgb8Tiles", 8, 3, SAMPLEFORMAT_UINT, 256, PLANARCONFIG_CONTIG, COMPRESSION_NONE, PixelFormat::Bgrx8},
        FormatCase{"Gray8Strips", 8, 1, SAMPLEFORMAT_UINT, 0, PLANARCONFIG_CONTIG, COMPRESSION_NONE, PixelFormat::Bgrx8},
        FormatCase{"Gray8Tiles", 8, 1, SAMPLEFORMAT_UINT, 256, PLANARCONFIG_CONTIG, COMPRESSION_NONE, PixelFormat::Bgrx8},
        FormatCase{"Rgb16Strips", 16, 3, SAMPLEFORMAT_UINT, 0, PLANARCONFIG_CONTIG, COMPRESSION_NONE, PixelFormat::Rgba16},
        FormatCase{"Rgb16Tiles", 16, 3, SAMPLEFORMAT_UINT, 256, PLANARCONFIG_CONTIG, COMPRESSION_NONE, PixelFormat::Rgba16},
        FormatCase{"Gray16Strips", 16, 1, SAMPLEFORMAT_UINT, 0, PLANARCONFIG_CONTIG, COMPRESSION_NONE, PixelFormat::Gray16},
        FormatCase{"Gray16Tiles", 16, 1, SAMPLEFORMAT_UINT, 256, PLANARCONFIG_CONTIG, COMPRESSION_NONE, PixelFormat::Gray16},
        FormatCase{"Gray16Signed", 16, 1, SAMPLEFORMAT_INT, 0, PLANARCONFIG_CONTIG, COMPRESSION_NONE, PixelFormat::Gray16},
        FormatCase{"Gray8Signed", 8, 1, SAMPLEFORMAT_INT, 0, PLANARCONFIG_CONTIG, COMPRESSION_NONE, PixelFormat::Bgrx8},
        FormatCase{"Gray32Float", 32, 1, SAMPLEFORMAT_IEEEFP, 256, PLANARCONFIG_CONTIG, COMPRESSION_NONE, PixelFormat::Gray32F},
        FormatCase{"Rgb32Float", 32, 3, SAMPLEFORMAT_IEEEFP, 0, PLANARCONFIG_CONTIG, COMPRESSION_NONE, PixelFormat::Rgba32F},
        FormatCase{"Gray64Float", 64, 1, SAMPLEFORMAT_IEEEFP, 0, PLANARCONFIG_CONTIG, COMPRESSION_NONE, PixelFormat::Gray32F},
        FormatCase{"Gray32Uint", 32, 1, SAMPLEFORMAT_UINT, 0, PLANARCONFIG_CONTIG, COMPRESSION_NONE, PixelFormat::Gray32F},
        FormatCase{"Rgb8PlanarStrips", 8, 3, SAMPLEFORMAT_UINT, 0, PLANARCONFIG_SEPARATE, COMPRESSION_NONE, PixelFormat::Bgrx8},
        FormatCase{"Rgb16PlanarTiles", 16, 3, SAMPLEFORMAT_UINT, 256, PLANARCONFIG_SEPARATE, COMPRESSION_NONE, PixelFormat::Rgba16},
        FormatCase{"Rgb8Deflate", 8, 3, SAMPLEFORMAT_UINT, 256, PLANARCONFIG_CONTIG, COMPRESSION_ADOBE_DEFLATE, PixelFormat::Bgrx8},
        FormatCase{"Gray16Zstd", 16, 1, SAMPLEFORMAT_UINT, 256, PLANARCONFIG_CONTIG, COMPRESSION_ZSTD, PixelFormat::Gray16},
        FormatCase{"Rgb8Lzw", 8, 3, SAMPLEFORMAT_UINT, 0, PLANARCONFIG_CONTIG, COMPRESSION_LZW, PixelFormat::Bgrx8}),
    [](const testing::TestParamInfo<FormatCase>& info) { return std::string(info.param.name); });

TEST(Tiff, MinIsWhiteIsInverted) {
    TempDir dir;
    TiffImage image;
    image.samples = 1;
    image.photometric = PHOTOMETRIC_MINISWHITE;
    image.pixel = [](int, int) { return Rgb{50, 50, 50}; };
    writeTiff(dir.file("t.tif"), {image});

    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    expectColor(shownPixel(*source, 0, 0, 10, 10), {205, 205, 205});
}

TEST(Tiff, ReadsBigTiff) {
    TempDir dir;
    TiffImage image;
    image.pixel = [](int, int) { return Rgb{10, 200, 30}; };
    writeTiff(dir.file("t.tif"), {image}, true);

    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    expectColor(shownPixel(*source, 0, 0, 5, 5), {10, 200, 30});
}

TEST(Tiff, OneRowStripsAreGroupedIntoTiles) {
    TempDir dir;
    TiffImage image;
    image.width = 100;
    image.height = 3000;
    image.samples = 1;
    image.rowsPerStrip = 1;
    image.pixel = [](int, int y) { return y < 2500 ? Rgb{200, 200, 200} : Rgb{40, 40, 40}; };
    writeTiff(dir.file("t.tif"), {image});

    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    // Not 3000 tiles of one row each, which would mean 3000 draw calls.
    EXPECT_GE(source->subimages()[0].levels[0].tileSize.height, 256);
    expectColor(shownPixel(*source, 0, 0, 50, 2400), {200, 200, 200});
    expectColor(shownPixel(*source, 0, 0, 50, 2600), {40, 40, 40});
}

TEST(Tiff, SingleHugeStripIsCutIntoTextureSizedTiles) {
    TempDir dir;
    TiffImage image;
    image.width = 5000;
    image.height = 4000;
    image.samples = 1;
    image.rowsPerStrip = 4000;
    image.compression = COMPRESSION_ADOBE_DEFLATE;
    image.pixel = [](int x, int y) { return x > 4000 && y > 3000 ? Rgb{240, 240, 240} : Rgb{20, 20, 20}; };
    writeTiff(dir.file("t.tif"), {image});

    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    const LevelInfo& level = source->subimages()[0].levels[0];
    EXPECT_LE(level.tileSize.width, 4096);
    EXPECT_LE(level.tileSize.height, 4096);
    expectColor(shownPixel(*source, 0, 0, 4500, 3500), {240, 240, 240});
    expectColor(shownPixel(*source, 0, 0, 100, 100), {20, 20, 20});
}

TEST(Tiff, PagesBecomeSubimagesEvenWithDifferentSizesAndFormats) {
    TempDir dir;
    TiffImage first;
    first.width = 100;
    first.height = 80;
    first.pixel = [](int, int) { return Rgb{200, 0, 0}; };
    TiffImage second;
    second.width = 40;
    second.height = 300;
    second.bits = 16;
    second.samples = 1;
    second.pixel = [](int, int) { return Rgb{90, 90, 90}; };
    writeTiff(dir.file("t.tif"), {first, second});

    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    ASSERT_EQ(source->subimages().size(), 2u);
    EXPECT_EQ(source->subimages()[0].levels.size(), 1u);
    EXPECT_EQ(source->subimages()[1].levels[0].size, (Size{40, 300}));
    EXPECT_EQ(source->subimages()[1].format, PixelFormat::Gray16);
    expectColor(shownPixel(*source, 0, 0, 5, 5), {200, 0, 0}, 5);
    expectColor(shownPixel(*source, 1, 0, 5, 5), {90, 90, 90});
    // Switching back must not reuse the other page's directory or cached rows.
    expectColor(shownPixel(*source, 0, 0, 5, 5), {200, 0, 0}, 5);
}

namespace {

TiffImage level(int width, int height, Rgb color, bool reduced) {
    TiffImage image;
    image.width = width;
    image.height = height;
    image.tileSize = 256;
    image.reduced = reduced;
    image.pixel = [color](int, int) { return color; };
    return image;
}

}  // namespace

TEST(Tiff, ReducedImagesInTheMainChainAreLevelsOfTheirPage) {
    TempDir dir;
    writeTiff(dir.file("t.tif"), {level(1000, 800, {200, 0, 0}, false), level(500, 400, {0, 200, 0}, true),
                                  level(250, 200, {0, 0, 200}, true), level(300, 300, {90, 90, 90}, false),
                                  level(150, 150, {50, 50, 50}, true)});

    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    ASSERT_EQ(source->subimages().size(), 2u);
    const auto& first = source->subimages()[0];
    ASSERT_EQ(first.levels.size(), 3u);
    EXPECT_EQ(first.levels[1].size, (Size{500, 400}));
    expectColor(shownPixel(*source, 0, 1, 5, 5), {0, 200, 0}, 5);
    expectColor(shownPixel(*source, 0, 2, 5, 5), {0, 0, 200}, 5);

    ASSERT_EQ(source->subimages()[1].levels.size(), 2u);
    expectColor(shownPixel(*source, 1, 1, 5, 5), {50, 50, 50}, 5);
    // Levels can be read in any order.
    expectColor(shownPixel(*source, 0, 0, 5, 5), {200, 0, 0}, 5);
}

TEST(Tiff, ReducedImagesInSubIfdsAreLevels) {
    TempDir dir;
    TiffImage main = level(1000, 800, {200, 0, 0}, false);
    main.subIfds = {level(500, 400, {0, 200, 0}, true), level(250, 200, {0, 0, 200}, true)};
    writeTiff(dir.file("t.tif"), {main, level(64, 64, {90, 90, 90}, false)});

    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    ASSERT_EQ(source->subimages().size(), 2u);
    ASSERT_EQ(source->subimages()[0].levels.size(), 3u);
    expectColor(shownPixel(*source, 0, 2, 5, 5), {0, 0, 200}, 5);
    expectColor(shownPixel(*source, 1, 0, 5, 5), {90, 90, 90}, 5);
}

TEST(Tiff, LevelsAreSortedAndNonDoublingRatiosAreKept) {
    TempDir dir;
    // Out of order, and a factor of 3 between two of them.
    writeTiff(dir.file("t.tif"), {level(1200, 900, {200, 0, 0}, false), level(100, 75, {0, 0, 200}, true),
                                  level(400, 300, {0, 200, 0}, true)});
    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    const auto& levels = source->subimages()[0].levels;
    ASSERT_EQ(levels.size(), 3u);
    EXPECT_EQ(levels[1].size, (Size{400, 300}));
    EXPECT_EQ(levels[2].size, (Size{100, 75}));
    expectColor(shownPixel(*source, 0, 1, 5, 5), {0, 200, 0}, 5);
}

TEST(Tiff, InconsistentReducedImagesAreIgnored) {
    TempDir dir;
    TiffImage wrongFormat = level(500, 400, {0, 200, 0}, true);
    wrongFormat.bits = 16;
    TiffImage larger = level(2000, 1600, {0, 0, 200}, true);
    writeTiff(dir.file("t.tif"), {level(1000, 800, {200, 0, 0}, false), wrongFormat, larger});
    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    ASSERT_EQ(source->subimages()[0].levels.size(), 1u);
    EXPECT_EQ(source->subimages()[0].levels[0].size, (Size{1000, 800}));
}

TEST(Tiff, ReadsOrientationAndIccProfilePerPage) {
    TempDir dir;
    TiffImage first;
    first.orientation = 6;
    first.iccProfile = {1, 2, 3, 4};
    TiffImage second;
    writeTiff(dir.file("t.tif"), {first, second});

    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    EXPECT_EQ(source->subimages()[0].orientation, Orientation::Rotate90CW);
    EXPECT_EQ(source->subimages()[0].iccProfile, first.iccProfile);
    EXPECT_EQ(source->subimages()[1].orientation, Orientation::Normal);
    EXPECT_TRUE(source->subimages()[1].iccProfile.empty());
}

TEST(Tiff, RejectsTilesThatDoNotExist) {
    TempDir dir;
    writeTiff(dir.file("t.tif"), {level(300, 300, {1, 2, 3}, false)});
    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    EXPECT_FALSE(source->readTile({1, 0, 0, 0}, {}).has_value());
    EXPECT_FALSE(source->readTile({0, 5, 0, 0}, {}).has_value());
    EXPECT_FALSE(source->readTile({0, 0, 2, 0}, {}).has_value());
    EXPECT_FALSE(source->readTile({0, 0, -1, 0}, {}).has_value());
    EXPECT_FALSE(source->readTile({0, 0, std::numeric_limits<int>::max(), 0}, {}).has_value());
}

TEST(Tiff, StopTokenCancelsAndALaterReadWorks) {
    TempDir dir;
    writeTiff(dir.file("t.tif"), {level(300, 300, {1, 2, 3}, false)});
    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = source->readTile({0, 0, 0, 0}, stop.get_token());
    ASSERT_FALSE(cancelled.has_value());
    EXPECT_EQ(cancelled.error(), "cancelled");
    EXPECT_TRUE(source->readTile({0, 0, 0, 0}, {}).has_value());
}

TEST(Tiff, ConcurrentReadsAcrossPagesAndLevelsAreCorrect) {
    TempDir dir;
    writeTiff(dir.file("t.tif"), {level(1000, 800, {200, 0, 0}, false), level(500, 400, {0, 200, 0}, true),
                                  level(600, 600, {0, 0, 200}, false)});
    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);

    std::atomic<int> wrong = 0;
    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&, t] {
                for (int i = 0; i < 40; ++i) {
                    const int kind = (t + i) % 3;
                    const int subimage = kind == 2 ? 1 : 0;
                    const int levelIndex = kind == 1 ? 1 : 0;
                    const auto tile = source->readTile({subimage, levelIndex, i % 2, i % 2}, {});
                    if (!tile) {
                        ++wrong;
                        continue;
                    }
                    const auto* pixel = reinterpret_cast<const std::uint8_t*>(tile->row(3));
                    const int expected = kind == 0 ? 2 : kind == 1 ? 1 : 0;  // B, G, R byte order
                    if (pixel[expected] < 150) {
                        ++wrong;
                    }
                }
            });
        }
    }
    EXPECT_EQ(wrong, 0);
}

TEST(Tiff, DamagedFilesReportErrorsWithoutCrashing) {
    TempDir dir;
    TiffImage image = level(600, 600, {1, 2, 3}, false);
    image.compression = COMPRESSION_ADOBE_DEFLATE;
    writeTiff(dir.file("t.tif"), {image});
    const auto size = std::filesystem::file_size(dir.file("t.tif"));

    std::vector<char> bytes(size);
    std::ifstream(dir.file("t.tif"), std::ios::binary).read(bytes.data(), static_cast<std::streamsize>(size));

    const auto writePrefix = [&](const std::filesystem::path& path, std::size_t length) {
        std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(length));
    };

    // Too short for any directory.
    writePrefix(dir.file("header.tif"), 8);
    EXPECT_FALSE(openImageSource(dir.file("header.tif")).has_value());
    writePrefix(dir.file("magic.tif"), 4);
    EXPECT_FALSE(openImageSource(dir.file("magic.tif")).has_value());

    // Random corruption at many positions: any outcome is fine except a crash.
    for (std::size_t position = 0; position < size; position += std::max<std::size_t>(1, size / 200)) {
        std::vector<char> copy = bytes;
        for (std::size_t i = 0; i < 8 && position + i < size; ++i) {
            copy[position + i] = static_cast<char>(0xA5 ^ i);
        }
        const auto path = dir.file("fuzz.tif");
        std::ofstream(path, std::ios::binary | std::ios::trunc).write(copy.data(), static_cast<std::streamsize>(size));
        auto source = openImageSource(path);
        if (source) {
            for (const SubimageInfo& info : (*source)->subimages()) {
                const LevelInfo& first = info.levels[0];
                if (static_cast<std::uint64_t>(first.size.width) * first.size.height < 100'000'000) {
                    (void)(*source)->readTile({0, 0, 0, 0}, {});
                }
                break;
            }
        }
    }
    SUCCEED();
}

TEST(Tiff, JpegCompressedTiffAndPlainJpegWorkInOneProcess) {
    TempDir dir;
    TiffImage image = level(512, 512, {200, 40, 20}, false);
    image.compression = COMPRESSION_JPEG;
    writeTiff(dir.file("t.tif"), {image});
    test::JpegOptions options;
    options.pixel = [](int, int) { return Rgb{20, 40, 200}; };
    test::writeFile(dir.file("j.jpg"), test::encodeJpeg(options));

    // In both orders, since the two can use different builds of libjpeg.
    const auto first = open(dir.file("j.jpg"));
    ASSERT_TRUE(first);
    expectColor(shownPixel(*first, 0, 0, 5, 5), {20, 40, 200}, 8);
    const auto second = open(dir.file("t.tif"));
    ASSERT_TRUE(second);
    expectColor(shownPixel(*second, 0, 0, 5, 5), {200, 40, 20}, 8);
    const auto third = open(dir.file("j.jpg"));
    ASSERT_TRUE(third);
    expectColor(shownPixel(*third, 0, 0, 5, 5), {20, 40, 200}, 8);
}

TEST(Overviews, FlatLargeImagesGetGeneratedLevels) {
    TempDir dir;
    TiffImage image;
    image.width = 6000;
    image.height = 3000;
    image.samples = 1;
    image.bits = 16;
    image.tileSize = 512;
    image.compression = COMPRESSION_ADOBE_DEFLATE;
    image.pixel = [](int x, int) { return x < 3000 ? Rgb{200, 200, 200} : Rgb{40, 40, 40}; };
    writeTiff(dir.file("t.tif"), {image});

    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    const auto& levels = source->subimages()[0].levels;
    ASSERT_GT(levels.size(), 1u);
    EXPECT_FALSE(levels[0].generated);
    EXPECT_TRUE(levels.back().generated);
    EXPECT_LE(std::max(levels.back().size.width, levels.back().size.height), 2048);

    const int coarsest = static_cast<int>(levels.size()) - 1;
    const Size size = levels.back().size;
    expectColor(shownPixel(*source, 0, coarsest, size.width / 4, size.height / 2), {200, 200, 200});
    expectColor(shownPixel(*source, 0, coarsest, size.width * 3 / 4, size.height / 2), {40, 40, 40});
    // Real tiles still come from the file.
    expectColor(shownPixel(*source, 0, 0, 5900, 2900), {40, 40, 40});
}

TEST(Overviews, SmallImagesAreLeftAlone) {
    TempDir dir;
    writeTiff(dir.file("t.tif"), {level(1000, 800, {200, 0, 0}, false)});
    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    ASSERT_EQ(source->subimages()[0].levels.size(), 1u);
    EXPECT_FALSE(source->subimages()[0].levels[0].generated);
}

TEST(Overviews, ACancelledBuildCanBeRetried) {
    TempDir dir;
    TiffImage image;
    image.width = 4000;
    image.height = 3000;
    image.samples = 1;
    image.tileSize = 512;
    image.pixel = [](int, int) { return Rgb{77, 77, 77}; };
    writeTiff(dir.file("t.tif"), {image});

    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    const int coarsest = static_cast<int>(source->subimages()[0].levels.size()) - 1;
    std::stop_source stop;
    stop.request_stop();
    EXPECT_FALSE(source->readTile({0, coarsest, 0, 0}, stop.get_token()).has_value());
    expectColor(shownPixel(*source, 0, coarsest, 10, 10), {77, 77, 77});
}

TEST(DisplayRange, AutoRangeIgnoresOutliersAndStretchesNarrowData) {
    const ImageBuffer image = ImageBuffer::create({1000, 1}, PixelFormat::Gray16);
    auto* row = reinterpret_cast<std::uint16_t*>(image.row(0));
    for (int x = 0; x < 1000; ++x) {
        row[x] = static_cast<std::uint16_t>(1000 + x % 100);  // a narrow range in a 16-bit file
    }
    row[0] = 0;  // single outliers
    row[1] = 65535;

    const std::array samples{image};
    const DisplayRange range = autoRange(samples);
    EXPECT_GE(range.low, 999);
    EXPECT_LE(range.high, 1100);

    const ImageBuffer shown = toDisplay(image, range);
    const auto* out = reinterpret_cast<const std::uint8_t*>(shown.row(0));
    EXPECT_LT(out[4 * 100], 10);    // value 1000: the bottom of the range
    EXPECT_GT(out[4 * 199], 240);   // value 1099: the top of the range
}

TEST(DisplayRange, FlatAndEmptyImagesAreSafe) {
    const ImageBuffer zero = ImageBuffer::create({4, 4}, PixelFormat::Gray16);
    std::memset(zero.data(), 0, zero.stride() * 4);
    const std::array zeros{zero};
    const DisplayRange range = autoRange(zeros);
    EXPECT_GT(range.high, range.low);
    EXPECT_NO_THROW((void)toDisplay(zero, range));

    EXPECT_GT(autoRange({}).high, autoRange({}).low);
}

TEST(DisplayRange, FloatsHandleNansAndNegativeValues) {
    const ImageBuffer image = ImageBuffer::create({4, 1}, PixelFormat::Gray32F);
    auto* row = reinterpret_cast<float*>(image.row(0));
    row[0] = std::numeric_limits<float>::quiet_NaN();
    row[1] = -5.0F;
    row[2] = 5.0F;
    row[3] = std::numeric_limits<float>::infinity();

    const std::array samples{image};
    const DisplayRange range = autoRange(samples);
    EXPECT_EQ(range.low, -5.0);
    EXPECT_EQ(range.high, 5.0);

    const ImageBuffer shown = toDisplay(image, range);
    const auto* out = reinterpret_cast<const std::uint8_t*>(shown.row(0));
    EXPECT_EQ(out[0], 0);       // NaN is black
    EXPECT_EQ(out[4], 0);
    EXPECT_EQ(out[8], 255);
    EXPECT_EQ(out[12], 255);    // infinity clips
}

TEST(DisplayRange, EightBitImagesAreNotCopied) {
    const ImageBuffer image = ImageBuffer::create({4, 4}, PixelFormat::Bgrx8);
    EXPECT_EQ(toDisplay(image, {0, 255}).data(), image.data());
}

TEST(DisplayRange, DefaultsSuitTheKindOfImage) {
    EXPECT_FALSE(usesAutoRangeByDefault(PixelFormat::Bgrx8));
    EXPECT_FALSE(usesAutoRangeByDefault(PixelFormat::Rgba16));  // photographs keep their exposure
    EXPECT_TRUE(usesAutoRangeByDefault(PixelFormat::Gray16));
    EXPECT_TRUE(usesAutoRangeByDefault(PixelFormat::Gray32F));
}

TEST(Shrink, AveragesEveryFormat) {
    const ImageBuffer gray = ImageBuffer::create({2, 2}, PixelFormat::Gray16);
    auto* g = reinterpret_cast<std::uint16_t*>(gray.data());
    g[0] = 100; g[1] = 200; g[2] = 300; g[3] = 400;
    const ImageBuffer small = shrink(gray, {1, 1});
    EXPECT_EQ(*reinterpret_cast<const std::uint16_t*>(small.data()), 250);

    const ImageBuffer floats = ImageBuffer::create({2, 1}, PixelFormat::Rgba32F);
    auto* f = reinterpret_cast<float*>(floats.data());
    for (int i = 0; i < 8; ++i) {
        f[i] = static_cast<float>(i);
    }
    const ImageBuffer floatSmall = shrink(floats, {1, 1});
    EXPECT_FLOAT_EQ(reinterpret_cast<const float*>(floatSmall.data())[1], 3.0F);
}

TEST(Thumbnail, WorksForHighBitDepthAndPyramids) {
    TempDir dir;
    TiffImage gray;
    gray.width = 800;
    gray.height = 400;
    gray.bits = 16;
    gray.samples = 1;
    // Only a narrow range of values, so it would be near black without stretching.
    gray.pixel = [](int x, int) { return x < 400 ? Rgb{2, 2, 2} : Rgb{4, 4, 4}; };
    writeTiff(dir.file("g.tif"), {gray});

    const auto thumbnail = makeThumbnail(dir.file("g.tif"), 256, {});
    ASSERT_TRUE(thumbnail.has_value()) << thumbnail.error();
    EXPECT_EQ(thumbnail->format(), PixelFormat::Bgrx8);
    EXPECT_EQ(thumbnail->size(), (Size{256, 128}));
    const auto* row = reinterpret_cast<const std::uint8_t*>(thumbnail->row(60));
    EXPECT_GT(row[4 * 240], 200);  // the brighter half reaches the top of the stretched range

    writeTiff(dir.file("p.tif"), {level(2000, 1000, {200, 0, 0}, false), level(1000, 500, {200, 0, 0}, true),
                                  level(300, 150, {200, 0, 0}, true)});
    const auto pyramid = makeThumbnail(dir.file("p.tif"), 256, {});
    ASSERT_TRUE(pyramid.has_value()) << pyramid.error();
    EXPECT_EQ(pyramid->width(), 256);
}

// Formats the helper does not write go through libtiff's RGBA interface in the reader.
TEST(Tiff, PaletteAndOneBitImagesUseTheFallback) {
    TempDir dir;
    {
        TIFF* tiff = TIFFOpen(dir.file("palette.tif").string().c_str(), "w");
        ASSERT_NE(tiff, nullptr);
        TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, 40u);
        TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, 30u);
        TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, std::uint16_t{8});
        TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, std::uint16_t{1});
        TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, std::uint16_t{PHOTOMETRIC_PALETTE});
        TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 7u);  // short last strip
        std::array<std::uint16_t, 256> red{};
        std::array<std::uint16_t, 256> green{};
        std::array<std::uint16_t, 256> blue{};
        red[1] = 200 * 257;
        green[1] = 40 * 257;
        blue[1] = 20 * 257;
        TIFFSetField(tiff, TIFFTAG_COLORMAP, red.data(), green.data(), blue.data());
        std::vector<std::uint8_t> row(40, 1);
        for (std::uint32_t y = 0; y < 30; ++y) {
            ASSERT_EQ(TIFFWriteScanline(tiff, row.data(), y, 0), 1);
        }
        TIFFClose(tiff);
    }
    {
        TIFF* tiff = TIFFOpen(dir.file("bits.tif").string().c_str(), "w");
        ASSERT_NE(tiff, nullptr);
        TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, 40u);
        TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, 30u);
        TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, std::uint16_t{1});
        TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, std::uint16_t{1});
        TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, std::uint16_t{PHOTOMETRIC_MINISBLACK});
        TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 7u);
        // Left half white, right half black.
        std::vector<std::uint8_t> row(5, 0);
        row[0] = 0xFF;
        row[1] = 0xFF;
        row[2] = 0xF0;
        for (std::uint32_t y = 0; y < 30; ++y) {
            ASSERT_EQ(TIFFWriteScanline(tiff, row.data(), y, 0), 1);
        }
        TIFFClose(tiff);
    }

    const auto palette = open(dir.file("palette.tif"));
    ASSERT_TRUE(palette);
    expectColor(shownPixel(*palette, 0, 0, 5, 3), {200, 40, 20});
    expectColor(shownPixel(*palette, 0, 0, 39, 29), {200, 40, 20});  // not flipped or lost in the short strip

    const auto bits = open(dir.file("bits.tif"));
    ASSERT_TRUE(bits);
    expectColor(shownPixel(*bits, 0, 0, 5, 3), {255, 255, 255});
    expectColor(shownPixel(*bits, 0, 0, 35, 29), {0, 0, 0});
}

TEST(Tiff, RowsAreNotFlippedByTheFallback) {
    TempDir dir;
    TIFF* tiff = TIFFOpen(dir.file("t.tif").string().c_str(), "w");
    ASSERT_NE(tiff, nullptr);
    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, 8u);
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, 20u);
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, std::uint16_t{1});
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, std::uint16_t{1});
    TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, std::uint16_t{PHOTOMETRIC_MINISBLACK});
    TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 7u);
    for (std::uint32_t y = 0; y < 20; ++y) {
        std::uint8_t value = y < 3 ? 0xFF : 0x00;  // white at the top only
        ASSERT_EQ(TIFFWriteScanline(tiff, &value, y, 0), 1);
    }
    TIFFClose(tiff);

    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    expectColor(shownPixel(*source, 0, 0, 2, 1), {255, 255, 255});
    expectColor(shownPixel(*source, 0, 0, 2, 18), {0, 0, 0});
}

TEST(Tiff, MasksAreNotShownAsPagesOrLevels) {
    TempDir dir;
    TiffImage mask = level(1000, 800, {255, 255, 255}, false);
    TiffImage main = level(1000, 800, {200, 0, 0}, false);
    writeTiff(dir.file("t.tif"), {main, mask});
    // Mark the second directory as a transparency mask.
    TIFF* tiff = TIFFOpen(dir.file("t.tif").string().c_str(), "r+");
    ASSERT_NE(tiff, nullptr);
    ASSERT_TRUE(TIFFSetDirectory(tiff, 1));
    TIFFSetField(tiff, TIFFTAG_SUBFILETYPE, static_cast<std::uint32_t>(FILETYPE_MASK));
    ASSERT_TRUE(TIFFRewriteDirectory(tiff));
    TIFFClose(tiff);

    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    EXPECT_EQ(source->subimages().size(), 1u);
    EXPECT_EQ(source->subimages()[0].levels.size(), 1u);
}

TEST(Tiff, Cmyk8IsShownAndCmyk16IsRefusedClearly) {
    TempDir dir;
    const auto write = [&](const std::filesystem::path& path, std::uint16_t bits) {
        TIFF* tiff = TIFFOpen(path.string().c_str(), "w");
        ASSERT_NE(tiff, nullptr);
        TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, 16u);
        TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, 8u);
        TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, bits);
        TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, std::uint16_t{4});
        TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, std::uint16_t{PHOTOMETRIC_SEPARATED});
        TIFFSetField(tiff, TIFFTAG_INKSET, std::uint16_t{INKSET_CMYK});
        TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, std::uint16_t{PLANARCONFIG_CONTIG});
        TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 8u);
        // Full magenta and yellow ink makes red; the byte pattern suits both depths.
        std::vector<std::uint8_t> row(16u * 4u * (bits / 8u), 0);
        for (std::size_t i = 0; i < 16; ++i) {
            for (std::size_t b = 0; b < bits / 8u; ++b) {
                row[(i * 4 + 1) * (bits / 8u) + b] = 255;
                row[(i * 4 + 2) * (bits / 8u) + b] = 255;
            }
        }
        for (std::uint32_t y = 0; y < 8; ++y) {
            ASSERT_EQ(TIFFWriteScanline(tiff, row.data(), y, 0), 1);
        }
        TIFFClose(tiff);
    };
    write(dir.file("c8.tif"), 8);
    write(dir.file("c16.tif"), 16);

    const auto source = open(dir.file("c8.tif"));
    ASSERT_TRUE(source);
    expectColor(shownPixel(*source, 0, 0, 4, 4), {255, 0, 0}, 5);

    const auto refused = openImageSource(dir.file("c16.tif"));
    ASSERT_FALSE(refused.has_value());
    EXPECT_NE(refused.error().find("CMYK"), std::string::npos) << refused.error();
}

TEST(Shrink, OddSizesAndSplitBlocksCoverEveryPixel) {
    // A non-integer ratio, with a horizontal ramp that shows any lost or shifted column.
    const Size full{1001, 601};
    const Size small{501, 301};
    const ImageBuffer source = ImageBuffer::create(full, PixelFormat::Gray16);
    for (int y = 0; y < full.height; ++y) {
        auto* row = reinterpret_cast<std::uint16_t*>(source.row(y));
        for (int x = 0; x < full.width; ++x) {
            row[x] = static_cast<std::uint16_t>(x * 60);
        }
    }

    const ImageBuffer whole = shrink(source, small);
    const ImageBuffer split = ImageBuffer::create(small, PixelFormat::Gray16);
    std::memset(split.data(), 0, split.stride() * static_cast<std::size_t>(small.height));
    constexpr int kBlock = 128;
    for (int y = 0; y < full.height; y += kBlock) {
        for (int x = 0; x < full.width; x += kBlock) {
            shrinkBlockInto(source.view(x, y, std::min(kBlock, full.width - x), std::min(kBlock, full.height - y)), x, y, full, split);
        }
    }

    for (const ImageBuffer* image : {&whole, &split}) {
        for (int y = 0; y < small.height; y += 50) {
            const auto* row = reinterpret_cast<const std::uint16_t*>(image->row(y));
            for (int x = 0; x < small.width; ++x) {
                const double expected = (x + 0.5) * full.width / small.width * 60;
                // Within two source pixels, even where a block edge splits a target pixel.
                ASSERT_NEAR(row[x], expected, 2.5 * 60) << image << " at " << x << "," << y;
            }
        }
    }
}

TEST(Shrink, RoundsInsteadOfDarkening) {
    const ImageBuffer image = ImageBuffer::create({2, 2}, PixelFormat::Bgrx8);
    auto* p = reinterpret_cast<std::uint8_t*>(image.data());
    const std::array<std::uint8_t, 4> values{10, 11, 11, 11};  // averages 10.75
    for (std::size_t i = 0; i < 4; ++i) {
        std::memset(p + i * 4, values[i], 4);
    }
    EXPECT_EQ(*reinterpret_cast<const std::uint8_t*>(shrink(image, {1, 1}).data()), 11);
}
