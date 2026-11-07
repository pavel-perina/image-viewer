#include <random>
#include <cmath>
#include <stop_token>
#include <gtest/gtest.h>

#include <sqlite3.h>
#include <tiffio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "core/image_ops.h"
#include "core/image_source.h"
#include "core/thumbnail.h"
#include "core/thumbnail_cache.h"
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

TEST(Overviews, HalvingAcrossTileEdgesIsTheSameAsHalvingAtOnce) {
    TempDir dir;
    TiffImage image;
    image.width = 3001;
    image.height = 1537;  // odd sizes, and rows of tiles that end on both odd and even rows
    image.samples = 3;
    image.tileSize = 512;
    image.compression = COMPRESSION_ADOBE_DEFLATE;
    image.pixel = [](int x, int y) {
        return Rgb{static_cast<std::uint8_t>(x * 7 + y * 3), static_cast<std::uint8_t>((x ^ y) * 5), static_cast<std::uint8_t>(y * 11)};
    };
    writeTiff(dir.file("t.tif"), {image});
    const auto source = open(dir.file("t.tif"));
    ASSERT_TRUE(source);
    const auto& levels = source->subimages()[0].levels;
    ASSERT_GE(levels.size(), 2u);
    ASSERT_TRUE(levels[1].generated);
    ASSERT_EQ(levels[1].size, (Size{1501, 769}));

    const ImageBuffer whole = ImageBuffer::create(levels[0].size, PixelFormat::Bgrx8);
    const int tiles = levels[0].tileSize.width;
    for (int row = 0; row * tiles < whole.height(); ++row) {
        for (int col = 0; col * tiles < whole.width(); ++col) {
            const auto tile = source->readTile({0, 0, col, row}, {});
            ASSERT_TRUE(tile);
            for (int y = 0; y < tile->height(); ++y) {
                std::memcpy(reinterpret_cast<std::uint8_t*>(whole.row(row * tiles + y)) + static_cast<std::size_t>(col * tiles) * 4,
                            tile->row(y), static_cast<std::size_t>(tile->width()) * 4);
            }
        }
    }
    const ImageBuffer expected = halve(whole);

    const int levelTile = levels[1].tileSize.width;
    for (int row = 0; row * levelTile < expected.height(); ++row) {
        for (int col = 0; col * levelTile < expected.width(); ++col) {
            const auto tile = source->readTile({0, 1, col, row}, {});
            ASSERT_TRUE(tile);
            for (int y = 0; y < tile->height(); ++y) {
                ASSERT_EQ(std::memcmp(tile->row(y),
                                      reinterpret_cast<const std::uint8_t*>(expected.row(row * levelTile + y)) + static_cast<std::size_t>(col * levelTile) * 4,
                                      static_cast<std::size_t>(tile->width()) * 4), 0) << "row " << row * levelTile + y;
            }
        }
    }
}

TEST(Halve, KeepsEveryFlatValueExactly) {
    for (int value = 0; value < 256; ++value) {
        const ImageBuffer flat = ImageBuffer::create({8, 8}, PixelFormat::Bgrx8);
        std::memset(flat.data(), value, flat.stride() * 8);
        const ImageBuffer half = halve(flat);
        ASSERT_EQ(half.size(), (Size{4, 4}));
        for (int y = 0; y < 4; ++y) {
            const auto* row = reinterpret_cast<const std::uint8_t*>(half.row(y));
            for (int i = 0; i < 16; ++i) {
                ASSERT_EQ(row[i], value) << "at " << y << "," << i;
            }
        }
    }
}

TEST(Halve, MixesDisplayPixelsInLinearLight) {
    // Half black and half white pixels are half as much light, which is 188 in sRGB and not 128.
    const ImageBuffer stripes = ImageBuffer::create({64, 64}, PixelFormat::Bgrx8);
    for (int y = 0; y < 64; ++y) {
        std::memset(stripes.row(y), y % 2 != 0 ? 255 : 0, static_cast<std::size_t>(64) * 4);
    }
    const ImageBuffer half = halve(stripes);
    const auto* row = reinterpret_cast<const std::uint8_t*>(half.row(16));
    EXPECT_NEAR(row[0], 188, 1);
    EXPECT_NEAR(row[1], 188, 1);
    EXPECT_NEAR(row[2], 188, 1);
}

TEST(Halve, LeavesLessFalsePatternFromFineStripesThanAveraging) {
    // Stripes of 3 pixels are too fine for half the size, and what is left of them is false.
    const auto contrast = [](const ImageBuffer& image) {
        const auto* row = reinterpret_cast<const std::uint16_t*>(image.row(image.height() / 2));
        const auto [low, high] = std::minmax_element(row + 8, row + image.width() - 8);
        return static_cast<double>(*high) - *low;
    };
    const ImageBuffer stripes = ImageBuffer::create({300, 16}, PixelFormat::Gray16);
    for (int y = 0; y < 16; ++y) {
        auto* row = reinterpret_cast<std::uint16_t*>(stripes.row(y));
        for (int x = 0; x < 300; ++x) {
            row[x] = x % 3 == 0 ? 60000 : 0;
        }
    }
    EXPECT_LT(contrast(halve(stripes)), 0.5 * contrast(shrink(stripes, {150, 8})));
}

TEST(Halve, KeepsTheOtherFormatsAndTheirRange) {
    const ImageBuffer floats = ImageBuffer::create({4, 4}, PixelFormat::Gray32F);
    for (int y = 0; y < 4; ++y) {
        std::fill_n(reinterpret_cast<float*>(floats.row(y)), 4, 2.5F);
    }
    const ImageBuffer halfFloats = halve(floats);
    EXPECT_FLOAT_EQ(*reinterpret_cast<const float*>(halfFloats.row(1)), 2.5F);
    const ImageBuffer shorts = ImageBuffer::create({4, 4}, PixelFormat::Rgba16);
    for (int y = 0; y < 4; ++y) {
        std::fill_n(reinterpret_cast<std::uint16_t*>(shorts.row(y)), 16, std::uint16_t{65535});
    }
    const ImageBuffer halfShorts = halve(shorts);
    EXPECT_EQ(*reinterpret_cast<const std::uint16_t*>(halfShorts.row(1)), 65535);
}

TEST(Halve, BandsSideBySideGiveWhatOneAtATimeGives) {
    // Large enough to be split between threads; small bands of rows are not.
    std::mt19937 random(7);
    for (const PixelFormat format : {PixelFormat::Bgrx8, PixelFormat::Gray16, PixelFormat::Rgba16, PixelFormat::Gray32F, PixelFormat::Rgba32F}) {
        SCOPED_TRACE(static_cast<int>(format));
        const ImageBuffer image = ImageBuffer::create({2001, 1203}, format);
        const std::size_t rowBytes = static_cast<std::size_t>(image.width()) * bytesPerPixel(format);
        for (int y = 0; y < image.height(); ++y) {
            auto* row = reinterpret_cast<std::uint8_t*>(image.row(y));
            if (format == PixelFormat::Gray32F || format == PixelFormat::Rgba32F) {
                auto* values = reinterpret_cast<float*>(row);
                for (std::size_t i = 0; i < rowBytes / 4; ++i) {
                    values[i] = static_cast<float>(random() % 1000) / 999.0F;
                }
            } else {
                for (std::size_t i = 0; i < rowBytes; ++i) {
                    row[i] = static_cast<std::uint8_t>(random());
                }
            }
        }
        const ImageBuffer together = halve(image);
        const ImageBuffer oneByOne = ImageBuffer::create(together.size(), format);
        for (int first = 0; first < oneByOne.height(); first += 50) {
            halveRows(image, 0, image.height(), oneByOne, first, std::min(first + 50, oneByOne.height()));
        }
        const std::size_t halfRowBytes = static_cast<std::size_t>(together.width()) * bytesPerPixel(format);
        for (int y = 0; y < together.height(); ++y) {
            ASSERT_EQ(std::memcmp(together.row(y), oneByOne.row(y), halfRowBytes), 0) << "row " << y;
        }
    }
}

TEST(Halve, EightBitsComeOutWithinOneStepOfTheExactResult) {
    const auto toLinear = [](int v) {
        const double c = v / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    const auto toSrgb = [](double c) {
        return std::lround(255 * (c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055));
    };
    std::mt19937 random(11);
    const ImageBuffer image = ImageBuffer::create({64, 64}, PixelFormat::Bgrx8);
    for (int y = 0; y < 64; ++y) {
        for (int i = 0; i < 256; ++i) {
            reinterpret_cast<std::uint8_t*>(image.row(y))[i] = static_cast<std::uint8_t>(random());
        }
    }
    const ImageBuffer half = halve(image);
    constexpr int kWeights[] = {1, 3, 3, 1};
    int exact = 0;
    for (int ty = 0; ty < 32; ++ty) {
        for (int tx = 0; tx < 32; ++tx) {
            for (int c = 0; c < 3; ++c) {
                double sum = 0;
                for (int j = 0; j < 4; ++j) {
                    for (int i = 0; i < 4; ++i) {
                        const int sy = std::clamp(2 * ty - 1 + j, 0, 63);
                        const int sx = std::clamp(2 * tx - 1 + i, 0, 63);
                        sum += kWeights[j] * kWeights[i] * toLinear(reinterpret_cast<const std::uint8_t*>(image.row(sy))[sx * 4 + c]);
                    }
                }
                const int got = reinterpret_cast<const std::uint8_t*>(half.row(ty))[tx * 4 + c];
                ASSERT_LE(std::abs(got - toSrgb(sum / 64)), 1);
                exact += got == toSrgb(sum / 64);
            }
        }
    }
    EXPECT_GE(exact, 32 * 32 * 3 * 99 / 100);
}

TEST(Resize, KeepsEveryFlatValueExactly) {
    for (int value = 0; value < 256; ++value) {
        const ImageBuffer flat = ImageBuffer::create({40, 30}, PixelFormat::Bgrx8);
        std::memset(flat.data(), value, flat.stride() * 30);
        for (const Size size : {Size{13, 11}, Size{1, 1}, Size{97, 61}}) {
            const ImageBuffer resized = resize(flat, size);
            ASSERT_EQ(resized.size(), size);
            for (int y = 0; y < size.height; ++y) {
                const auto* row = reinterpret_cast<const std::uint8_t*>(resized.row(y));
                for (int i = 0; i < size.width * 4; ++i) {
                    ASSERT_EQ(row[i], value) << size.width << "x" << size.height << " at " << y << "," << i;
                }
            }
        }
    }
}

TEST(Resize, MatchesTheSharpKernelWhenReducing) {
    // Reducing in two steps must give what the formula of Magic Kernel Sharp 2013 gives in one,
    // away from the edges, where the two handle what lies beyond the image differently.
    const auto kernel = [](double x) {
        x = std::abs(x);
        return x <= 0.5 ? 17.0 / 16 - 1.75 * x * x : x <= 1.5 ? (1 - x) * (1.75 - x) : x <= 2.5 ? -0.125 * (x - 2.5) * (x - 2.5) : 0.0;
    };
    constexpr int kWidth = 1000;
    const ImageBuffer source = ImageBuffer::create({kWidth, 1}, PixelFormat::Gray32F);
    auto* in = reinterpret_cast<float*>(source.row(0));
    for (int x = 0; x < kWidth; ++x) {
        in[x] = static_cast<float>(100 + 50 * std::sin(x * 0.11) + 20 * std::sin(x * 0.53));
    }
    for (const int width : {500, 333, 137, 61}) {
        const ImageBuffer resized = resize(source, {width, 1});
        const double ratio = static_cast<double>(kWidth) / width;
        const auto* out = reinterpret_cast<const float*>(resized.row(0));
        for (int x = 4; x < width - 4; ++x) {
            const double centre = (x + 0.5) * ratio;
            double sum = 0;
            double weights = 0;
            for (int i = static_cast<int>(std::ceil(centre - 0.5 - 2.5 * ratio)); i <= static_cast<int>(std::floor(centre - 0.5 + 2.5 * ratio)); ++i) {
                const double weight = kernel((i + 0.5 - centre) / ratio);
                sum += weight * in[i];
                weights += weight;
            }
            ASSERT_NEAR(out[x], sum / weights, 0.05) << "width " << width << " at " << x;
        }
    }
}

TEST(Resize, GivesUpWhenStopped) {
    const ImageBuffer source = ImageBuffer::create({64, 64}, PixelFormat::Bgrx8);
    std::memset(source.data(), 90, source.stride() * 64);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_EQ(resize(source, {8, 8}, stop.get_token()).width(), 0);
}

TEST(Thumbnail, ReadsAFlatImageDirectlyRatherThanGeneratingLevels) {
    TempDir dir;
    TiffImage image;
    image.width = 4000;
    image.height = 3000;
    image.samples = 1;
    image.tileSize = 512;
    image.pixel = [](int x, int) { return x < 2000 ? Rgb{200, 200, 200} : Rgb{40, 40, 40}; };
    writeTiff(dir.file("t.tif"), {image});
    const auto thumbnail = makeThumbnail(dir.file("t.tif"), 164, {});
    ASSERT_TRUE(thumbnail) << thumbnail.error();
    EXPECT_EQ(thumbnail->size(), (Size{164, 123}));
    const auto* row = reinterpret_cast<const std::uint8_t*>(thumbnail->row(60));
    EXPECT_EQ(row[10 * 4], 200);
    EXPECT_EQ(row[150 * 4], 40);
}

TEST(Resize, MixesDisplayPixelsInLinearLight) {
    const ImageBuffer stripes = ImageBuffer::create({64, 64}, PixelFormat::Bgrx8);
    for (int y = 0; y < 64; ++y) {
        std::memset(stripes.row(y), y % 2 != 0 ? 255 : 0, static_cast<std::size_t>(64) * 4);
    }
    const ImageBuffer resized = resize(stripes, {32, 32});
    const auto* row = reinterpret_cast<const std::uint8_t*>(resized.row(16));
    EXPECT_NEAR(row[0], 188, 2);
}

TEST(Resize, LeavesLessFalsePatternFromFineStripesThanAveraging) {
    const auto contrast = [](const ImageBuffer& image) {
        const auto* row = reinterpret_cast<const std::uint16_t*>(image.row(image.height() / 2));
        const auto [low, high] = std::minmax_element(row + 8, row + image.width() - 8);
        return static_cast<double>(*high) - *low;
    };
    const ImageBuffer stripes = ImageBuffer::create({300, 16}, PixelFormat::Gray16);
    for (int y = 0; y < 16; ++y) {
        auto* row = reinterpret_cast<std::uint16_t*>(stripes.row(y));
        for (int x = 0; x < 300; ++x) {
            row[x] = x % 3 == 0 ? 60000 : 0;
        }
    }
    EXPECT_LT(contrast(resize(stripes, {150, 8})), 0.5 * contrast(shrink(stripes, {150, 8})));
}

TEST(Resize, OvershootStaysInsideTheRange) {
    const ImageBuffer edge = ImageBuffer::create({40, 8}, PixelFormat::Gray16);
    for (int y = 0; y < 8; ++y) {
        auto* row = reinterpret_cast<std::uint16_t*>(edge.row(y));
        for (int x = 0; x < 40; ++x) {
            row[x] = x < 20 ? 0 : 65535;
        }
    }
    const ImageBuffer resized = resize(edge, {17, 3});
    const auto* row = reinterpret_cast<const std::uint16_t*>(resized.row(1));
    EXPECT_EQ(row[0], 0);
    EXPECT_EQ(row[6], 0) << "the dip before the edge would wrap to bright";
    EXPECT_EQ(row[16], 65535);
    EXPECT_EQ(row[10], 65535) << "the peak after the edge would wrap to dark";
}

TEST(Resize, HandlesEnlargingAndTheSmallestImages) {
    const ImageBuffer one = ImageBuffer::create({1, 1}, PixelFormat::Rgba16);
    std::fill_n(reinterpret_cast<std::uint16_t*>(one.row(0)), 4, std::uint16_t{1234});
    const ImageBuffer large = resize(one, {5, 3});
    EXPECT_EQ(*reinterpret_cast<const std::uint16_t*>(large.row(2)), 1234);
    const ImageBuffer same = resize(one, {1, 1});
    EXPECT_EQ(*reinterpret_cast<const std::uint16_t*>(same.row(0)), 1234);
}

namespace {

ThumbnailCache::Key cacheKey(const std::string& path = "/a.png", int edge = 164, const std::string& color = "srgb") {
    return {path, edge, color, 1000, 2000};
}

ImageBuffer flatImage(Size size, int value) {
    const ImageBuffer image = ImageBuffer::create(size, PixelFormat::Bgrx8);
    std::memset(image.data(), value, image.stride() * static_cast<std::size_t>(size.height));
    return image;
}

}  // namespace

TEST(ThumbnailCache, KeepsPixelsBetweenRuns) {
    TempDir dir;
    {
        const auto cache = ThumbnailCache::open(dir.file("c.db"), 1 << 20);
        ASSERT_TRUE(cache);
        EXPECT_FALSE(cache->find(cacheKey()));
        cache->store(cacheKey(), flatImage({20, 10}, 123));
    }
    const auto cache = ThumbnailCache::open(dir.file("c.db"), 1 << 20);
    ASSERT_TRUE(cache);
    const auto found = cache->find(cacheKey());
    ASSERT_TRUE(found);
    EXPECT_EQ(found->size(), (Size{20, 10}));
    EXPECT_EQ(found->format(), PixelFormat::Bgrx8);
    EXPECT_NEAR(static_cast<int>(reinterpret_cast<const std::uint8_t*>(found->row(5))[20]), 123, 1);
}

TEST(ThumbnailCache, MissesWhenAnythingItDependsOnDiffers) {
    TempDir dir;
    const auto cache = ThumbnailCache::open(dir.file("c.db"), 1 << 20);
    ASSERT_TRUE(cache);
    cache->store(cacheKey(), flatImage({8, 8}, 50));
    EXPECT_TRUE(cache->find(cacheKey()));
    EXPECT_FALSE(cache->find(cacheKey("/b.png")));
    EXPECT_FALSE(cache->find(cacheKey("/a.png", 328)));
    EXPECT_FALSE(cache->find(cacheKey("/a.png", 164, "off")));
    ThumbnailCache::Key changed = cacheKey();
    changed.modified = 1001;
    EXPECT_FALSE(cache->find(changed));
    changed = cacheKey();
    changed.size = 2001;
    EXPECT_FALSE(cache->find(changed));
    // A new version of the file replaces the old thumbnail rather than adding to it.
    changed.size = 2001;
    cache->store(changed, flatImage({8, 8}, 60));
    EXPECT_TRUE(cache->find(changed));
    EXPECT_FALSE(cache->find(cacheKey()));
}

TEST(ThumbnailCache, ReplacesADamagedFile) {
    TempDir dir;
    {
        std::ofstream file(dir.file("c.db"), std::ios::binary);
        file << std::string(5000, 'x');
    }
    const auto cache = ThumbnailCache::open(dir.file("c.db"), 1 << 20);
    ASSERT_TRUE(cache);
    cache->store(cacheKey(), flatImage({8, 8}, 50));
    EXPECT_TRUE(cache->find(cacheKey()));
}

TEST(ThumbnailCache, ADamagedEntryIsNotFound) {
    TempDir dir;
    const auto cache = ThumbnailCache::open(dir.file("c.db"), 1 << 20);
    ASSERT_TRUE(cache);
    // With detail, so that half of it is cut from the pixels rather than from the headers.
    const ImageBuffer picture = flatImage({128, 128}, 50);
    std::mt19937 random(1);
    for (int y = 0; y < picture.height(); ++y) {
        auto* row = reinterpret_cast<std::uint8_t*>(picture.row(y));
        std::generate_n(row, picture.width() * 4, [&] { return static_cast<std::uint8_t>(random()); });
    }
    cache->store(cacheKey(), picture);
    ASSERT_TRUE(cache->find(cacheKey()));

    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(dir.file("c.db").c_str(), &db), SQLITE_OK);
    // Cut short, then with a valid start followed by nonsense.
    for (const char* damage : {"UPDATE thumbnails SET bytes = substr(bytes, 1, length(bytes) / 2)",
                               "UPDATE thumbnails SET bytes = substr(bytes, 1, 2) || zeroblob(200)"}) {
        ASSERT_EQ(sqlite3_exec(db, damage, nullptr, nullptr, nullptr), SQLITE_OK);
        EXPECT_FALSE(cache->find(cacheKey())) << damage;
    }
    sqlite3_close(db);

    cache->store(cacheKey(), picture);
    EXPECT_TRUE(cache->find(cacheKey()));
}

TEST(ThumbnailCache, IsSharedByCopiesOfTheProgramRunningAtOnce) {
    TempDir dir;
    const auto first = ThumbnailCache::open(dir.file("c.db"), 1 << 20);
    const auto second = ThumbnailCache::open(dir.file("c.db"), 1 << 20);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    first->store(cacheKey("/one.png"), flatImage({16, 16}, 10));
    second->store(cacheKey("/two.png"), flatImage({16, 16}, 20));
    EXPECT_TRUE(second->find(cacheKey("/one.png")));
    EXPECT_TRUE(first->find(cacheKey("/two.png")));
}

TEST(ThumbnailCache, LeavesALockedFileToTheCopyHoldingIt) {
    TempDir dir;
    const auto running = ThumbnailCache::open(dir.file("c.db"), 1 << 20);
    ASSERT_TRUE(running);
    running->store(cacheKey(), flatImage({16, 16}, 10));

    // Another copy in the middle of a long write, as far as opening can tell.
    sqlite3* holder = nullptr;
    ASSERT_EQ(sqlite3_open(dir.file("c.db").c_str(), &holder), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(holder, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_FALSE(ThumbnailCache::open(dir.file("c.db"), 1 << 20)) << "no cache this time";
    sqlite3_exec(holder, "COMMIT", nullptr, nullptr, nullptr);
    sqlite3_close(holder);

    EXPECT_TRUE(running->find(cacheKey())) << "and the file was not taken from under it";
    running->store(cacheKey("/b.png"), flatImage({16, 16}, 10));
    const auto later = ThumbnailCache::open(dir.file("c.db"), 1 << 20);
    ASSERT_TRUE(later);
    EXPECT_TRUE(later->find(cacheKey()));
    EXPECT_TRUE(later->find(cacheKey("/b.png")));
}

TEST(ThumbnailCache, LeavesANewerVersionToTheProgramThatMadeIt) {
    TempDir dir;
    ASSERT_TRUE(ThumbnailCache::open(dir.file("c.db"), 1 << 20));
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(dir.file("c.db").c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "PRAGMA user_version=999", nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(db);

    EXPECT_FALSE(ThumbnailCache::open(dir.file("c.db"), 1 << 20));
    ASSERT_EQ(sqlite3_open(dir.file("c.db").c_str(), &db), SQLITE_OK);
    int version = 0;
    sqlite3_exec(db, "PRAGMA user_version", [](void* out, int, char** values, char**) { *static_cast<int*>(out) = std::atoi(values[0]); return 0; }, &version, nullptr);
    sqlite3_close(db);
    EXPECT_EQ(version, 999);
}

TEST(ThumbnailCache, ClearingLeavesNothingOfThemInTheFile) {
    TempDir dir;
    const auto cache = ThumbnailCache::open(dir.file("c.db"), 1 << 26);
    const auto other = ThumbnailCache::open(dir.file("c.db"), 1 << 26);  // another copy of the program
    ASSERT_TRUE(cache);
    ASSERT_TRUE(other);
    std::mt19937 random(2);
    const ImageBuffer noise = ImageBuffer::create({128, 128}, PixelFormat::Bgrx8);
    for (int y = 0; y < noise.height(); ++y) {
        auto* row = reinterpret_cast<std::uint8_t*>(noise.row(y));
        std::generate_n(row, noise.width() * 4, [&] { return static_cast<std::uint8_t>(random()); });
    }
    for (int i = 0; i < 40; ++i) {
        cache->store(cacheKey("/holiday/secret_" + std::to_string(i) + ".jpg"), noise);
    }
    const ThumbnailCache::Usage before = cache->usage();
    EXPECT_EQ(before.count, 40u);
    EXPECT_GT(before.bytes, 40u * 1000);
    // With the log, where SQLite writes first.
    const auto onDisk = [&] {
        std::error_code missing;
        const auto log = std::filesystem::file_size(dir.file("c.db").string() + "-wal", missing);
        return std::filesystem::file_size(dir.file("c.db")) + (missing ? 0 : log);
    };
    const auto sizeBefore = onDisk();

    EXPECT_TRUE(cache->clear());
    EXPECT_EQ(cache->usage().count, 0u);
    EXPECT_EQ(other->usage().count, 0u);
    EXPECT_FALSE(other->find(cacheKey("/holiday/secret_0.jpg")));
    EXPECT_LT(onDisk(), sizeBefore / 4) << "rewritten, not only marked free: " << sizeBefore << " -> " << onDisk();
    for (const char* suffix : {"", "-wal"}) {
        std::ifstream file(dir.file("c.db").string() + suffix, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        EXPECT_EQ(bytes.find("secret_"), std::string::npos) << "a path is left in c.db" << suffix;
    }

    // Both go on working.
    other->store(cacheKey("/after.png"), flatImage({8, 8}, 1));
    EXPECT_TRUE(cache->find(cacheKey("/after.png")));
}

TEST(ThumbnailCache, ReturnsNullWhereItCannotBeCreated) {
    EXPECT_FALSE(ThumbnailCache::open("/does/not/exist/c.db", 1 << 20));
    EXPECT_FALSE(makeCachedThumbnail(nullptr, "/does/not/exist.jpg", 164, {}).has_value());
}

TEST(ThumbnailCache, TrimDropsTheLeastRecentlyUsed) {
    TempDir dir;
    const auto cache = ThumbnailCache::open(dir.file("c.db"), 1 << 20);
    ASSERT_TRUE(cache);
    std::mt19937 random(1);  // noise, so that the entries have some size
    const ImageBuffer noise = ImageBuffer::create({128, 128}, PixelFormat::Bgrx8);
    for (int y = 0; y < 128; ++y) {
        auto* row = reinterpret_cast<std::uint8_t*>(noise.row(y));
        for (int i = 0; i < 128 * 4; ++i) {
            row[i] = static_cast<std::uint8_t>(random());
        }
    }
    for (int i = 0; i < 80; ++i) {
        cache->store(cacheKey("/" + std::to_string(i)), noise);
    }
    int before = 0;
    for (int i = 0; i < 80; ++i) {
        before += cache->find(cacheKey("/" + std::to_string(i))).has_value() ? 1 : 0;
    }
    ASSERT_EQ(before, 80) << "the limit is meant to be reached only by the next step";

    const auto small = ThumbnailCache::open(dir.file("c.db"), 200 * 1024);
    ASSERT_TRUE(small);
    int after = 0;
    for (int i = 0; i < 80; ++i) {
        after += small->find(cacheKey("/" + std::to_string(i))).has_value() ? 1 : 0;
    }
    EXPECT_GT(after, 0);
    EXPECT_LT(after, 80);
}

TEST(ThumbnailCache, ServesAFileItHasSeenWithoutReadingIt) {
    TempDir dir;
    const auto path = dir.file("a.jpg");
    {
        test::JpegOptions options;
        options.width = 400;
        options.height = 200;
        const std::vector<std::uint8_t> bytes = test::encodeJpeg(options);
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const auto cache = ThumbnailCache::open(dir.file("c.db"), 1 << 20);
    ASSERT_TRUE(cache);
    const auto first = makeCachedThumbnail(cache.get(), path, 164, {});
    ASSERT_TRUE(first);
    EXPECT_EQ(first->size(), (Size{164, 82}));

    // The same size and time, but no longer an image: only the cache can answer now.
    const auto time = std::filesystem::last_write_time(path);
    const auto size = std::filesystem::file_size(path);
    {
        std::ofstream file(path, std::ios::binary);
        file << std::string(size, 'x');
    }
    std::filesystem::last_write_time(path, time);
    const auto second = makeCachedThumbnail(cache.get(), path, 164, {});
    ASSERT_TRUE(second) << second.error();
    EXPECT_EQ(second->size(), (Size{164, 82}));

    ColorTarget off;
    off.enabled = false;
    EXPECT_FALSE(makeCachedThumbnail(cache.get(), path, 164, {}, off)) << "another colour setting is another thumbnail";
}

TEST(ThumbnailCache, CanBeUsedFromSeveralThreads) {
    TempDir dir;
    const auto cache = ThumbnailCache::open(dir.file("c.db"), 1 << 20);
    ASSERT_TRUE(cache);
    std::vector<std::thread> threads;
    std::atomic<int> found = 0;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < 40; ++i) {
                const auto key = cacheKey("/" + std::to_string(t) + "/" + std::to_string(i));
                cache->store(key, flatImage({16, 16}, i));
                found += cache->find(key).has_value() ? 1 : 0;
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(found, 160);
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
    EXPECT_FALSE(usesAutoRangeByDefault(PixelFormat::Gray16));
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
    // Only a narrow range of values, which stays near black because 16 bits are not stretched.
    gray.pixel = [](int x, int) { return x < 400 ? Rgb{2, 2, 2} : Rgb{4, 4, 4}; };
    writeTiff(dir.file("g16.tif"), {gray});
    const auto unstretched = makeThumbnail(dir.file("g16.tif"), 256, {});
    ASSERT_TRUE(unstretched.has_value()) << unstretched.error();
    EXPECT_LT(reinterpret_cast<const std::uint8_t*>(unstretched->row(60))[4 * 240], 20);

    // Floats are.
    gray.bits = 32;
    gray.sampleFormat = SAMPLEFORMAT_IEEEFP;
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
