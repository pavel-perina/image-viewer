#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>

#include <gtest/gtest.h>
#include <lcms2.h>

#include "core/image_source.h"
#include "core/exif.h"
#include "core/image_writer.h"
#include "test_exif.h"

namespace iv {
namespace {

namespace fs = std::filesystem;

ImageBuffer makeImage(Size size, PixelFormat format, const std::function<double(int x, int y, int c)>& value) {
    ImageBuffer image = ImageBuffer::create(size, format);
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            for (int c = 0; c < 4; ++c) {
                const double v = c == 3 ? 1.0 : value(x, y, c);  // c: 0 red, 1 green, 2 blue
                switch (format) {
                case PixelFormat::Bgrx8:
                    reinterpret_cast<std::uint8_t*>(image.row(y))[x * 4 + (c == 3 ? 3 : 2 - c)] =
                        static_cast<std::uint8_t>(std::lround(v * 255));
                    break;
                case PixelFormat::Rgba16:
                    reinterpret_cast<std::uint16_t*>(image.row(y))[x * 4 + c] = static_cast<std::uint16_t>(std::lround(v * 65535));
                    break;
                case PixelFormat::Rgba32F:
                    reinterpret_cast<float*>(image.row(y))[x * 4 + c] = static_cast<float>(v);
                    break;
                case PixelFormat::Gray16:
                    if (c == 0) {
                        reinterpret_cast<std::uint16_t*>(image.row(y))[x] = static_cast<std::uint16_t>(std::lround(v * 65535));
                    }
                    break;
                case PixelFormat::Gray32F:
                    if (c == 0) {
                        reinterpret_cast<float*>(image.row(y))[x] = static_cast<float>(v);
                    }
                    break;
                }
            }
        }
    }
    return image;
}

// Smooth, so that lossy formats stay close, and different along each axis and channel.
double smooth(int x, int y, int c) {
    switch (c) {
    case 0:  return x / 63.0;
    case 1:  return y / 47.0;
    default: return 0.25 + 0.5 * (x + y) / 109.0;
    }
}

// Differences in 8-bit steps, whatever the formats.
double sampleAt(const ImageBuffer& image, int x, int y, int c) {
    switch (image.format()) {
    case PixelFormat::Bgrx8:   return reinterpret_cast<const std::uint8_t*>(image.row(y))[x * 4 + 2 - c];
    case PixelFormat::Rgba16:  return reinterpret_cast<const std::uint16_t*>(image.row(y))[x * 4 + c] / 257.0;
    case PixelFormat::Rgba32F: return reinterpret_cast<const float*>(image.row(y))[x * 4 + c] * 255.0;
    case PixelFormat::Gray16:  return reinterpret_cast<const std::uint16_t*>(image.row(y))[x] / 257.0;
    case PixelFormat::Gray32F: return reinterpret_cast<const float*>(image.row(y))[x] * 255.0;
    }
    return 0;
}

struct Difference {
    double largest = 0;
    double mean = 0;
};

Difference difference(const ImageBuffer& a, const ImageBuffer& b, int channels = 3) {
    Difference d;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            for (int c = 0; c < channels; ++c) {
                const double delta = std::abs(sampleAt(a, x, y, c) - sampleAt(b, x, y, c));
                d.largest = std::max(d.largest, delta);
                d.mean += delta;
            }
        }
    }
    d.mean /= static_cast<double>(a.width()) * a.height() * channels;
    return d;
}

std::vector<std::uint8_t> rgbProfile() {
    // Adobe RGB primaries, so that it differs from what a reader assumes without one.
    const cmsCIExyY white{0.3127, 0.3290, 1.0};
    const cmsCIExyYTRIPLE primaries{{0.64, 0.33, 1.0}, {0.21, 0.71, 1.0}, {0.15, 0.06, 1.0}};
    cmsToneCurve* curve = cmsBuildGamma(nullptr, 2.2);
    cmsToneCurve* curves[3] = {curve, curve, curve};
    cmsHPROFILE profile = cmsCreateRGBProfile(&white, &primaries, curves);
    cmsFreeToneCurve(curve);
    cmsUInt32Number size = 0;
    cmsSaveProfileToMem(profile, nullptr, &size);
    std::vector<std::uint8_t> bytes(size);
    cmsSaveProfileToMem(profile, bytes.data(), &size);
    cmsCloseProfile(profile);
    return bytes;
}

// Whether two RGB profiles take colours to the same place, within an 8-bit step.
bool convertTheSame(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    cmsHPROFILE first = cmsOpenProfileFromMem(a.data(), static_cast<cmsUInt32Number>(a.size()));
    cmsHPROFILE second = cmsOpenProfileFromMem(b.data(), static_cast<cmsUInt32Number>(b.size()));
    cmsHPROFILE srgb = cmsCreate_sRGBProfile();
    bool same = first && second;
    if (same) {
        cmsHTRANSFORM one = cmsCreateTransform(first, TYPE_RGB_8, srgb, TYPE_RGB_8, INTENT_RELATIVE_COLORIMETRIC, 0);
        cmsHTRANSFORM two = cmsCreateTransform(second, TYPE_RGB_8, srgb, TYPE_RGB_8, INTENT_RELATIVE_COLORIMETRIC, 0);
        for (int v = 0; v < 256 && same; v += 15) {
            const std::uint8_t in[] = {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(255 - v), 128};
            std::uint8_t x[3];
            std::uint8_t y[3];
            cmsDoTransform(one, in, x, 1);
            cmsDoTransform(two, in, y, 1);
            for (int c = 0; c < 3; ++c) {
                same = same && std::abs(x[c] - y[c]) <= 1;
            }
        }
        cmsDeleteTransform(one);
        cmsDeleteTransform(two);
    }
    for (cmsHPROFILE profile : {first, second, srgb}) {
        if (profile) {
            cmsCloseProfile(profile);
        }
    }
    return same;
}

class Writer : public ::testing::Test {
protected:
    void SetUp() override {
        m_folder = fs::temp_directory_path() / ("iv_writer_" + std::to_string(::getpid()) + "_" +
                                                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(m_folder);
        fs::create_directories(m_folder);
    }
    void TearDown() override { fs::remove_all(m_folder); }

    struct ReadBack {
        SubimageInfo info;
        ImageBuffer pixels;
        std::uintmax_t fileSize = 0;
    };

    // Writes, opens with the viewer's own readers and decodes the whole image.
    ReadBack roundTrip(const ImageBuffer& image, const WriteOptions& options, const WriteMetadata& metadata = {}) {
        const fs::path path = m_folder / ("image." + std::string(extensionFor(options)));
        ReadBack out;
        const auto written = writeImage(path, image, metadata, options, true);
        EXPECT_TRUE(written) << written.error();
        if (!written) {
            return out;
        }
        out.fileSize = fs::file_size(path);
        auto source = openImageSource(path);
        EXPECT_TRUE(source) << source.error();
        if (!source) {
            return out;
        }
        out.info = (*source)->subimages().front();
        auto tile = (*source)->readTile({0, 0, 0, 0}, {});
        EXPECT_TRUE(tile) << tile.error();
        if (tile) {
            out.pixels = *tile;
        }
        return out;
    }

    fs::path m_folder;
};

struct Case {
    const char* name;
    WriteOptions options;
    double largest;  // in 8-bit steps; 0 for lossless
    double mean;
};

TEST_F(Writer, EveryFormatGivesBackTheImage) {
    const ImageBuffer image = makeImage({64, 48}, PixelFormat::Bgrx8, smooth);
    const Case cases[] = {
        {"JPEG", JpegWriteOptions{.quality = 95, .chroma = ChromaSubsampling::Full}, 12, 1.5},
        {"PNG", PngWriteOptions{}, 0, 0},
        {"TIFF none", TiffWriteOptions{.compression = TiffCompression::None}, 0, 0},
        {"TIFF LZW", TiffWriteOptions{.compression = TiffCompression::Lzw}, 0, 0},
        {"TIFF Deflate", TiffWriteOptions{.compression = TiffCompression::Deflate}, 0, 0},
        {"WebP", WebpWriteOptions{.quality = 95}, 24, 3},
        {"WebP lossless", WebpWriteOptions{.lossless = true}, 0, 0},
        {"AVIF", AvifWriteOptions{.quality = 95, .chroma = ChromaSubsampling::Full}, 12, 1.5},
        // Its colour space loses a little at the most saturated edge, here red 0 and green 255.
        {"JPEG XL", JxlWriteOptions{.distance = 0.5F}, 24, 1.5},
        {"JPEG XL lossless", JxlWriteOptions{.lossless = true}, 0, 0},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        if (!canWrite(c.options)) {
            continue;
        }
        const ReadBack back = roundTrip(image, c.options);
        ASSERT_FALSE(back.pixels.isNull());
        ASSERT_EQ(back.pixels.size(), image.size());
        const Difference d = difference(image, back.pixels);
        EXPECT_LE(d.largest, c.largest);
        EXPECT_LE(d.mean, c.mean);
    }
}

TEST_F(Writer, KeepsTheColourProfileAndTheExif) {
    const ImageBuffer image = makeImage({64, 48}, PixelFormat::Bgrx8, smooth);
    WriteMetadata metadata;
    metadata.iccProfile = rgbProfile();
    metadata.exif = test::exampleExifBlock(true);
    const WriteOptions all[] = {JpegWriteOptions{}, PngWriteOptions{}, TiffWriteOptions{}, WebpWriteOptions{},
                                AvifWriteOptions{}, JxlWriteOptions{}, JxlWriteOptions{.lossless = true}};
    for (const WriteOptions& options : all) {
        SCOPED_TRACE(std::string(extensionFor(options)));
        if (!canWrite(options)) {
            continue;
        }
        const ReadBack back = roundTrip(image, options, metadata);
        if (const auto* jxl = std::get_if<JxlWriteOptions>(&options); jxl && !jxl->lossless) {
            // Lossy JPEG XL stores a profile it recognises as a description, and gives back
            // one made from that, which converts the same.
            EXPECT_TRUE(convertTheSame(back.info.iccProfile, metadata.iccProfile));
        } else {
            EXPECT_EQ(back.info.iccProfile, metadata.iccProfile);
        }
        if (std::holds_alternative<TiffWriteOptions>(options)) {
            continue;  // EXIF in TIFF is not written yet
        }
        EXPECT_TRUE(back.info.exif.present);
        EXPECT_EQ(back.info.exif.make, "Canon");
        EXPECT_EQ(back.info.exif.iso, 400);
        // The pixels are written upright, so the orientation they had does not apply.
        EXPECT_EQ(back.info.exif.orientation, Orientation::Normal);
        // And the block itself comes back, for the next file to be written from it.
        EXPECT_EQ(parseExif(back.info.rawExif).model, "Canon EOS 5D");
    }
}

TEST_F(Writer, KeepsTheXmpWithItsOrientationReset) {
    const ImageBuffer image = makeImage({64, 48}, PixelFormat::Bgrx8, smooth);
    const std::string packet = R"(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">)"
                               R"(<rdf:Description xmlns:tiff="http://ns.adobe.com/tiff/1.0/" xmlns:xmp="http://ns.adobe.com/xap/1.0/")"
                               R"( tiff:Orientation="6" xmp:Rating="4"/></rdf:RDF></x:xmpmeta>)";
    WriteMetadata metadata;
    metadata.xmp.assign(packet.begin(), packet.end());
    std::string expected = packet;
    expected.replace(expected.find("\"6\""), 3, "\"1\"");

    const WriteOptions all[] = {JpegWriteOptions{}, PngWriteOptions{}, TiffWriteOptions{}, WebpWriteOptions{},
                                AvifWriteOptions{}, JxlWriteOptions{}};
    for (const WriteOptions& options : all) {
        SCOPED_TRACE(std::string(extensionFor(options)));
        if (!canWrite(options)) {
            continue;
        }
        const auto bytes = encodeImage(image, metadata, options);
        ASSERT_TRUE(bytes) << bytes.error();
        // Every format stores the packet uncompressed, so it shows in the file as it is.
        const std::string_view file(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        EXPECT_NE(file.find(expected), std::string_view::npos);
        // And the readers find it.
        const ReadBack back = roundTrip(image, options, metadata);
        EXPECT_EQ(std::string(back.info.xmp.begin(), back.info.xmp.end()), expected);
    }
}

TEST_F(Writer, GrayPixelsAreWrittenAsGray) {
    const ImageBuffer gray = makeImage({64, 48}, PixelFormat::Bgrx8, [](int x, int, int) { return x / 63.0; });
    const WriteOptions grayable[] = {JpegWriteOptions{}, PngWriteOptions{}, JxlWriteOptions{.lossless = true}};
    WriteMetadata withRgbProfile;
    withRgbProfile.iccProfile = rgbProfile();
    for (const WriteOptions& options : grayable) {
        SCOPED_TRACE(std::string(extensionFor(options)));
        if (!canWrite(options)) {
            continue;
        }
        EXPECT_EQ(roundTrip(gray, options).info.stored.colorModel, "Grayscale");
        // An RGB profile has to go with RGB pixels.
        EXPECT_NE(roundTrip(gray, options, withRgbProfile).info.stored.colorModel, "Grayscale");
    }
    EXPECT_EQ(roundTrip(gray, TiffWriteOptions{}).info.stored.colorModel, "Grayscale");
}

TEST_F(Writer, DeepImagesKeepTheirDepthWhereTheFormatCan) {
    // Steps finer than 8 bits can hold, which a reduction would lose.
    const auto fine = [](int x, int y, int c) { return (x * 48 + y + c * 7) / 4000.0; };
    const ImageBuffer rgb16 = makeImage({64, 48}, PixelFormat::Rgba16, fine);
    const ImageBuffer gray16 = makeImage({64, 48}, PixelFormat::Gray16, fine);
    const ImageBuffer grayFloat = makeImage({64, 48}, PixelFormat::Gray32F, [](int x, int y, int) { return x * 0.01 - y * 0.5; });

    for (const WriteOptions& options : {WriteOptions{PngWriteOptions{}}, WriteOptions{TiffWriteOptions{}},
                                        WriteOptions{JxlWriteOptions{.lossless = true}}}) {
        SCOPED_TRACE(std::string(extensionFor(options)));
        if (!canWrite(options)) {
            continue;
        }
        const ReadBack colour = roundTrip(rgb16, options);
        EXPECT_EQ(colour.pixels.format(), PixelFormat::Rgba16);
        EXPECT_EQ(difference(rgb16, colour.pixels).largest, 0);
        const ReadBack gray = roundTrip(gray16, options);
        EXPECT_EQ(gray.pixels.format(), PixelFormat::Gray16);
        EXPECT_EQ(difference(gray16, gray.pixels, 1).largest, 0);
    }

    // Floats, even outside 0 to 1, stay exactly as they were in TIFF.
    const ReadBack floats = roundTrip(grayFloat, TiffWriteOptions{});
    ASSERT_EQ(floats.pixels.format(), PixelFormat::Gray32F);
    EXPECT_EQ(difference(grayFloat, floats.pixels, 1).largest, 0);

    // AVIF keeps 10 bits of them.
    if (canWrite(AvifWriteOptions{})) {
        const ReadBack avif = roundTrip(rgb16, AvifWriteOptions{.quality = 100, .chroma = ChromaSubsampling::Full});
        EXPECT_EQ(avif.pixels.format(), PixelFormat::Rgba16);
        EXPECT_LE(difference(rgb16, avif.pixels).mean, 1.0);
    }

    // JPEG holds 8 bits, so it gets the nearest 8-bit values.
    const ReadBack reduced = roundTrip(rgb16, JpegWriteOptions{.quality = 100, .chroma = ChromaSubsampling::Full});
    EXPECT_EQ(reduced.pixels.format(), PixelFormat::Bgrx8);
    EXPECT_LE(difference(rgb16, reduced.pixels).largest, 3);
}

TEST_F(Writer, JpegFollowsItsOptions) {
    const ImageBuffer image = makeImage({64, 48}, PixelFormat::Bgrx8, [](int x, int y, int c) {
        return ((x * 7 + y * 13 + c * 5) % 17) / 16.0;  // busy, so that quality shows in the size
    });
    const ReadBack low = roundTrip(image, JpegWriteOptions{.quality = 30});
    const ReadBack high = roundTrip(image, JpegWriteOptions{.quality = 95});
    EXPECT_LT(low.fileSize, high.fileSize);
    EXPECT_EQ(high.info.stored.chromaSubsampling, "4:2:0");
    EXPECT_EQ(roundTrip(image, JpegWriteOptions{.chroma = ChromaSubsampling::Full}).info.stored.chromaSubsampling, "4:4:4");
    EXPECT_EQ(roundTrip(image, JpegWriteOptions{.chroma = ChromaSubsampling::Horizontal}).info.stored.chromaSubsampling, "4:2:2");
}

TEST_F(Writer, NeverReplacesAFileUnlessTold) {
    const ImageBuffer image = makeImage({8, 8}, PixelFormat::Bgrx8, smooth);
    const fs::path path = m_folder / "a.png";
    ASSERT_TRUE(writeImage(path, image, {}, JpegWriteOptions{}, false));
    const auto again = writeImage(path, image, {}, JpegWriteOptions{}, false);
    ASSERT_FALSE(again);
    EXPECT_NE(again.error().find("already exists"), std::string::npos);
    EXPECT_TRUE(writeImage(path, image, {}, JpegWriteOptions{}, true));
    EXPECT_FALSE(writeImage(m_folder / "b.png", ImageBuffer{}, {}, JpegWriteOptions{}, false));
}

TEST(WriterFormats, FollowTheExtension) {
    EXPECT_TRUE(std::holds_alternative<JpegWriteOptions>(*optionsForExtension("a/b.JPG")));
    EXPECT_TRUE(std::holds_alternative<TiffWriteOptions>(*optionsForExtension("b.tiff")));
    EXPECT_FALSE(optionsForExtension("b.gif"));
    EXPECT_EQ(extensionFor(PngWriteOptions{}), "png");
}

}  // namespace
}  // namespace iv
