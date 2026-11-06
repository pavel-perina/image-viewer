#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <random>
#include <stop_token>
#include <string>
#include <vector>

#ifdef IV_HAVE_PNG
#include <png.h>
#endif
#ifdef IV_HAVE_WEBP
#include <webp/encode.h>
#include <webp/mux.h>
#endif
#ifdef IV_HAVE_AVIF
#include <avif/avif.h>
#endif
#ifdef IV_HAVE_JXL
#include <jxl/encode.h>
#include <jxl/encode_cxx.h>
#endif

#include "core/histogram.h"
#include "core/image_details.h"
#include "core/image_ops.h"
#include "core/image_source.h"
#include "core/thumbnail.h"
#include "test_exif.h"
#include "test_jpeg.h"

using namespace iv;
using Bytes = std::vector<std::uint8_t>;

namespace {

constexpr int kWidth = 64;
constexpr int kHeight = 48;

// Left half red-ish, right half blue-ish, with partial transparency on the bottom right.
void pixelAt(int x, int y, std::array<std::uint8_t, 4>& rgba) {
    rgba = x < kWidth / 2 ? std::array<std::uint8_t, 4>{200, 40, 20, 255} : std::array<std::uint8_t, 4>{20, 40, 200, 255};
    if (x >= kWidth / 2 && y >= kHeight / 2) {
        rgba[3] = 0;
    }
}

// A folder for this process, since ctest runs tests in parallel, removed when it ends.
const std::filesystem::path& testFolder() {
    struct Folder {
        std::filesystem::path path = std::filesystem::temp_directory_path() / ("iv_format_" + std::to_string(std::random_device{}()));
        Folder() { std::filesystem::create_directories(path); }
        ~Folder() { std::filesystem::remove_all(path); }
    };
    static const Folder folder;
    return folder.path;
}

std::filesystem::path write(const std::string& name, const Bytes& bytes) {
    const auto path = testFolder() / name;
    test::writeFile(path, bytes);
    return path;
}

std::shared_ptr<ImageSource> open(const std::filesystem::path& path) {
    auto source = openImageSource(path);
    EXPECT_TRUE(source.has_value()) << path << ": " << (source ? "" : source.error());
    return source ? *source : nullptr;
}

std::array<int, 3> shown(ImageSource& source, int x, int y) {
    const SubimageInfo& info = source.subimages()[0];
    const auto tile = source.readTile({0, 0, 0, 0}, {});
    if (!tile) {
        ADD_FAILURE() << tile.error();
        return {};
    }
    const ImageBuffer display = toDisplay(*tile, fullRange(info.format));
    const auto* p = reinterpret_cast<const std::uint8_t*>(display.row(y)) + static_cast<std::size_t>(x) * 4;
    return {p[2], p[1], p[0]};
}

void expectNear(const std::array<int, 3>& actual, const std::array<int, 3>& expected, int tolerance) {
    EXPECT_NEAR(actual[0], expected[0], tolerance);
    EXPECT_NEAR(actual[1], expected[1], tolerance);
    EXPECT_NEAR(actual[2], expected[2], tolerance);
}

// Checks the picture: not swapped, not flipped, not shifted. Allows for lossy compression.
void expectPicture(ImageSource& source, int tolerance) {
    expectNear(shown(source, 8, 8), {200, 40, 20}, tolerance);
    expectNear(shown(source, kWidth - 8, 8), {20, 40, 200}, tolerance);
    expectNear(shown(source, 8, kHeight - 8), {200, 40, 20}, tolerance);
}

std::string valueOf(const std::vector<DetailRow>& rows, const std::string& label) {
    const auto it = std::ranges::find(rows, label, &DetailRow::label);
    return it == rows.end() ? std::string() : it->value;
}

// Random damage and truncation: any outcome is fine except a crash or a hang.
void survivesDamage(const std::string& name, const Bytes& bytes) {
    const auto path = testFolder() / ("damaged_" + name);
    const auto attempt = [&](const Bytes& damaged) {
        test::writeFile(path, damaged);
        auto source = openImageSource(path);
        if (source) {
            (void)(*source)->readTile({0, 0, 0, 0}, {});
        }
    };
    for (std::size_t at = 0; at < bytes.size(); at += std::max<std::size_t>(1, bytes.size() / 60)) {
        Bytes copy = bytes;
        for (std::size_t i = 0; i < 4 && at + i < copy.size(); ++i) {
            copy[at + i] = static_cast<std::uint8_t>(0xA5 ^ i);
        }
        attempt(copy);
        attempt(Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(at)));
    }
}

}  // namespace

TEST(Formats, ListsTheExtensionsItWasBuiltWith) {
    const auto extensions = supportedExtensions();
    const auto has = [&](std::string_view e) { return std::ranges::find(extensions, e) != extensions.end(); };
    EXPECT_TRUE(has("jpg"));
    EXPECT_TRUE(has("tif"));
#ifdef IV_HAVE_PNG
    EXPECT_TRUE(has("png"));
#endif
#ifdef IV_HAVE_WEBP
    EXPECT_TRUE(has("webp"));
#endif
#ifdef IV_HAVE_AVIF
    EXPECT_TRUE(has("avif"));
#endif
#ifdef IV_HAVE_JXL
    EXPECT_TRUE(has("jxl"));
#endif
}

TEST(Alpha, FlattensOntoMidGray) {
    const ImageBuffer image = ImageBuffer::create({3, 1}, PixelFormat::Bgrx8);
    const std::array<std::uint8_t, 12> pixels{10, 20, 30, 255, 10, 20, 30, 0, 255, 255, 255, 128};
    std::memcpy(image.row(0), pixels.data(), pixels.size());
    flattenAlpha(image);
    const auto* out = reinterpret_cast<const std::uint8_t*>(image.row(0));
    EXPECT_EQ(out[0], 10);  // opaque stays
    EXPECT_EQ(out[4], 128);  // transparent shows the background
    EXPECT_EQ(out[7], 255);  // and becomes opaque
    EXPECT_NEAR(out[8], 192, 1);  // half of white and gray

    const ImageBuffer deep = ImageBuffer::create({1, 1}, PixelFormat::Rgba16);
    const std::array<std::uint16_t, 4> pixel16{65535, 0, 0, 0};
    std::memcpy(deep.row(0), pixel16.data(), sizeof(pixel16));
    flattenAlpha(deep);
    EXPECT_NEAR(reinterpret_cast<const std::uint16_t*>(deep.row(0))[0], 32768, 1);
}

#ifdef IV_HAVE_PNG

namespace {

struct PngOptions {
    int colorType = PNG_COLOR_TYPE_RGB;
    int depth = 8;
    bool interlaced = false;
    bool transparentColor = false;  // a tRNS chunk
    Bytes icc;
};

void appendToBytes(png_structp png, png_bytep data, png_size_t length) {
    auto* out = static_cast<Bytes*>(png_get_io_ptr(png));
    out->insert(out->end(), data, data + length);
}

// Puts a chunk just after the header or just before the end, without a valid checksum,
// which only matters to libpng for chunks it reads.
void insertChunk(Bytes& png, const char* type, const Bytes& payload, bool atEnd) {
    Bytes chunk{std::uint8_t(payload.size() >> 24), std::uint8_t(payload.size() >> 16), std::uint8_t(payload.size() >> 8), std::uint8_t(payload.size())};
    chunk.insert(chunk.end(), type, type + 4);
    chunk.insert(chunk.end(), payload.begin(), payload.end());
    chunk.resize(chunk.size() + 4, 0);
    const std::size_t at = atEnd ? png.size() - 12 : 8 + 25;  // IEND, or after signature and IHDR
    png.insert(png.begin() + static_cast<std::ptrdiff_t>(at), chunk.begin(), chunk.end());
}

Bytes encodePng(const PngOptions& options) {
    Bytes out;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    png_set_write_fn(png, &out, appendToBytes, nullptr);
    png_set_IHDR(png, info, kWidth, kHeight, options.depth, options.colorType,
                 options.interlaced ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    const std::array<png_color, 2> palette{png_color{200, 40, 20}, png_color{20, 40, 200}};
    const bool isPalette = options.colorType == PNG_COLOR_TYPE_PALETTE;
    if (isPalette) {
        png_set_PLTE(png, info, palette.data(), 2);
    }
    if (options.transparentColor) {
        if (isPalette) {
            const std::array<png_byte, 2> alpha{255, 0};  // the second entry is transparent
            png_set_tRNS(png, info, alpha.data(), 2, nullptr);
        } else {
            png_color_16 color{};
            color.gray = options.depth == 16 ? 40 * 257 : 40;
            png_set_tRNS(png, info, nullptr, 0, &color);
        }
    }
    if (!options.icc.empty()) {
        png_set_iCCP(png, info, "test", PNG_COMPRESSION_TYPE_BASE, options.icc.data(), static_cast<png_uint_32>(options.icc.size()));
    }
    png_write_info(png, info);
    if (options.depth == 16) {
        png_set_swap(png);
    }
    if (options.depth < 8) {
        png_set_packing(png);
    }
    const int passes = png_set_interlace_handling(png);

    const bool gray = (options.colorType & PNG_COLOR_MASK_COLOR) == 0;
    const bool alpha = (options.colorType & PNG_COLOR_MASK_ALPHA) != 0;
    Bytes row;
    for (int pass = 0; pass < passes; ++pass) {
        for (int y = 0; y < kHeight; ++y) {
            row.clear();
            for (int x = 0; x < kWidth; ++x) {
                std::array<std::uint8_t, 4> rgba{};
                pixelAt(x, y, rgba);
                const auto put = [&](std::uint8_t value) {
                    row.push_back(value);
                    if (options.depth == 16) {
                        row.push_back(value);  // value * 257
                    }
                };
                if (isPalette) {
                    row.push_back(x < kWidth / 2 ? 0 : 1);
                } else if (gray) {
                    const std::uint8_t value = x < kWidth / 2 ? 200 : 40;
                    put(options.depth == 1 ? (x < kWidth / 2 ? 1 : 0) : value);
                } else {
                    put(rgba[0]);
                    put(rgba[1]);
                    put(rgba[2]);
                }
                if (alpha) {
                    put(rgba[3]);
                }
            }
            png_write_row(png, row.data());
        }
    }
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    return out;
}

}  // namespace

TEST(Png, EightBitColour) {
    const auto source = open(write("rgb.png", encodePng({})));
    ASSERT_TRUE(source);
    EXPECT_EQ(source->subimages()[0].format, PixelFormat::Bgrx8);
    expectPicture(*source, 0);
    const auto rows = describeImage(source->subimages()[0]);
    EXPECT_EQ(valueOf(rows, "Format"), "PNG");
    EXPECT_EQ(valueOf(rows, "Compression"), "Deflate, lossless");
    EXPECT_EQ(valueOf(rows, "Bit depth"), "8-bit");
    EXPECT_EQ(valueOf(rows, "Color"), "RGB");
}

TEST(Png, SixteenBitKeepsItsPrecisionInTheRightByteOrder) {
    PngOptions options;
    options.depth = 16;
    const auto source = open(write("rgb16.png", encodePng(options)));
    ASSERT_TRUE(source);
    EXPECT_EQ(source->subimages()[0].format, PixelFormat::Rgba16);
    expectPicture(*source, 0);
    const auto tile = source->readTile({0, 0, 0, 0}, {});
    ASSERT_TRUE(tile.has_value());
    const auto* first = reinterpret_cast<const std::uint16_t*>(tile->row(0));
    EXPECT_EQ(first[0], 200 * 257);
    EXPECT_EQ(first[3], 65535);  // made opaque
    EXPECT_EQ(valueOf(describeImage(source->subimages()[0]), "Bit depth"), "16-bit");
}

TEST(Png, GrayAtAllDepths) {
    PngOptions eight;
    eight.colorType = PNG_COLOR_TYPE_GRAY;
    const auto source8 = open(write("gray8.png", encodePng(eight)));
    ASSERT_TRUE(source8);
    expectNear(shown(*source8, 8, 8), {200, 200, 200}, 0);
    expectNear(shown(*source8, kWidth - 8, 8), {40, 40, 40}, 0);
    EXPECT_EQ(valueOf(describeImage(source8->subimages()[0]), "Color"), "Grayscale");

    PngOptions deep = eight;
    deep.depth = 16;
    const auto source16 = open(write("gray16.png", encodePng(deep)));
    ASSERT_TRUE(source16);
    EXPECT_EQ(source16->subimages()[0].format, PixelFormat::Gray16);
    expectNear(shown(*source16, 8, 8), {200, 200, 200}, 0);

    PngOptions one = eight;
    one.depth = 1;
    const auto source1 = open(write("gray1.png", encodePng(one)));
    ASSERT_TRUE(source1);
    expectNear(shown(*source1, 8, 8), {255, 255, 255}, 0);
    expectNear(shown(*source1, kWidth - 8, 8), {0, 0, 0}, 0);
    EXPECT_EQ(valueOf(describeImage(source1->subimages()[0]), "Bit depth"), "1-bit");
}

TEST(Png, PaletteWithAndWithoutTransparency) {
    PngOptions options;
    options.colorType = PNG_COLOR_TYPE_PALETTE;
    const auto source = open(write("palette.png", encodePng(options)));
    ASSERT_TRUE(source);
    expectPicture(*source, 0);
    EXPECT_EQ(valueOf(describeImage(source->subimages()[0]), "Color"), "Palette");

    options.transparentColor = true;
    const auto transparent = open(write("palette_t.png", encodePng(options)));
    ASSERT_TRUE(transparent);
    expectNear(shown(*transparent, 8, 8), {200, 40, 20}, 0);
    expectNear(shown(*transparent, kWidth - 8, 8), {128, 128, 128}, 0);
    EXPECT_EQ(valueOf(describeImage(transparent->subimages()[0]), "Color"), "Palette + alpha");
}

TEST(Png, AlphaIsFlattenedAtBothDepthsAndForGray) {
    PngOptions rgba;
    rgba.colorType = PNG_COLOR_TYPE_RGBA;
    const auto source = open(write("rgba.png", encodePng(rgba)));
    ASSERT_TRUE(source);
    expectPicture(*source, 0);
    expectNear(shown(*source, kWidth - 8, kHeight - 8), {128, 128, 128}, 0);

    rgba.depth = 16;
    const auto deep = open(write("rgba16.png", encodePng(rgba)));
    ASSERT_TRUE(deep);
    EXPECT_EQ(deep->subimages()[0].format, PixelFormat::Rgba16);
    expectPicture(*deep, 0);
    expectNear(shown(*deep, kWidth - 8, kHeight - 8), {128, 128, 128}, 1);

    PngOptions grayAlpha;
    grayAlpha.colorType = PNG_COLOR_TYPE_GRAY_ALPHA;
    const auto gray = open(write("gray_a.png", encodePng(grayAlpha)));
    ASSERT_TRUE(gray);
    expectNear(shown(*gray, 8, 8), {200, 200, 200}, 0);
    expectNear(shown(*gray, kWidth - 8, kHeight - 8), {128, 128, 128}, 0);

    // A transparent colour turns 16-bit gray into a format with room for the result.
    PngOptions keyed;
    keyed.colorType = PNG_COLOR_TYPE_GRAY;
    keyed.depth = 16;
    keyed.transparentColor = true;
    const auto colorKey = open(write("gray_t.png", encodePng(keyed)));
    ASSERT_TRUE(colorKey);
    EXPECT_EQ(colorKey->subimages()[0].format, PixelFormat::Rgba16);
    expectNear(shown(*colorKey, 8, 8), {200, 200, 200}, 0);
    expectNear(shown(*colorKey, kWidth - 8, 8), {128, 128, 128}, 1);
}

TEST(Png, InterlacedIsComplete) {
    PngOptions options;
    options.interlaced = true;
    const auto source = open(write("adam7.png", encodePng(options)));
    ASSERT_TRUE(source);
    const auto tile = source->readTile({0, 0, 0, 0}, {});
    ASSERT_TRUE(tile.has_value());
    // Every pixel, since a missed pass would leave holes.
    for (int y = 0; y < kHeight; ++y) {
        const auto* row = reinterpret_cast<const std::uint8_t*>(tile->row(y));
        for (int x = 0; x < kWidth; ++x) {
            ASSERT_EQ(row[x * 4 + 2], x < kWidth / 2 ? 200 : 20) << x << "," << y;
        }
    }
}

TEST(Png, ReadsIccAndExifEvenAfterThePixels) {
    // libpng checks the structure of a profile, so this one needs a proper header.
    Bytes icc(400, 0);
    icc[2] = 1;  // size
    icc[3] = 144;
    icc[8] = 2;    // version
    icc[131] = 1;  // one tag, filling the rest; libpng wants more than a tiny profile
    std::memcpy(&icc[132], "test", 4);
    icc[139] = 144;
    icc[142] = 1;
    icc[143] = 0;
    for (std::size_t i = 144; i < icc.size(); ++i) {
        icc[i] = static_cast<std::uint8_t>(i * i * 31 + i * 7);  // does not compress away
    }
    std::memcpy(&icc[12], "mntrRGB XYZ ", 12);
    std::memcpy(&icc[36], "acsp", 4);
    const std::array<std::uint8_t, 12> d50{0, 0, 0xF6, 0xD6, 0, 1, 0, 0, 0, 0, 0xD3, 0x2D};
    std::memcpy(&icc[68], d50.data(), d50.size());
    PngOptions options;
    options.icc = icc;
    Bytes bytes = encodePng(options);
    Bytes block = test::exampleExifBlock(true);
    block.erase(block.begin(), block.begin() + 6);
    insertChunk(bytes, "eXIf", block, true);
    const auto source = open(write("exif.png", bytes));
    ASSERT_TRUE(source);
    const SubimageInfo& info = source->subimages()[0];
    EXPECT_EQ(info.exif.model, "Canon EOS 5D");
    EXPECT_EQ(info.orientation, Orientation::Rotate90CW);
    EXPECT_EQ(info.iccProfile, options.icc);
    expectPicture(*source, 0);
}

TEST(Png, MarksAnApngAsAnimated) {
    Bytes bytes = encodePng({});
    insertChunk(bytes, "acTL", Bytes{0, 0, 0, 2, 0, 0, 0, 0}, false);
    const auto source = open(write("anim.png", bytes));
    ASSERT_TRUE(source);
    expectPicture(*source, 0);
    EXPECT_EQ(valueOf(describeImage(source->subimages()[0]), "Animation"), "First frame shown");
}

TEST(Png, CancelsAndReportsTruncation) {
    const Bytes bytes = encodePng({});
    const auto source = open(write("cancel.png", bytes));
    ASSERT_TRUE(source);
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = source->readTile({0, 0, 0, 0}, stop.get_token());
    ASSERT_FALSE(cancelled.has_value());
    EXPECT_EQ(cancelled.error(), "cancelled");
    EXPECT_TRUE(source->readTile({0, 0, 0, 0}, {}).has_value());

    const auto truncated = open(write("short.png", Bytes(bytes.begin(), bytes.begin() + 60)));
    ASSERT_TRUE(truncated);  // the header is intact
    EXPECT_FALSE(truncated->readTile({0, 0, 0, 0}, {}).has_value());
}

TEST(Png, SurvivesDamage) {
    PngOptions options;
    options.colorType = PNG_COLOR_TYPE_RGBA;
    options.interlaced = true;
    survivesDamage("x.png", encodePng(options));
}

#endif  // IV_HAVE_PNG

#ifdef IV_HAVE_WEBP

namespace {

Bytes bgraPixels() {
    Bytes bgra(static_cast<std::size_t>(kWidth) * kHeight * 4);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            std::array<std::uint8_t, 4> rgba{};
            pixelAt(x, y, rgba);
            auto* p = &bgra[(static_cast<std::size_t>(y) * kWidth + x) * 4];
            p[0] = rgba[2];
            p[1] = rgba[1];
            p[2] = rgba[0];
            p[3] = rgba[3];
        }
    }
    return bgra;
}

Bytes encodeWebp(bool lossless, bool withAlpha) {
    Bytes bgra = bgraPixels();
    if (!withAlpha) {
        for (std::size_t i = 3; i < bgra.size(); i += 4) {
            bgra[i] = 255;
        }
    }
    std::uint8_t* out = nullptr;
    const std::size_t size = lossless ? WebPEncodeLosslessBGRA(bgra.data(), kWidth, kHeight, kWidth * 4, &out)
                                      : WebPEncodeBGRA(bgra.data(), kWidth, kHeight, kWidth * 4, 95, &out);
    Bytes bytes(out, out + size);
    WebPFree(out);
    return bytes;
}

Bytes withWebpChunks(const Bytes& webp, const Bytes& exif, const Bytes& icc) {
    WebPMux* mux = WebPMuxCreate(std::array{WebPData{webp.data(), webp.size()}}.data(), 1);
    const WebPData exifData{exif.data(), exif.size()};
    const WebPData iccData{icc.data(), icc.size()};
    EXPECT_EQ(WebPMuxSetChunk(mux, "EXIF", &exifData, 1), WEBP_MUX_OK);
    EXPECT_EQ(WebPMuxSetChunk(mux, "ICCP", &iccData, 1), WEBP_MUX_OK);
    WebPData assembled;
    EXPECT_EQ(WebPMuxAssemble(mux, &assembled), WEBP_MUX_OK);
    Bytes out(assembled.bytes, assembled.bytes + assembled.size);
    WebPDataClear(&assembled);
    WebPMuxDelete(mux);
    return out;
}

}  // namespace

TEST(Webp, LossyAndLosslessAreToldApart) {
    const auto lossy = open(write("lossy.webp", encodeWebp(false, false)));
    ASSERT_TRUE(lossy);
    expectPicture(*lossy, 12);
    const auto lossyRows = describeImage(lossy->subimages()[0]);
    EXPECT_EQ(valueOf(lossyRows, "Format"), "WebP");
    EXPECT_EQ(valueOf(lossyRows, "Compression"), "WebP, lossy");
    EXPECT_EQ(valueOf(lossyRows, "Chroma"), "4:2:0");
    EXPECT_EQ(valueOf(lossyRows, "Color"), "RGB");

    const auto lossless = open(write("lossless.webp", encodeWebp(true, false)));
    ASSERT_TRUE(lossless);
    expectPicture(*lossless, 0);
    const auto rows = describeImage(lossless->subimages()[0]);
    EXPECT_EQ(valueOf(rows, "Compression"), "WebP, lossless");
    EXPECT_EQ(valueOf(rows, "Chroma"), "");
}

TEST(Webp, AlphaIsFlattened) {
    const auto source = open(write("alpha.webp", encodeWebp(true, true)));
    ASSERT_TRUE(source);
    expectPicture(*source, 0);
    expectNear(shown(*source, kWidth - 8, kHeight - 8), {128, 128, 128}, 0);
    EXPECT_EQ(valueOf(describeImage(source->subimages()[0]), "Color"), "RGB + alpha");
}

TEST(Webp, ReadsExifAndIcc) {
    Bytes block = test::exampleExifBlock(true);
    const Bytes icc{1, 2, 3, 4, 5, 6, 7, 8};
    const auto withPrefix = open(write("exif.webp", withWebpChunks(encodeWebp(true, false), block, icc)));
    ASSERT_TRUE(withPrefix);
    const SubimageInfo& info = withPrefix->subimages()[0];
    EXPECT_EQ(info.iccProfile, icc);
    EXPECT_EQ(info.exif.model, "Canon EOS 5D");
    EXPECT_EQ(info.orientation, Orientation::Rotate90CW);

    // The specification wants the TIFF structure directly, without the JPEG prefix.
    block.erase(block.begin(), block.begin() + 6);
    const auto bare = open(write("exif2.webp", withWebpChunks(encodeWebp(true, false), block, icc)));
    ASSERT_TRUE(bare);
    EXPECT_EQ(bare->subimages()[0].exif.iso, 400);
}

TEST(Webp, AnimationShowsItsFirstFrame) {
    WebPAnimEncoderOptions options;
    ASSERT_TRUE(WebPAnimEncoderOptionsInit(&options));
    WebPAnimEncoder* encoder = WebPAnimEncoderNew(kWidth, kHeight, &options);
    ASSERT_NE(encoder, nullptr);
    WebPConfig config;
    ASSERT_TRUE(WebPConfigInit(&config));
    config.lossless = 1;
    Bytes bgra = bgraPixels();
    int timestamp = 0;
    for (int frame = 0; frame < 2; ++frame) {
        for (std::size_t i = 0; i < bgra.size(); i += 4) {
            bgra[i + 3] = 255;
            if (frame == 1) {
                bgra[i] = bgra[i + 1] = bgra[i + 2] = 99;
            }
        }
        WebPPicture picture;
        ASSERT_TRUE(WebPPictureInit(&picture));
        picture.use_argb = 1;
        picture.width = kWidth;
        picture.height = kHeight;
        ASSERT_TRUE(WebPPictureImportBGRA(&picture, bgra.data(), kWidth * 4));
        ASSERT_TRUE(WebPAnimEncoderAdd(encoder, &picture, timestamp, &config));
        WebPPictureFree(&picture);
        timestamp += 100;
    }
    ASSERT_TRUE(WebPAnimEncoderAdd(encoder, nullptr, timestamp, nullptr));
    WebPData data;
    WebPDataInit(&data);
    ASSERT_TRUE(WebPAnimEncoderAssemble(encoder, &data));
    const Bytes bytes(data.bytes, data.bytes + data.size);
    WebPDataClear(&data);
    WebPAnimEncoderDelete(encoder);

    const auto source = open(write("anim.webp", bytes));
    ASSERT_TRUE(source);
    expectNear(shown(*source, 8, 8), {200, 40, 20}, 0);
    EXPECT_EQ(valueOf(describeImage(source->subimages()[0]), "Animation"), "First frame shown");
}

TEST(Webp, SurvivesDamage) { survivesDamage("x.webp", encodeWebp(false, true)); }

#endif  // IV_HAVE_WEBP

#ifdef IV_HAVE_AVIF

namespace {

struct AvifOptions {
    int depth = 8;
    bool lossless = false;
    bool alpha = false;
    avifPixelFormat yuv = AVIF_PIXEL_FORMAT_YUV420;
    int rotationAngle = -1;
    Bytes exif;
    Bytes icc;
};

bool canEncodeAvif() { return avifCodecName(AVIF_CODEC_CHOICE_AUTO, AVIF_CODEC_FLAG_CAN_ENCODE) != nullptr; }

Bytes encodeAvif(const AvifOptions& options) {
    avifImage* image = avifImageCreate(kWidth, kHeight, static_cast<std::uint32_t>(options.depth),
                                       options.lossless ? AVIF_PIXEL_FORMAT_YUV444 : options.yuv);
    if (options.lossless) {
        image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
        image->yuvRange = AVIF_RANGE_FULL;
    }
    if (options.rotationAngle >= 0) {
        image->transformFlags |= AVIF_TRANSFORM_IROT;
        image->irot.angle = static_cast<std::uint8_t>(options.rotationAngle);
    }
    if (!options.exif.empty()) {
        EXPECT_EQ(avifImageSetMetadataExif(image, options.exif.data(), options.exif.size()), AVIF_RESULT_OK);
        // That call also copies the EXIF orientation to the container, which a test may overrule.
        if (options.rotationAngle < 0) {
            image->transformFlags = AVIF_TRANSFORM_NONE;
        }
    }
    if (!options.icc.empty()) {
        EXPECT_EQ(avifImageSetProfileICC(image, options.icc.data(), options.icc.size()), AVIF_RESULT_OK);
    }

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, image);
    rgb.depth = 8;
    rgb.format = AVIF_RGB_FORMAT_RGBA;
    EXPECT_EQ(avifRGBImageAllocatePixels(&rgb), AVIF_RESULT_OK);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            std::array<std::uint8_t, 4> rgba{};
            pixelAt(x, y, rgba);
            if (!options.alpha) {
                rgba[3] = 255;
            }
            std::memcpy(rgb.pixels + static_cast<std::size_t>(y) * rgb.rowBytes + static_cast<std::size_t>(x) * 4, rgba.data(), 4);
        }
    }
    EXPECT_EQ(avifImageRGBToYUV(image, &rgb), AVIF_RESULT_OK);
    avifRGBImageFreePixels(&rgb);

    avifEncoder* encoder = avifEncoderCreate();
    encoder->speed = AVIF_SPEED_FASTEST;
    encoder->quality = options.lossless ? AVIF_QUALITY_LOSSLESS : 90;
    encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
    avifRWData output = AVIF_DATA_EMPTY;
    const avifResult result = avifEncoderWrite(encoder, image, &output);
    EXPECT_EQ(result, AVIF_RESULT_OK) << avifResultToString(result) << " " << encoder->diag.error;
    Bytes bytes(output.data, output.data + output.size);
    avifRWDataFree(&output);
    avifEncoderDestroy(encoder);
    avifImageDestroy(image);
    return bytes;
}

}  // namespace

class AvifTest : public testing::Test {
protected:
    void SetUp() override {
        if (!canEncodeAvif()) {
            GTEST_SKIP() << "no AV1 encoder available to create test files";
        }
    }
};

TEST_F(AvifTest, EightBitLossyWithSubsampling) {
    const auto source = open(write("lossy.avif", encodeAvif({})));
    ASSERT_TRUE(source);
    EXPECT_EQ(source->subimages()[0].format, PixelFormat::Bgrx8);
    expectPicture(*source, 25);
    const auto rows = describeImage(source->subimages()[0]);
    EXPECT_EQ(valueOf(rows, "Format"), "AVIF");
    EXPECT_EQ(valueOf(rows, "Compression"), "AV1, lossy");
    EXPECT_EQ(valueOf(rows, "Chroma"), "4:2:0");
    EXPECT_EQ(valueOf(rows, "Bit depth"), "8-bit");
}

TEST_F(AvifTest, LosslessIsRecognized) {
    AvifOptions options;
    options.lossless = true;
    const auto source = open(write("lossless.avif", encodeAvif(options)));
    ASSERT_TRUE(source);
    expectPicture(*source, 0);
    const auto rows = describeImage(source->subimages()[0]);
    EXPECT_EQ(valueOf(rows, "Compression"), "AV1, lossless");
    EXPECT_EQ(valueOf(rows, "Color"), "RGB");
    EXPECT_EQ(valueOf(rows, "Chroma"), "");
}

TEST_F(AvifTest, TenBitKeepsItsPrecision) {
    AvifOptions options;
    options.depth = 10;
    const auto source = open(write("ten.avif", encodeAvif(options)));
    ASSERT_TRUE(source);
    EXPECT_EQ(source->subimages()[0].format, PixelFormat::Rgba16);
    expectPicture(*source, 25);
    EXPECT_EQ(valueOf(describeImage(source->subimages()[0]), "Bit depth"), "10-bit");
    const auto histogram = computeHistogram(*source, 0, {});
    ASSERT_TRUE(histogram.has_value());
    EXPECT_EQ(histogram->binCount(), 65536);
}

TEST_F(AvifTest, AlphaIsFlattened) {
    AvifOptions options;
    options.alpha = true;
    const auto source = open(write("alpha.avif", encodeAvif(options)));
    ASSERT_TRUE(source);
    expectNear(shown(*source, kWidth - 8, kHeight - 8), {128, 128, 128}, 25);
    expectNear(shown(*source, 8, 8), {200, 40, 20}, 25);
    EXPECT_EQ(valueOf(describeImage(source->subimages()[0]), "Color"), "YCbCr + alpha");
}

TEST_F(AvifTest, ContainerRotationIsTheOrientation) {
    AvifOptions options;
    options.rotationAngle = 3;
    const auto source = open(write("rotated.avif", encodeAvif(options)));
    ASSERT_TRUE(source);
    EXPECT_EQ(source->subimages()[0].orientation, Orientation::Rotate90CW);
}

TEST_F(AvifTest, ExifAndIccAreRead) {
    AvifOptions options;
    options.exif = test::exampleExifBlock(true);
    options.exif.erase(options.exif.begin(), options.exif.begin() + 6);  // libavif wants plain TIFF
    options.icc = {1, 2, 3, 4};
    const auto source = open(write("exif.avif", encodeAvif(options)));
    ASSERT_TRUE(source);
    const SubimageInfo& info = source->subimages()[0];
    EXPECT_EQ(info.exif.model, "Canon EOS 5D");
    EXPECT_EQ(info.iccProfile, options.icc);
    // The EXIF block says rotated while the container says not: the container wins.
    EXPECT_EQ(info.orientation, Orientation::Normal);
}

TEST_F(AvifTest, SurvivesDamage) { survivesDamage("x.avif", encodeAvif({})); }

#endif  // IV_HAVE_AVIF

#ifdef IV_HAVE_JXL

namespace {

struct JxlOptions {
    int bits = 8;
    bool isFloat = false;
    bool alpha = false;
    bool lossless = true;
    bool gray = false;
    Bytes exifBox;
};

Bytes finishJxl(JxlEncoder* encoder) {
    Bytes out(64 * 1024);
    std::uint8_t* next = out.data();
    std::size_t available = out.size();
    JxlEncoderStatus status = JXL_ENC_NEED_MORE_OUTPUT;
    while (status == JXL_ENC_NEED_MORE_OUTPUT) {
        status = JxlEncoderProcessOutput(encoder, &next, &available);
        if (status == JXL_ENC_NEED_MORE_OUTPUT) {
            const std::size_t used = static_cast<std::size_t>(next - out.data());
            out.resize(out.size() * 2);
            next = out.data() + used;
            available = out.size() - used;
        }
    }
    EXPECT_EQ(status, JXL_ENC_SUCCESS);
    out.resize(static_cast<std::size_t>(next - out.data()));
    return out;
}

Bytes encodeJxl(const JxlOptions& options) {
    const JxlEncoderPtr encoder = JxlEncoderMake(nullptr);
    JxlBasicInfo basic;
    JxlEncoderInitBasicInfo(&basic);
    basic.xsize = kWidth;
    basic.ysize = kHeight;
    basic.bits_per_sample = static_cast<std::uint32_t>(options.bits);
    basic.exponent_bits_per_sample = options.isFloat ? 8 : 0;
    basic.uses_original_profile = options.lossless ? JXL_TRUE : JXL_FALSE;
    basic.num_color_channels = options.gray ? 1 : 3;
    if (options.alpha) {
        basic.alpha_bits = static_cast<std::uint32_t>(options.bits);
        basic.alpha_exponent_bits = options.isFloat ? 8 : 0;
        basic.num_extra_channels = 1;
    }
    if (!options.exifBox.empty()) {
        EXPECT_EQ(JxlEncoderUseBoxes(encoder.get()), JXL_ENC_SUCCESS);
    }
    EXPECT_EQ(JxlEncoderSetBasicInfo(encoder.get(), &basic), JXL_ENC_SUCCESS);
    JxlColorEncoding color;
    JxlColorEncodingSetToSRGB(&color, options.gray ? JXL_TRUE : JXL_FALSE);
    EXPECT_EQ(JxlEncoderSetColorEncoding(encoder.get(), &color), JXL_ENC_SUCCESS);
    if (!options.exifBox.empty()) {
        EXPECT_EQ(JxlEncoderAddBox(encoder.get(), "Exif", options.exifBox.data(), options.exifBox.size(), JXL_FALSE), JXL_ENC_SUCCESS);
    }

    JxlEncoderFrameSettings* settings = JxlEncoderFrameSettingsCreate(encoder.get(), nullptr);
    if (options.lossless) {
        EXPECT_EQ(JxlEncoderSetFrameLossless(settings, JXL_TRUE), JXL_ENC_SUCCESS);
    } else {
        EXPECT_EQ(JxlEncoderSetFrameDistance(settings, 0.5F), JXL_ENC_SUCCESS);
    }

    const int channels = options.gray ? 1 : options.alpha ? 4 : 3;
    std::vector<std::uint8_t> samples8;
    std::vector<std::uint16_t> samples16;
    std::vector<float> samplesFloat;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            std::array<std::uint8_t, 4> rgba{};
            pixelAt(x, y, rgba);
            if (options.gray) {
                rgba[0] = x < kWidth / 2 ? 200 : 40;
            }
            for (int c = 0; c < channels; ++c) {
                const std::uint8_t value = rgba[static_cast<std::size_t>(c)];
                if (options.isFloat) {
                    samplesFloat.push_back(static_cast<float>(value) / 255.0F);
                } else if (options.bits > 8) {
                    samples16.push_back(static_cast<std::uint16_t>(value * 257));
                } else {
                    samples8.push_back(value);
                }
            }
        }
    }
    JxlPixelFormat format{static_cast<std::uint32_t>(channels),
                          options.isFloat ? JXL_TYPE_FLOAT : options.bits > 8 ? JXL_TYPE_UINT16 : JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    const void* data = options.isFloat         ? static_cast<const void*>(samplesFloat.data())
                       : options.bits > 8      ? static_cast<const void*>(samples16.data())
                                               : static_cast<const void*>(samples8.data());
    const std::size_t size = samplesFloat.size() * 4 + samples16.size() * 2 + samples8.size();
    EXPECT_EQ(JxlEncoderAddImageFrame(settings, &format, data, size), JXL_ENC_SUCCESS);
    JxlEncoderCloseInput(encoder.get());
    return finishJxl(encoder.get());
}

}  // namespace

TEST(Jxl, LosslessEightBit) {
    const auto source = open(write("lossless.jxl", encodeJxl({})));
    ASSERT_TRUE(source);
    EXPECT_EQ(source->subimages()[0].format, PixelFormat::Bgrx8);
    expectPicture(*source, 0);
    const auto rows = describeImage(source->subimages()[0]);
    EXPECT_EQ(valueOf(rows, "Format"), "JPEG XL");
    EXPECT_EQ(valueOf(rows, "Compression"), "JPEG XL, likely lossless");
    EXPECT_EQ(valueOf(rows, "Bit depth"), "8-bit");
}

TEST(Jxl, LossyIsToldApart) {
    JxlOptions options;
    options.lossless = false;
    const auto source = open(write("lossy.jxl", encodeJxl(options)));
    ASSERT_TRUE(source);
    expectPicture(*source, 12);
    EXPECT_EQ(valueOf(describeImage(source->subimages()[0]), "Compression"), "JPEG XL, lossy");
}

TEST(Jxl, SixteenBitAndFloatKeepTheirPrecision) {
    JxlOptions deep;
    deep.bits = 16;
    const auto source = open(write("deep.jxl", encodeJxl(deep)));
    ASSERT_TRUE(source);
    EXPECT_EQ(source->subimages()[0].format, PixelFormat::Rgba16);
    expectPicture(*source, 1);
    EXPECT_EQ(valueOf(describeImage(source->subimages()[0]), "Bit depth"), "16-bit");

    JxlOptions floats;
    floats.bits = 32;
    floats.isFloat = true;
    const auto floatSource = open(write("float.jxl", encodeJxl(floats)));
    ASSERT_TRUE(floatSource);
    EXPECT_EQ(floatSource->subimages()[0].format, PixelFormat::Rgba32F);
    expectPicture(*floatSource, 1);
    EXPECT_EQ(valueOf(describeImage(floatSource->subimages()[0]), "Bit depth"), "32-bit float");
}

TEST(Jxl, GrayHighDepthIsGrayFormat) {
    JxlOptions gray;
    gray.gray = true;
    gray.bits = 16;
    const auto source = open(write("gray.jxl", encodeJxl(gray)));
    ASSERT_TRUE(source);
    EXPECT_EQ(source->subimages()[0].format, PixelFormat::Gray16);
    expectNear(shown(*source, 8, 8), {200, 200, 200}, 1);
    expectNear(shown(*source, kWidth - 8, 8), {40, 40, 40}, 1);
    EXPECT_EQ(valueOf(describeImage(source->subimages()[0]), "Color"), "Grayscale");

    JxlOptions gray8;
    gray8.gray = true;
    const auto eight = open(write("gray8.jxl", encodeJxl(gray8)));
    ASSERT_TRUE(eight);
    expectNear(shown(*eight, 8, 8), {200, 200, 200}, 1);
}

TEST(Jxl, AlphaIsFlattened) {
    JxlOptions options;
    options.alpha = true;
    const auto source = open(write("alpha.jxl", encodeJxl(options)));
    ASSERT_TRUE(source);
    expectNear(shown(*source, kWidth - 8, kHeight - 8), {128, 128, 128}, 0);
    expectNear(shown(*source, 8, 8), {200, 40, 20}, 0);
    EXPECT_EQ(valueOf(describeImage(source->subimages()[0]), "Color"), "RGB + alpha");

    options.bits = 16;
    const auto deep = open(write("alpha16.jxl", encodeJxl(options)));
    ASSERT_TRUE(deep);
    expectNear(shown(*deep, kWidth - 8, kHeight - 8), {128, 128, 128}, 1);
}

TEST(Jxl, ReadsAnExifBox) {
    JxlOptions options;
    Bytes block = test::exampleExifBlock(true);
    block.erase(block.begin(), block.begin() + 6);
    options.exifBox = {0, 0, 0, 0};  // the offset of the TIFF structure
    options.exifBox.insert(options.exifBox.end(), block.begin(), block.end());
    const auto source = open(write("exif.jxl", encodeJxl(options)));
    ASSERT_TRUE(source);
    const SubimageInfo& info = source->subimages()[0];
    EXPECT_EQ(info.exif.model, "Canon EOS 5D");
    EXPECT_EQ(info.exif.iso, 400);
    EXPECT_EQ(info.orientation, Orientation::Normal);
    expectPicture(*source, 0);
}

TEST(Jxl, RecognizesARecompressedJpeg) {
    test::JpegOptions jpeg;
    jpeg.width = kWidth;
    jpeg.height = kHeight;
    const Bytes jpegBytes = test::encodeJpeg(jpeg);

    const JxlEncoderPtr encoder = JxlEncoderMake(nullptr);
    ASSERT_EQ(JxlEncoderUseContainer(encoder.get(), JXL_TRUE), JXL_ENC_SUCCESS);
    ASSERT_EQ(JxlEncoderStoreJPEGMetadata(encoder.get(), JXL_TRUE), JXL_ENC_SUCCESS);
    JxlEncoderFrameSettings* settings = JxlEncoderFrameSettingsCreate(encoder.get(), nullptr);
    ASSERT_EQ(JxlEncoderAddJPEGFrame(settings, jpegBytes.data(), jpegBytes.size()), JXL_ENC_SUCCESS);
    JxlEncoderCloseInput(encoder.get());
    const auto source = open(write("jpeg.jxl", finishJxl(encoder.get())));
    ASSERT_TRUE(source);
    EXPECT_EQ(valueOf(describeImage(source->subimages()[0]), "Compression"), "JPEG XL, recompressed JPEG");
    expectNear(shown(*source, 8, 8), {128, 128, 128}, 3);
}

TEST(Jxl, SurvivesDamage) { survivesDamage("x.jxl", encodeJxl({})); }

#endif  // IV_HAVE_JXL

TEST(WholeImage, SupportsThumbnailsCancellingAndMissingTiles) {
#ifdef IV_HAVE_JXL
    const auto path = write("thumb.jxl", encodeJxl({}));
    const auto thumbnail = makeThumbnail(path, 32, {});
    ASSERT_TRUE(thumbnail.has_value()) << thumbnail.error();
    EXPECT_EQ(thumbnail->width(), 32);

    const auto source = open(path);
    ASSERT_TRUE(source);
    EXPECT_FALSE(source->readTile({0, 0, 1, 0}, {}).has_value());
    EXPECT_FALSE(source->readTile({0, 1, 0, 0}, {}).has_value());
    EXPECT_FALSE(source->readTile({0, 0, -1, 0}, {}).has_value());

    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = source->readTile({0, 0, 0, 0}, stop.get_token());
    ASSERT_FALSE(cancelled.has_value());
    EXPECT_EQ(cancelled.error(), "cancelled");
    EXPECT_TRUE(source->readTile({0, 0, 0, 0}, {}).has_value());  // cancelling is not a failure
#else
    GTEST_SKIP();
#endif
}
