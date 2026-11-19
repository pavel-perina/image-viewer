#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
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

#include "core/document.h"
#include "core/histogram.h"
#include "core/color.h"
#include "core/image_details.h"
#include "core/image_ops.h"
#include "core/image_source.h"
#include "core/overview_source.h"
#include "core/pixel_probe.h"
#include "core/save_image.h"
#include "core/thumbnail.h"
#include "core/whole_image_source.h"
#include "test_exif.h"
#include "test_formats.h"
#include "test_jpeg.h"

using namespace iv;
using Bytes = std::vector<std::uint8_t>;

using namespace iv::test::formats;

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

// Showing a picture with alpha on mid gray, which is how transparency looks until it is drawn.
// The values are worked out by hand: half of white and gray is 192. The source keeps its
// pixels, as other readers share them.
TEST(Alpha, FlattensACopyOntoMidGray) {
    const ImageBuffer image = ImageBuffer::create({3, 1}, PixelFormat::Bgrx8).withAlpha(true);
    const std::array<std::uint8_t, 12> pixels{10, 20, 30, 255, 10, 20, 30, 0, 255, 255, 255, 128};
    std::memcpy(image.row(0), pixels.data(), pixels.size());
    const ImageBuffer flat = flattened(image);
    EXPECT_FALSE(flat.hasAlpha());
    const auto* out = reinterpret_cast<const std::uint8_t*>(flat.row(0));
    EXPECT_EQ(out[0], 10);  // opaque stays
    EXPECT_EQ(out[4], 128);  // transparent shows the background
    EXPECT_EQ(out[7], 255);  // and becomes opaque
    EXPECT_NEAR(out[8], 192, 1);  // half of white and gray
    EXPECT_EQ(std::memcmp(image.row(0), pixels.data(), pixels.size()), 0) << "the source is left alone";

    const ImageBuffer deep = ImageBuffer::create({1, 1}, PixelFormat::Rgba16).withAlpha(true);
    const std::array<std::uint16_t, 4> pixel16{65535, 0, 0, 0};
    std::memcpy(deep.row(0), pixel16.data(), sizeof(pixel16));
    EXPECT_NEAR(reinterpret_cast<const std::uint16_t*>(flattened(deep).row(0))[0], 32768, 1);
}

// Without alpha the fourth channel means nothing, so whatever it holds changes no colour, and
// nothing is copied.
TEST(Alpha, APictureWithoutAlphaIsLeftAsItIs) {
    const ImageBuffer image = ImageBuffer::create({1, 1}, PixelFormat::Bgrx8);
    const std::array<std::uint8_t, 4> pixel{10, 20, 30, 0};
    std::memcpy(image.row(0), pixel.data(), pixel.size());
    const ImageBuffer flat = flattened(image);
    EXPECT_EQ(flat.data(), image.data());
    EXPECT_EQ(std::memcmp(flat.row(0), pixel.data(), pixel.size()), 0);
}

// A part of a picture has alpha as the whole has.
TEST(Alpha, AViewKeepsIt) {
    const ImageBuffer image = ImageBuffer::create({4, 4}, PixelFormat::Rgba16).withAlpha(true);
    EXPECT_TRUE(image.view(1, 1, 2, 2).hasAlpha());
    EXPECT_FALSE(image.withAlpha(false).view(1, 1, 2, 2).hasAlpha());
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

std::uint32_t pngChecksum(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return ~crc;
}

// Puts a chunk just after the header or just before the end.
void insertChunk(Bytes& png, const char* type, const Bytes& payload, bool atEnd) {
    Bytes chunk{std::uint8_t(payload.size() >> 24), std::uint8_t(payload.size() >> 16), std::uint8_t(payload.size() >> 8), std::uint8_t(payload.size())};
    chunk.insert(chunk.end(), type, type + 4);
    chunk.insert(chunk.end(), payload.begin(), payload.end());
    const std::uint32_t crc = pngChecksum(chunk.data() + 4, chunk.size() - 4);
    for (const int shift : {24, 16, 8, 0}) {
        chunk.push_back(static_cast<std::uint8_t>(crc >> shift));
    }
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

// Alpha is decoded as the file has it, and shown on mid gray for now. PNG is lossless and
// keeps the colour of transparent pixels, so that is compared too.
// Why: a strip with a transparent background, or a cut-out logo in it, needs each picture's
// alpha, while thumbnails flatten it. Input: the test picture as 8-bit and 16-bit RGBA PNG,
// asked for at its own size. The truth: pixelAt, in 8 bits: opaque red-ish on the left, the
// half transparent rows at alpha 128 with their own colour, and the transparent corner at 0.
TEST(Alpha, AnSrgbCopyKeepsAlphaAtEitherDepth) {
    for (const int depth : {8, 16}) {
        SCOPED_TRACE(depth);
        PngOptions rgba;
        rgba.colorType = PNG_COLOR_TYPE_RGBA;
        rgba.depth = depth;
        const auto copy = makeSrgbCopy(write("srgb_copy" + std::to_string(depth) + ".png", encodePng(rgba)), {kWidth, kHeight}, {});
        ASSERT_TRUE(copy) << copy.error();
        ASSERT_EQ(copy->format(), PixelFormat::Bgrx8);
        EXPECT_TRUE(copy->hasAlpha());
        const auto pixel = [&](int x, int y) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(copy->row(y)) + x * 4;
            return std::array<int, 4>{p[2], p[1], p[0], p[3]};
        };
        EXPECT_EQ(pixel(8, 8), (std::array<int, 4>{200, 40, 20, 255}));
        EXPECT_EQ(pixel(kWidth - 8, kHeight / 2 + 1), (std::array<int, 4>{20, 40, 200, 128}));
        EXPECT_EQ(pixel(kWidth - 8, kHeight - 8)[3], 0);
    }
}

TEST(Png, AlphaIsKeptAtBothDepthsAndForGrayAndShownOnMidGray) {
    PngOptions rgba;
    rgba.colorType = PNG_COLOR_TYPE_RGBA;
    const auto source = open(write("rgba.png", encodePng(rgba)));
    ASSERT_TRUE(source);
    expectPicture(*source, 0);
    expectNear(shown(*source, kWidth - 8, kHeight - 8), {128, 128, 128}, 0);
    expectAlphaKept(*source, 0);
    EXPECT_EQ(decoded(*source, kWidth - 8, kHeight - 8), (std::array<int, 4>{20, 40, 200, 0}));

    rgba.depth = 16;
    const auto deep = open(write("rgba16.png", encodePng(rgba)));
    ASSERT_TRUE(deep);
    EXPECT_EQ(deep->subimages()[0].format, PixelFormat::Rgba16);
    expectPicture(*deep, 0);
    expectNear(shown(*deep, kWidth - 8, kHeight - 8), {128, 128, 128}, 1);
    expectAlphaKept(*deep, 0);

    const auto opaque = open(write("rgb_opaque.png", encodePng(PngOptions{})));
    ASSERT_TRUE(opaque);
    expectNoAlpha(*opaque);

    PngOptions grayAlpha;
    grayAlpha.colorType = PNG_COLOR_TYPE_GRAY_ALPHA;
    const auto gray = open(write("gray_a.png", encodePng(grayAlpha)));
    ASSERT_TRUE(gray);
    expectNear(shown(*gray, 8, 8), {200, 200, 200}, 0);
    expectNear(shown(*gray, kWidth - 8, kHeight - 8), {128, 128, 128}, 0);
    EXPECT_EQ(decoded(*gray, kWidth - 8, kHeight / 2 + 1), (std::array<int, 4>{40, 40, 40, 128}));

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

namespace {

Bytes bigEndian32(std::initializer_list<std::uint32_t> values) {
    Bytes out;
    for (const std::uint32_t v : values) {
        for (const int shift : {24, 16, 8, 0}) {
            out.push_back(static_cast<std::uint8_t>(v >> shift));
        }
    }
    return out;
}

// The profile a PNG ends up with once these chunks are put before its pixels.
std::vector<std::uint8_t> pngProfile(const std::string& name, const std::vector<std::pair<const char*, Bytes>>& chunks, const Bytes& icc = {}) {
    PngOptions options;
    options.icc = icc;
    Bytes png = encodePng(options);
    for (const auto& [type, payload] : chunks) {
        insertChunk(png, type, payload, false);
    }
    return open(write(name, png))->subimages()[0].iccProfile;
}

}  // namespace

TEST(PngColor, ChunksDescribeTheColours) {
    EXPECT_TRUE(pngProfile("none.png", {}).empty());
    EXPECT_TRUE(pngProfile("srgb.png", {{"sRGB", {0}}}).empty());
    // What encoders write when they mean sRGB, so it must not turn into a slow transform.
    EXPECT_TRUE(pngProfile("g22.png", {{"gAMA", bigEndian32({45455})}}).empty());
    EXPECT_FALSE(pngProfile("g1.png", {{"gAMA", bigEndian32({100000})}}).empty());

    const Bytes adobeChrm = bigEndian32({31270, 32900, 64000, 33000, 21000, 71000, 15000, 6000});
    const auto wide = pngProfile("chrm.png", {{"gAMA", bigEndian32({45455})}, {"cHRM", adobeChrm}});
    SubimageInfo info;
    info.stored.colorModel = "RGB";
    info.iccProfile = wide;
    EXPECT_NE(displayTransform(info, {}), nullptr);
}

TEST(PngColor, PrecedenceFollowsTheSpecification) {
    const IccBytes adobe = adobeRgbProfile();
    // A profile wins over sRGB and gamma chunks,
    EXPECT_EQ(pngProfile("icc_gamma.png", {{"gAMA", bigEndian32({100000})}}, adobe), adobe);
    // and an sRGB chunk over a gamma chunk,
    EXPECT_TRUE(pngProfile("srgb_gamma.png", {{"gAMA", bigEndian32({100000})}, {"sRGB", {0}}}).empty());
#ifdef PNG_cICP_SUPPORTED
    // and cICP over them all.
    const auto p3 = pngProfile("cicp.png", {{"cICP", {12, 13, 0, 1}}}, adobe);
    EXPECT_EQ(iccDescription(p3), "Display P3");
    EXPECT_TRUE(pngProfile("cicp_srgb.png", {{"cICP", {1, 13, 0, 1}}}, adobe).empty());
#endif
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

TEST(Webp, AlphaIsKeptAndShownOnMidGray) {
    const auto source = open(write("alpha.webp", encodeWebp(true, true)));
    ASSERT_TRUE(source);
    expectPicture(*source, 0);
    expectNear(shown(*source, kWidth - 8, kHeight - 8), {128, 128, 128}, 0);
    expectAlphaKept(*source, 0);
    const auto lossy = open(write("alpha_lossy.webp", encodeWebp(false, true)));
    ASSERT_TRUE(lossy);
    expectAlphaKept(*lossy, 12);
    const auto opaque = open(write("opaque.webp", encodeWebp(true, false)));
    ASSERT_TRUE(opaque);
    expectNoAlpha(*opaque);
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
    int primaries = 0;  // 0 leaves the encoder's default
    int transfer = 0;
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

    if (options.primaries != 0) {
        image->colorPrimaries = static_cast<avifColorPrimaries>(options.primaries);
        image->transferCharacteristics = static_cast<avifTransferCharacteristics>(options.transfer);
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

TEST_F(AvifTest, AlphaIsKeptAndShownOnMidGray) {
    AvifOptions options;
    const auto opaque = open(write("opaque.avif", encodeAvif(options)));
    ASSERT_TRUE(opaque);
    expectNoAlpha(*opaque);
    options.alpha = true;
    const auto source = open(write("alpha.avif", encodeAvif(options)));
    ASSERT_TRUE(source);
    expectAlphaKept(*source, 25);
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

TEST_F(AvifTest, ColourCodePointsBecomeAProfile) {
    AvifOptions p3;
    p3.primaries = 12;
    p3.transfer = 13;
    EXPECT_EQ(iccDescription(open(write("p3.avif", encodeAvif(p3)))->subimages()[0].iccProfile), "Display P3");

    AvifOptions srgb;
    srgb.primaries = 1;
    srgb.transfer = 13;
    EXPECT_TRUE(open(write("srgb.avif", encodeAvif(srgb)))->subimages()[0].iccProfile.empty());

    AvifOptions hdr;  // cannot be expressed yet, and must not be mistaken for something else
    hdr.primaries = 9;
    hdr.transfer = 16;
    EXPECT_TRUE(open(write("hdr.avif", encodeAvif(hdr)))->subimages()[0].iccProfile.empty());

    AvifOptions both = p3;  // an embedded profile has the final say
    both.icc = adobeRgbProfile();
    EXPECT_EQ(open(write("both.avif", encodeAvif(both)))->subimages()[0].iccProfile, both.icc);
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

TEST(Jxl, AlphaIsKeptAndShownOnMidGray) {
    JxlOptions options;
    const auto opaque = open(write("opaque.jxl", encodeJxl(options)));
    ASSERT_TRUE(opaque);
    expectNoAlpha(*opaque);
    options.alpha = true;
    const auto source = open(write("alpha.jxl", encodeJxl(options)));
    ASSERT_TRUE(source);
    expectAlphaKept(*source, 0);
    expectNear(shown(*source, kWidth - 8, kHeight - 8), {128, 128, 128}, 0);
    expectNear(shown(*source, 8, 8), {200, 40, 20}, 0);
    EXPECT_EQ(valueOf(describeImage(source->subimages()[0]), "Color"), "RGB + alpha");

    options.bits = 16;
    const auto deep = open(write("alpha16.jxl", encodeJxl(options)));
    ASSERT_TRUE(deep);
    expectAlphaKept(*deep, 0);
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

// A level larger than one tile is copied together from its tiles, and has alpha as they have.
// The source is made up: 3000 pixels wide, so two tiles, of one half transparent colour.
TEST(Alpha, ALevelPutTogetherFromTilesKeepsIt) {
    const Size size{3000, 8};
    SubimageInfo info;
    info.alpha = true;
    const auto source = makeWholeImageSource(info, size, [size](const std::stop_token&) -> Result<ImageBuffer> {
        const ImageBuffer image = ImageBuffer::create(size, PixelFormat::Bgrx8);
        for (int y = 0; y < size.height; ++y) {
            auto* p = reinterpret_cast<std::uint8_t*>(image.row(y));
            for (int x = 0; x < size.width; ++x, p += 4) {
                p[0] = 10, p[1] = 20, p[2] = 30, p[3] = 128;
            }
        }
        return image.withAlpha(true);
    });
    ASSERT_GT(size.width, source->subimages()[0].levels[0].tileSize.width) << "the test needs two tiles";
    const auto level = readLevel(*source, 0, 0, {});
    ASSERT_TRUE(level.has_value());
    EXPECT_TRUE(level->hasAlpha());
    EXPECT_EQ(reinterpret_cast<const std::uint8_t*>(level->row(7))[2999 * 4 + 3], 128);
}

// Why: resizing mixes neighbouring pixels, and a transparent pixel has a colour all the same,
// which must not be mixed in: the edge of a red shape on transparent "green" would turn
// brown. Input: the left half opaque red, the right half fully transparent green, reduced to
// a quarter, at 8 and 16 bits. The truth: every pixel that shows at all is pure red, with no
// green in it; alpha goes from opaque to none, and the picture still says it has alpha.
TEST(Alpha, ResizingKeepsItAndDoesNotMixInTheColourUnderTransparency) {
    for (const PixelFormat format : {PixelFormat::Bgrx8, PixelFormat::Rgba16}) {
        SCOPED_TRACE(format == PixelFormat::Bgrx8 ? "8 bits" : "16 bits");
        const bool deep = format == PixelFormat::Rgba16;
        const int max = deep ? 65535 : 255;
        const ImageBuffer image = ImageBuffer::create({64, 16}, format).withAlpha(true);
        // Red, green and alpha of a pixel, whatever the order of the format's channels.
        const auto at = [&](const ImageBuffer& buffer, int x, int y) -> std::array<int, 3> {
            if (deep) {
                const auto* p = reinterpret_cast<const std::uint16_t*>(buffer.row(y)) + x * 4;
                return {p[0], p[1], p[3]};
            }
            const auto* p = reinterpret_cast<const std::uint8_t*>(buffer.row(y)) + x * 4;
            return {p[2], p[1], p[3]};
        };
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 64; ++x) {
                const bool left = x < 32;
                if (deep) {
                    auto* p = reinterpret_cast<std::uint16_t*>(image.row(y)) + x * 4;
                    p[0] = left ? 65535 : 0, p[1] = left ? 0 : 65535, p[2] = 0, p[3] = left ? 65535 : 0;
                } else {
                    auto* p = reinterpret_cast<std::uint8_t*>(image.row(y)) + x * 4;
                    p[0] = 0, p[1] = left ? 0 : 255, p[2] = left ? 255 : 0, p[3] = left ? 255 : 0;
                }
            }
        }
        const ImageBuffer small = resize(image, {16, 4});
        ASSERT_FALSE(small.isNull());
        EXPECT_TRUE(small.hasAlpha());
        EXPECT_EQ(at(small, 0, 2)[2], max);
        EXPECT_EQ(at(small, 15, 2)[2], 0);
        for (int x = 0; x < 16; ++x) {
            const int red = at(small, x, 2)[0];
            const int green = at(small, x, 2)[1];
            const int alpha = at(small, x, 2)[2];
            if (alpha > max / 50) {
                EXPECT_EQ(green, 0) << "column " << x;
                EXPECT_NEAR(red, max, max / 100) << "column " << x;
            }
        }
    }
}

// Why: straightening resamples the picture as resizing does, with a kernel that reaches into
// neighbours, so the same leak of hidden colour would show at transparent edges. Input: the
// left half opaque red, the right half transparent green, turned a little (a few degrees),
// at 8 and 16 bits. The truth: the result has alpha, every pixel that shows at all is red
// without green, and both opaque and fully transparent pixels remain.
TEST(Alpha, StraighteningKeepsItAndDoesNotMixInTheColourUnderTransparency) {
    for (const PixelFormat format : {PixelFormat::Bgrx8, PixelFormat::Rgba16}) {
        SCOPED_TRACE(format == PixelFormat::Bgrx8 ? "8 bits" : "16 bits");
        const bool deep = format == PixelFormat::Rgba16;
        const int max = deep ? 65535 : 255;
        const ImageBuffer image = ImageBuffer::create({64, 64}, format).withAlpha(true);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                const bool left = x < 32;
                if (deep) {
                    auto* p = reinterpret_cast<std::uint16_t*>(image.row(y)) + x * 4;
                    p[0] = left ? 65535 : 0, p[1] = left ? 0 : 65535, p[2] = 0, p[3] = left ? 65535 : 0;
                } else {
                    auto* p = reinterpret_cast<std::uint8_t*>(image.row(y)) + x * 4;
                    p[0] = 0, p[1] = left ? 0 : 255, p[2] = left ? 255 : 0, p[3] = left ? 255 : 0;
                }
            }
        }
        // Turned by about 3 degrees around the middle, target pixels to source pixels.
        const double c = std::cos(0.05), s = std::sin(0.05);
        const std::array<double, 9> turn{c, -s, 32 - 32 * c + 32 * s, s, c, 32 - 32 * s - 32 * c, 0, 0, 1};
        const ImageBuffer turned = warp(image, turn, {64, 64});
        ASSERT_FALSE(turned.isNull());
        EXPECT_TRUE(turned.hasAlpha());
        int opaque = 0;
        int clear = 0;
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                int red = 0, green = 0, a = 0;
                if (deep) {
                    const auto* p = reinterpret_cast<const std::uint16_t*>(turned.row(y)) + x * 4;
                    red = p[0], green = p[1], a = p[3];
                } else {
                    const auto* p = reinterpret_cast<const std::uint8_t*>(turned.row(y)) + x * 4;
                    red = p[2], green = p[1], a = p[3];
                }
                opaque += a == max;
                clear += a == 0;
                if (a > max / 50) {
                    EXPECT_EQ(green, 0) << x << ", " << y;
                    EXPECT_NEAR(red, max, max / 100) << x << ", " << y;
                }
            }
        }
        EXPECT_GT(opaque, 0);
        EXPECT_GT(clear, 0);
    }
}

// A picture without alpha is resized as before: its fourth channel is not a weight, whatever
// it holds. Opaque red with zeros there stays red, where taking them for alpha would give black.
TEST(Alpha, ResizingAPictureWithoutAlphaIgnoresTheFourthChannel) {
    const ImageBuffer image = ImageBuffer::create({8, 8}, PixelFormat::Bgrx8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            auto* p = reinterpret_cast<std::uint8_t*>(image.row(y)) + x * 4;
            p[0] = 0, p[1] = 0, p[2] = 255, p[3] = 0;
        }
    }
    const ImageBuffer small = resize(image, {4, 4});
    EXPECT_FALSE(small.hasAlpha());
    EXPECT_EQ(reinterpret_cast<const std::uint8_t*>(small.row(1))[1 * 4 + 2], 255);
}

// Turning a picture makes new pixels, which must have alpha as the old ones had: otherwise a
// turned transparent picture would show the colours stored under its transparency. One
// transparent blue pixel beside an opaque one, turned a quarter, is shown as gray above blue
// or below it, never as two blues.
TEST(Alpha, TurningKeepsItSoATurnedPictureIsShownTheSame) {
    const ImageBuffer image = ImageBuffer::create({2, 1}, PixelFormat::Bgrx8).withAlpha(true);
    const std::array<std::uint8_t, 8> pixels{200, 40, 20, 255, 200, 40, 20, 0};
    std::memcpy(image.row(0), pixels.data(), pixels.size());
    const ImageBuffer turned = applyOrientation(image, Orientation::Rotate90CW);
    ASSERT_EQ(turned.height(), 2);
    EXPECT_TRUE(turned.hasAlpha());
    const ImageBuffer display = toDisplay(turned, fullRange(PixelFormat::Bgrx8));
    const int top = reinterpret_cast<const std::uint8_t*>(display.row(0))[0];
    const int bottom = reinterpret_cast<const std::uint8_t*>(display.row(1))[0];
    EXPECT_EQ(std::min(top, bottom), 128);
    EXPECT_EQ(std::max(top, bottom), 200);
}

// The view asks for 16-bit pixels for pictures deeper than 8 bits. They go on the backdrop
// too: transparent white, 65535, shows the default backdrop, gray 128 (32896 in 16 bits).
TEST(Alpha, DeepPicturesAreShownOnTheBackdropToo) {
    const ImageBuffer deep = ImageBuffer::create({1, 1}, PixelFormat::Rgba16).withAlpha(true);
    const std::array<std::uint16_t, 4> pixel{65535, 65535, 65535, 0};
    std::memcpy(deep.row(0), pixel.data(), sizeof(pixel));
    const ImageBuffer wide = toDisplayWide(deep, fullRange(PixelFormat::Rgba16));
    ASSERT_EQ(wide.format(), PixelFormat::Rgba16);
    EXPECT_EQ(reinterpret_cast<const std::uint16_t*>(wide.row(0))[0], 128 * 257);
    EXPECT_EQ(reinterpret_cast<const std::uint16_t*>(wide.row(0))[3], 65535);
}

// Why: transparency is shown on squares of two colours, and a picture is drawn in tiles, so
// the squares must continue from one tile to the next, and keep their colours however the
// picture is stretched. Input: a fully transparent 16 x 1 picture at 8 bits, and one at 16
// bits whose values use a small part of the range and are shown stretched, on 4-pixel squares
// of black and white, starting at x = 2 as the second tile of a picture would. The truth:
// pixels 0 and 1 finish the first square (black), 2 to 5 are white, 6 to 9 black; the same
// with 16 bits however stretched; a half transparent white pixel is half way to the square.
TEST(Alpha, ShownOnSquaresThatContinueAcrossTiles) {
    Backdrop backdrop;
    backdrop.first = {0, 0, 0, 255};
    backdrop.second = {255, 255, 255, 255};
    backdrop.size = 4;
    backdrop.x = 2;
    const ImageBuffer clear = ImageBuffer::create({16, 1}, PixelFormat::Bgrx8).withAlpha(true);
    std::memset(clear.row(0), 0, 16 * 4);
    reinterpret_cast<std::uint8_t*>(clear.row(0))[3 * 4 + 0] = 255;
    reinterpret_cast<std::uint8_t*>(clear.row(0))[3 * 4 + 1] = 255;
    reinterpret_cast<std::uint8_t*>(clear.row(0))[3 * 4 + 2] = 255;
    reinterpret_cast<std::uint8_t*>(clear.row(0))[6 * 4 + 0] = 255;
    reinterpret_cast<std::uint8_t*>(clear.row(0))[6 * 4 + 1] = 255;
    reinterpret_cast<std::uint8_t*>(clear.row(0))[6 * 4 + 2] = 255;
    reinterpret_cast<std::uint8_t*>(clear.row(0))[6 * 4 + 3] = 128;  // half white over black
    const ImageBuffer shownClear = toDisplay(clear, fullRange(PixelFormat::Bgrx8), nullptr, backdrop);
    const auto grayAt = [&](int x) { return static_cast<int>(reinterpret_cast<const std::uint8_t*>(shownClear.row(0))[x * 4 + 1]); };
    const std::array<int, 10> expected{0, 0, 255, 255, 255, 255, 128, 0, 0, 0};
    for (int x = 0; x < 10; ++x) {
        EXPECT_EQ(grayAt(x), expected[static_cast<std::size_t>(x)]) << "pixel " << x;
    }
    EXPECT_NE(shownClear.data(), clear.data()) << "the picture's own pixels are left alone";

    const ImageBuffer deep = ImageBuffer::create({16, 1}, PixelFormat::Rgba16).withAlpha(true);
    for (int x = 0; x < 16; ++x) {
        auto* p = reinterpret_cast<std::uint16_t*>(deep.row(0)) + x * 4;
        p[0] = p[1] = p[2] = 1000;
        p[3] = 0;
    }
    const ImageBuffer shownDeep = toDisplay(deep, {0, 2000}, nullptr, backdrop);
    EXPECT_EQ(reinterpret_cast<const std::uint8_t*>(shownDeep.row(0))[1], 0);
    EXPECT_EQ(reinterpret_cast<const std::uint8_t*>(shownDeep.row(0))[2 * 4 + 1], 255);
}

// Why: a large picture is shown reduced through overviews, made from its tiles by halving and
// averaging, and these mix neighbours as resizing does. Input: made up, large enough to need
// overviews: the left half opaque red, the right half fully transparent green. The truth: the
// overviews keep alpha, every pixel that shows at all is red without green, the right edge is
// transparent, and it is shown on mid gray as before. Overviews are made by halving, and by
// averaging when the memory budget skips levels, which a picture this small never needs, so
// averaging is checked through shrink, which uses the same code.
TEST(Alpha, OverviewsKeepItAndDoNotMixInTheColourUnderTransparency) {
    const Size size{6000, 16};
    SubimageInfo info;
    info.alpha = true;
    const auto paint = [](const ImageBuffer& image) {
        for (int y = 0; y < image.height(); ++y) {
            auto* p = reinterpret_cast<std::uint8_t*>(image.row(y));
            for (int x = 0; x < image.width(); ++x, p += 4) {
                const bool left = x < image.width() / 2;
                p[0] = 0, p[1] = left ? 0 : 255, p[2] = left ? 255 : 0, p[3] = left ? 255 : 0;
            }
        }
        return image.withAlpha(true);
    };
    const auto source = withOverviews(makeWholeImageSource(info, size, [size, paint](const std::stop_token&) -> Result<ImageBuffer> {
        return paint(ImageBuffer::create(size, PixelFormat::Bgrx8));
    }));
    const auto& levels = source->subimages()[0].levels;
    ASSERT_GT(levels.size(), 1u) << "the test needs a picture with overviews";
    const auto tile = source->readTile({0, static_cast<int>(levels.size()) - 1, 0, 0}, {});
    ASSERT_TRUE(tile.has_value()) << tile.error();

    const auto expectClean = [](const ImageBuffer& reduced) {
        EXPECT_TRUE(reduced.hasAlpha());
        const int y = reduced.height() / 2;
        const auto* row = reinterpret_cast<const std::uint8_t*>(reduced.row(y));
        EXPECT_EQ(row[3], 255);
        EXPECT_EQ(row[(reduced.width() - 1) * 4 + 3], 0);
        for (int x = 0; x < reduced.width(); ++x) {
            const std::uint8_t* p = row + x * 4;
            if (p[3] > 5) {
                EXPECT_EQ(p[1], 0) << "column " << x;
                EXPECT_NEAR(p[2], 255, 2) << "column " << x;
            }
        }
        const ImageBuffer display = toDisplay(reduced, fullRange(PixelFormat::Bgrx8));
        const auto* edge = reinterpret_cast<const std::uint8_t*>(display.row(y)) + (reduced.width() - 1) * 4;
        EXPECT_EQ(edge[1], 128);
    };
    {
        SCOPED_TRACE("halved");
        expectClean(*tile);
    }
    {
        SCOPED_TRACE("averaged");
        expectClean(shrink(paint(ImageBuffer::create({64, 16}, PixelFormat::Bgrx8)), {10, 4}));
    }
}

#ifdef IV_HAVE_PNG

// Until transparency is drawn, what reads pixels for showing them sees the picture on mid
// gray, as before alpha was decoded. Each is checked on the transparent corner of the test
// picture, which has to come out as gray 128 and not as the blue stored under it.
TEST(Alpha, IsStillMidGrayInThumbnailsHistogramsAndTheProbe) {
    PngOptions rgba;
    rgba.colorType = PNG_COLOR_TYPE_RGBA;
    const auto path = write("flat.png", encodePng(rgba));

    const auto thumbnail = makeThumbnail(path, kWidth, {});
    ASSERT_TRUE(thumbnail.has_value()) << thumbnail.error();
    EXPECT_FALSE(thumbnail->hasAlpha());
    const auto* corner = reinterpret_cast<const std::uint8_t*>(thumbnail->row(kHeight - 4)) + (kWidth - 4) * 4;
    EXPECT_EQ(corner[0], 128);
    EXPECT_EQ(corner[2], 128);

    const auto source = open(path);
    ASSERT_TRUE(source);
    const auto histogram = computeHistogram(*source, 0, {});
    ASSERT_TRUE(histogram.has_value());
    // Blue 200 is the right half's opaque part only: a quarter of the picture.
    EXPECT_EQ(histogram->channels[2][200], static_cast<std::uint64_t>(kWidth / 2 * kHeight / 2));

    const auto probe = probePixel(*source, 0, kWidth - 8, kHeight - 8, {});
    ASSERT_TRUE(probe.has_value());
    EXPECT_EQ(probe->eightBit, (std::array<std::uint8_t, 3>{128, 128, 128}));
}

// Alpha of an edited picture at a pixel, from 0 to 255.
int editedAlpha(const ImageBuffer& pixels, int x, int y) {
    return reinterpret_cast<const std::uint8_t*>(pixels.row(y))[x * 4 + 3];
}

// Why: an edited transparent picture must keep its transparency, so that it is saved with it.
// Input: the test picture (its bottom right transparent below four half transparent rows),
// with each edit alone, and then with several, saved as PNG and as JPEG. The truth: every
// edit leaves pixels with alpha, the transparent corner stays transparent, the PNG has alpha
// and its transparent corner, and the JPEG, which has none, gets mid gray.
TEST(Alpha, EditsKeepItAndAnEditedPictureIsSavedWithIt) {
    PngOptions rgba;
    rgba.colorType = PNG_COLOR_TYPE_RGBA;
    const auto source = open(write("edit.png", encodePng(rgba)));
    ASSERT_TRUE(source);

    ColorEdit adjust;
    adjust.chroma = 0.5;
    const std::pair<const char*, Edit> edits[] = {
        {"crop", CropEdit{{kWidth / 4, kHeight / 4, kWidth * 3 / 4, kHeight * 3 / 4}}},
        {"resize", ResizeEdit{{kWidth / 2, kHeight / 2}}},
        {"straighten", TiltEdit{{.roll = 0.02}, 50, true}},
        {"grayscale", GrayEdit{}},
        {"adjust", adjust},
    };
    for (const auto& [name, edit] : edits) {
        SCOPED_TRACE(name);
        Document document(source, 0);
        document.apply(edit);
        const auto edited = document.source();
        EXPECT_TRUE(edited->subimages()[0].alpha);
        const auto pixels = readLevel(*edited, 0, 0, {});
        ASSERT_TRUE(pixels.has_value()) << pixels.error();
        ASSERT_TRUE(pixels->hasAlpha());
        EXPECT_EQ(editedAlpha(*pixels, pixels->width() - 3, pixels->height() - 3), 0);
        EXPECT_EQ(editedAlpha(*pixels, 2, 2), 255);
    }

    Document document(source, 0);
    document.apply(CropEdit{{kWidth / 2, 0, kWidth / 2, kHeight}});
    document.apply(ResizeEdit{{kWidth / 4, kHeight / 2}});
    document.apply(adjust);
    const auto edited = document.source();

    const auto png = testFolder() / "edited.png";
    ASSERT_TRUE(saveSubimage(*edited, 0, png, PngWriteOptions{}, {}, true).has_value());
    const auto reopened = open(png);
    ASSERT_TRUE(reopened);
    EXPECT_TRUE(reopened->subimages()[0].alpha);
    EXPECT_EQ(decoded(*reopened, kWidth / 4 - 2, kHeight / 2 - 2)[3], 0);
    EXPECT_EQ(decoded(*reopened, 2, 2)[3], 255);

    const auto jpeg = testFolder() / "edited.jpg";
    ASSERT_TRUE(saveSubimage(*edited, 0, jpeg, JpegWriteOptions{.quality = 100}, {}, true).has_value());
    const auto flat = open(jpeg);
    ASSERT_TRUE(flat);
    expectNoAlpha(*flat);
    expectNear(shown(*flat, kWidth / 4 - 3, kHeight / 2 - 3), {128, 128, 128}, 3);
}

#endif  // IV_HAVE_PNG

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
