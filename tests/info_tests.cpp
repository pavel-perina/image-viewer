#include <gtest/gtest.h>

#include <tiffio.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <numeric>
#include <stop_token>
#include <string>
#include <vector>

#include "core/exif.h"
#include "core/histogram.h"
#include "core/image_details.h"
#include "core/image_source.h"
#include "test_exif.h"
#include "test_jpeg.h"
#include "test_tiff.h"

using namespace iv;
using test::Rgb;
using test::TiffImage;
using test::writeTiff;

namespace {

std::filesystem::path tempFile(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("iv_info_" + name);
}

std::uint64_t total(const std::vector<std::uint64_t>& counts) {
    return std::accumulate(counts.begin(), counts.end(), std::uint64_t{0});
}

std::string valueOf(const std::vector<DetailRow>& rows, const std::string& label) {
    const auto it = std::ranges::find(rows, label, &DetailRow::label);
    return it == rows.end() ? std::string() : it->value;
}

}  // namespace

TEST(Histogram, EightBitRgbCountsEachChannelInTheRightOrder) {
    const ImageBuffer image = ImageBuffer::create({2, 1}, PixelFormat::Bgrx8);
    const std::array<std::uint8_t, 8> pixels{10, 20, 30, 255, 40, 50, 60, 255};  // BGRX
    std::memcpy(image.row(0), pixels.data(), pixels.size());
    const std::array tiles{image};
    const Histogram histogram = histogramOf(tiles);
    EXPECT_FALSE(histogram.gray);
    EXPECT_EQ(histogram.binCount(), 256);
    EXPECT_EQ(histogram.channels[0][30], 1u);  // red
    EXPECT_EQ(histogram.channels[1][20], 1u);  // green
    EXPECT_EQ(histogram.channels[2][10], 1u);  // blue
    EXPECT_EQ(histogram.channels[0][60], 1u);
    EXPECT_EQ(total(histogram.channels[0]), 2u);
}

TEST(Histogram, EqualChannelsAreGray) {
    const ImageBuffer image = ImageBuffer::create({4, 4}, PixelFormat::Bgrx8);
    std::memset(image.data(), 77, image.stride() * 4);
    const std::array tiles{image};
    const Histogram histogram = histogramOf(tiles);
    EXPECT_TRUE(histogram.gray);
    EXPECT_EQ(histogram.channels[0][77], 16u);
    EXPECT_TRUE(histogram.channels[1].empty());
}

TEST(Histogram, SixteenBitUsesAllBins) {
    const ImageBuffer gray = ImageBuffer::create({3, 1}, PixelFormat::Gray16);
    const std::array<std::uint16_t, 3> values{0, 40000, 65535};
    std::memcpy(gray.row(0), values.data(), sizeof(values));
    const std::array tiles{gray};
    const Histogram histogram = histogramOf(tiles);
    EXPECT_TRUE(histogram.gray);
    EXPECT_EQ(histogram.bitsPerBin, 16);
    EXPECT_EQ(histogram.binCount(), 65536);
    EXPECT_EQ(histogram.high, 65535);
    EXPECT_EQ(histogram.channels[0][40000], 1u);
    EXPECT_EQ(histogram.channels[0][65535], 1u);

    const ImageBuffer rgb = ImageBuffer::create({1, 1}, PixelFormat::Rgba16);
    const std::array<std::uint16_t, 4> pixel{1000, 2000, 3000, 65535};
    std::memcpy(rgb.row(0), pixel.data(), sizeof(pixel));
    const std::array rgbTiles{rgb};
    const Histogram color = histogramOf(rgbTiles);
    EXPECT_FALSE(color.gray);
    EXPECT_EQ(color.channels[2][3000], 1u);
    EXPECT_EQ(color.channels[0][65535], 0u);  // alpha is not counted
}

TEST(Histogram, FloatRangeComesFromTheDataAndSkipsNonFinite) {
    const ImageBuffer image = ImageBuffer::create({5, 1}, PixelFormat::Gray32F);
    const std::array<float, 5> values{-2.0F, 2.0F, 2.0F, std::numeric_limits<float>::quiet_NaN(),
                                      std::numeric_limits<float>::infinity()};
    std::memcpy(image.row(0), values.data(), sizeof(values));
    const std::array tiles{image};
    const Histogram histogram = histogramOf(tiles);
    EXPECT_EQ(histogram.bitsPerBin, 0);
    EXPECT_EQ(histogram.low, -2.0);
    EXPECT_EQ(histogram.high, 2.0);
    EXPECT_EQ(total(histogram.channels[0]), 3u);
    EXPECT_EQ(histogram.channels[0].front(), 1u);
    EXPECT_EQ(histogram.channels[0].back(), 2u);
}

TEST(Histogram, ChoosesAReasonableLevel) {
    const auto levels = [](std::initializer_list<int> widths) {
        std::vector<LevelInfo> out;
        for (const int width : widths) {
            out.push_back({{width, width}, {256, 256}});
        }
        return out;
    };
    // The smallest level with a megapixel, not the full 144 MP and not the tiny one.
    EXPECT_EQ(histogramLevel(levels({12000, 6000, 3000, 1500, 750, 375})), 3);
    // A small image is measured in full.
    EXPECT_EQ(histogramLevel(levels({800})), 0);
    EXPECT_EQ(histogramLevel(levels({800, 400, 200})), 0);
    // Rather a small level than reading a huge one.
    EXPECT_EQ(histogramLevel(levels({12000, 300})), 1);
    // A single huge level has no alternative.
    EXPECT_EQ(histogramLevel(levels({12000})), 0);
}

TEST(Histogram, CountsOnlyRealPixelsAcrossTilesAndUsesAReducedLevel) {
    const auto path = tempFile("hist.tif");
    TiffImage full;
    full.width = 3000;
    full.height = 3000;
    full.tileSize = 256;
    full.pixel = [](int, int) { return Rgb{200, 200, 200}; };
    TiffImage reduced = full;
    reduced.width = 1100;
    reduced.height = 1050;  // partial tiles on both edges
    reduced.reduced = true;
    reduced.pixel = [](int, int) { return Rgb{100, 100, 100}; };
    writeTiff(path, {full, reduced});

    auto source = openImageSource(path);
    ASSERT_TRUE(source.has_value()) << source.error();
    const auto histogram = computeHistogram(**source, 0, {});
    ASSERT_TRUE(histogram.has_value()) << histogram.error();
    EXPECT_EQ(histogram->measuredSize, (Size{1100, 1050}));
    EXPECT_EQ(histogram->channels[0][100], 1100u * 1050u);
    EXPECT_EQ(total(histogram->channels[0]), 1100u * 1050u);
    source->reset();  // Windows cannot delete a file that is open
    std::filesystem::remove(path);
}

TEST(Histogram, CancelsAndRejectsMissingPages) {
    const auto path = tempFile("cancel.tif");
    writeTiff(path, {TiffImage{}});
    auto source = openImageSource(path);
    ASSERT_TRUE(source.has_value());
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = computeHistogram(**source, 0, stop.get_token());
    ASSERT_FALSE(cancelled.has_value());
    EXPECT_EQ(cancelled.error(), "cancelled");
    EXPECT_FALSE(computeHistogram(**source, 7, {}).has_value());
    source->reset();
    std::filesystem::remove(path);
}

class ExifEndian : public testing::TestWithParam<bool> {};

TEST_P(ExifEndian, ReadsEveryField) {
    const ExifData exif = parseExif(test::exampleExifBlock(GetParam()));
    EXPECT_TRUE(exif.present);
    EXPECT_EQ(exif.orientation, Orientation::Rotate90CW);
    EXPECT_EQ(exif.make, "Canon");
    EXPECT_EQ(exif.model, "Canon EOS 5D");
    EXPECT_EQ(exif.lens, "EF 50mm f/1.4");
    EXPECT_EQ(exif.dateTaken, "2024-05-01 12:30:00");
    EXPECT_DOUBLE_EQ(*exif.exposureSeconds, 1.0 / 250);
    EXPECT_DOUBLE_EQ(*exif.fNumber, 2.8);
    EXPECT_DOUBLE_EQ(*exif.focalLength, 50);
    EXPECT_DOUBLE_EQ(*exif.focalLength35mm, 75);
    EXPECT_DOUBLE_EQ(*exif.exposureBias, -2.0 / 3);
    EXPECT_EQ(*exif.iso, 400);
    EXPECT_EQ(*exif.meteringMode, 5);
    EXPECT_EQ(*exif.whiteBalance, 1);
    EXPECT_NEAR(*exif.latitude, 49 + 11 / 60.0 + 30.6 / 3600, 1e-9);
    EXPECT_NEAR(*exif.longitude, 16 + 36 / 60.0 + 18 / 3600.0, 1e-9);
    EXPECT_DOUBLE_EQ(*exif.altitude, 234.5);
}

INSTANTIATE_TEST_SUITE_P(Both, ExifEndian, testing::Bool(),
                         [](const testing::TestParamInfo<bool>& info) { return info.param ? "Little" : "Big"; });

TEST(Exif, SurvivesCorruptionAtEveryByte) {
    const std::vector<std::uint8_t> block = test::exampleExifBlock(true);
    for (std::size_t at = 0; at < block.size(); ++at) {
        for (const std::uint8_t value : {std::uint8_t{0x00}, std::uint8_t{0xFF}, std::uint8_t{0x7F}}) {
            std::vector<std::uint8_t> copy = block;
            copy[at] = value;
            (void)parseExif(copy);
        }
        (void)parseExif(std::span(block).first(at));  // truncated here
    }
}

TEST(Exif, RemovesTheLocationLeavingTheRest) {
    for (const bool little : {true, false}) {
        std::vector<std::uint8_t> block = test::exampleExifBlock(little);
        const std::span<std::uint8_t> tiff = std::span(block).subspan(6);
        removeExifLocation(tiff);
        const ExifData exif = parseExif(block);
        EXPECT_FALSE(exif.latitude);
        EXPECT_FALSE(exif.altitude);
        EXPECT_EQ(exif.model, "Canon EOS 5D");
        EXPECT_EQ(*exif.iso, 400);
    }
}

TEST(Exif, RemovingTheLocationSurvivesCorruption) {
    const std::vector<std::uint8_t> block = test::exampleExifBlock(true);
    for (std::size_t at = 6; at < block.size(); ++at) {
        std::vector<std::uint8_t> copy = block;
        copy[at] = 0xFF;
        removeExifLocation(std::span(copy).subspan(6));
    }
}

TEST(Exif, CleansTextAndDates) {
    EXPECT_EQ(cleanExifText(std::string_view("  Canon \0garbage", 15)), "Canon");
    EXPECT_EQ(cleanExifText("a\x01" "b"), "ab");
    EXPECT_EQ(normalizeExifDate("2024:05:01 12:30:00"), "2024-05-01 12:30:00");
    EXPECT_EQ(normalizeExifDate("    :  :     :  :  "), "");
    EXPECT_EQ(normalizeExifDate("0000:00:00 00:00:00"), "");
    EXPECT_EQ(normalizeExifDate("yesterday"), "");
}

TEST(Details, DescribesAJpegWithSubsamplingAndCamera) {
    const auto path = tempFile("details.jpg");
    test::JpegOptions options;
    options.width = 64;
    options.height = 32;
    options.exif = test::exampleExifBlock(true);
    test::writeFile(path, test::encodeJpeg(options));

    const auto source = openImageSource(path);
    ASSERT_TRUE(source.has_value()) << source.error();
    const auto rows = describeImage((*source)->subimages()[0]);
    // Orientation 6 swaps the sides as displayed.
    EXPECT_EQ(valueOf(rows, "Dimensions"), "32 x 64");
    EXPECT_EQ(valueOf(rows, "Megapixels"), "0.00 MP");
    EXPECT_EQ(valueOf(rows, "Format"), "JPEG");
    EXPECT_EQ(valueOf(rows, "Bit depth"), "8-bit");
    EXPECT_EQ(valueOf(rows, "Compression"), "JPEG, lossy");
    EXPECT_EQ(valueOf(rows, "Chroma"), "4:2:0");
    EXPECT_EQ(valueOf(rows, "Camera"), "Canon EOS 5D");  // the make is not repeated
    EXPECT_EQ(valueOf(rows, "Lens"), "EF 50mm f/1.4");
    EXPECT_EQ(valueOf(rows, "Focal length"), "50 mm (75 mm in 35mm)");
    EXPECT_EQ(valueOf(rows, "Exposure"), "1/250 s");
    EXPECT_EQ(valueOf(rows, "Aperture"), "f/2.8");
    EXPECT_EQ(valueOf(rows, "ISO"), "400");
    EXPECT_EQ(valueOf(rows, "Metering"), "Pattern");
    EXPECT_EQ(valueOf(rows, "White balance"), "Manual");
    EXPECT_EQ(valueOf(rows, "Exposure bias"), "-0.7 EV");
    EXPECT_EQ(valueOf(rows, "Taken"), "2024-05-01 12:30:00");
    EXPECT_EQ(valueOf(rows, "Coordinates"), "49.19183\xC2\xB0 N, 16.60500\xC2\xB0 E");
    EXPECT_EQ(valueOf(rows, "Altitude"), "235 m");
    const auto coordinates = std::ranges::find(rows, std::string("Coordinates"), &DetailRow::label);
    ASSERT_NE(coordinates, rows.end());
    EXPECT_EQ(coordinates->section, "Location");
    EXPECT_EQ(coordinates->link, "https://geohack.toolforge.org/geohack.php?params=49.191833_N_16.605000_E");
    std::filesystem::remove(path);
}

TEST(Details, LinksTheLocationToAMap) {
    EXPECT_EQ(mapLink(49.1918333, 16.605), "https://geohack.toolforge.org/geohack.php?params=49.191833_N_16.605000_E");
    EXPECT_EQ(mapLink(-33.8568, -70.25), "https://geohack.toolforge.org/geohack.php?params=33.856800_S_70.250000_W");
}

TEST(Details, ShowsTheSouthAndWestAndBelowSeaLevel) {
    SubimageInfo info;
    info.exif.latitude = -33.85681;
    info.exif.longitude = -151.21514;
    info.exif.altitude = -12.4;
    const auto rows = describeImage(info);
    EXPECT_EQ(valueOf(rows, "Coordinates"), "33.85681\xC2\xB0 S, 151.21514\xC2\xB0 W");
    EXPECT_EQ(valueOf(rows, "Altitude"), "-12 m");
    info.exif.longitude.reset();
    EXPECT_EQ(valueOf(describeImage(info), "Coordinates"), "");
}

TEST(Details, NamesChromaSubsampling) {
    EXPECT_EQ(chromaSubsamplingName(1, 1, 1, 1), "4:4:4");
    EXPECT_EQ(chromaSubsamplingName(2, 1, 1, 1), "4:2:2");
    EXPECT_EQ(chromaSubsamplingName(2, 2, 1, 1), "4:2:0");
    EXPECT_EQ(chromaSubsamplingName(1, 2, 1, 1), "4:4:0");
    EXPECT_EQ(chromaSubsamplingName(4, 1, 1, 1), "4:1:1");
    EXPECT_EQ(chromaSubsamplingName(2, 2, 0, 0), "");
}

TEST(Details, DescribesTiffPagesSeparately) {
    if (!TIFFIsCODECConfigured(COMPRESSION_ZSTD)) {
        GTEST_SKIP() << "this libtiff is built without Zstandard";
    }
    const auto path = tempFile("pages.tif");
    TiffImage first;
    first.width = 2000;
    first.height = 1500;
    first.bits = 16;
    first.compression = COMPRESSION_LZW;
    first.tileSize = 256;
    TiffImage reduced = first;
    reduced.width = 1000;
    reduced.height = 750;
    reduced.reduced = true;
    TiffImage second;
    second.samples = 1;
    second.bits = 32;
    second.sampleFormat = SAMPLEFORMAT_IEEEFP;
    second.compression = COMPRESSION_ZSTD;
    writeTiff(path, {first, reduced, second});

    auto source = openImageSource(path);
    ASSERT_TRUE(source.has_value()) << source.error();
    const auto page1 =describeImage((*source)->subimages()[0]);
    EXPECT_EQ(valueOf(page1, "Dimensions"), "2000 x 1500");
    EXPECT_EQ(valueOf(page1, "Megapixels"), "3.0 MP");
    EXPECT_EQ(valueOf(page1, "Format"), "TIFF");
    EXPECT_EQ(valueOf(page1, "Bit depth"), "16-bit");
    EXPECT_EQ(valueOf(page1, "Color"), "RGB");
    EXPECT_EQ(valueOf(page1, "Compression"), "LZW, lossless");
    EXPECT_EQ(valueOf(page1, "Pyramid"), "2 levels");

    const auto page2 = describeImage((*source)->subimages()[1]);
    EXPECT_EQ(valueOf(page2, "Bit depth"), "32-bit float");
    EXPECT_EQ(valueOf(page2, "Color"), "Grayscale");
    EXPECT_EQ(valueOf(page2, "Compression"), "Zstandard, lossless");
    EXPECT_EQ(valueOf(page2, "Pyramid"), "");
    source->reset();
    std::filesystem::remove(path);
}

TEST(Details, LeavesOutMeteringAndWhiteBalanceItCannotName) {
    SubimageInfo info;
    info.exif.meteringMode = 0;  // "unknown"
    info.exif.whiteBalance = 7;
    const auto rows = describeImage(info);
    EXPECT_EQ(valueOf(rows, "Metering"), "");
    EXPECT_EQ(valueOf(rows, "White balance"), "");
}

TEST(Summary, DescribesAnImageInALine) {
    SubimageInfo info;
    info.levels.push_back({Size{400, 300}, Size{400, 300}, false});
    info.stored.container = "PNG";
    info.stored.colorModel = "RGB";
    EXPECT_EQ(imageSummary(info), "400x300 0.12MP 24bit PNG");

    info.levels.front().size = {6000, 4000};
    info.orientation = Orientation::Rotate90CW;
    info.stored.colorModel = "Grayscale";
    info.stored.bitsPerSample = 16;
    EXPECT_EQ(imageSummary(info), "4000x6000 24.0MP 16bit PNG");

    info.stored.colorModel = "RGB + alpha";
    info.stored.bitsPerSample = 8;
    EXPECT_EQ(imageSummary(info), "4000x6000 24.0MP 32bit PNG");
}

TEST(Summary, DescribesTheExposureInALine) {
    ExifData exif;
    EXPECT_EQ(cameraSummary(exif), "");
    exif.exposureSeconds = 1.0 / 60;
    exif.fNumber = 4;
    exif.iso = 500;
    exif.exposureBias = -1.0 / 3;
    EXPECT_EQ(cameraSummary(exif), "1/60s f/4 ISO-500 EV -0.33");
    exif.exposureBias = 0;
    exif.fNumber.reset();
    EXPECT_EQ(cameraSummary(exif), "1/60s ISO-500");
}

TEST(Summary, GivesSizesInDecimalUnits) {
    EXPECT_EQ(shortDataSize(0), "0 B");
    EXPECT_EQ(shortDataSize(999), "999 B");
    EXPECT_EQ(shortDataSize(1000), "1 kB");
    EXPECT_EQ(shortDataSize(780'000), "780 kB");
    EXPECT_EQ(shortDataSize(999'499), "999 kB");
    EXPECT_EQ(shortDataSize(999'500), "1.0 MB");
    EXPECT_EQ(shortDataSize(500'000'000), "500.0 MB");
    EXPECT_EQ(shortDataSize(2'500'000'000), "2.5 GB");
}

TEST(Details, ReadsCameraTagsAndExifDirectoryFromTiff) {
    const auto path = tempFile("exif.tif");
    {
        TIFF* tiff = TIFFOpen(path.string().c_str(), "w");
        ASSERT_NE(tiff, nullptr);
        ASSERT_EQ(TIFFCreateEXIFDirectory(tiff), 0);
        TIFFSetField(tiff, EXIFTAG_EXPOSURETIME, 0.004);
        TIFFSetField(tiff, EXIFTAG_FNUMBER, 2.8);
        TIFFSetField(tiff, EXIFTAG_FOCALLENGTH, 50.0);
        const std::uint16_t iso = 400;
        TIFFSetField(tiff, EXIFTAG_ISOSPEEDRATINGS, std::uint16_t{1}, &iso);
        TIFFSetField(tiff, EXIFTAG_LENSMODEL, "EF 50mm");
        TIFFSetField(tiff, EXIFTAG_DATETIMEORIGINAL, "2024:05:01 12:30:00");
        std::uint64_t exifOffset = 0;
        ASSERT_TRUE(TIFFWriteCustomDirectory(tiff, &exifOffset));
        TIFFFreeDirectory(tiff);
        TIFFCreateDirectory(tiff);

        TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, 8u);
        TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, 8u);
        TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, std::uint16_t{8});
        TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, std::uint16_t{1});
        TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, std::uint16_t{PHOTOMETRIC_MINISBLACK});
        TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 8u);
        TIFFSetField(tiff, TIFFTAG_MAKE, "Nikon Corporation");
        TIFFSetField(tiff, TIFFTAG_MODEL, "D850");
        TIFFSetField(tiff, TIFFTAG_EXIFIFD, exifOffset);
        std::array<std::uint8_t, 64> pixels{};
        pixels.fill(90);
        TIFFWriteEncodedStrip(tiff, 0, pixels.data(), 64);
        ASSERT_TRUE(TIFFWriteDirectory(tiff));
        TIFFClose(tiff);
    }

    auto source = openImageSource(path);
    ASSERT_TRUE(source.has_value()) << source.error();
    const auto rows = describeImage((*source)->subimages()[0]);
    EXPECT_EQ(valueOf(rows, "Camera"), "Nikon Corporation D850");
    EXPECT_EQ(valueOf(rows, "Lens"), "EF 50mm");
    EXPECT_EQ(valueOf(rows, "Exposure"), "1/250 s");
    EXPECT_EQ(valueOf(rows, "Aperture"), "f/2.8");
    EXPECT_EQ(valueOf(rows, "Focal length"), "50 mm");
    EXPECT_EQ(valueOf(rows, "ISO"), "400");
    EXPECT_EQ(valueOf(rows, "Taken"), "2024-05-01 12:30:00");
    // Reading the EXIF directory must not break reading the pixels afterwards.
    const auto tile = (*source)->readTile({0, 0, 0, 0}, {});
    ASSERT_TRUE(tile.has_value()) << tile.error();
    EXPECT_EQ(*reinterpret_cast<const std::uint8_t*>(tile->row(0)), 90);
    source->reset();
    std::filesystem::remove(path);
}
