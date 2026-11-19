#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <vector>

#include "core/image_source.h"
#include "test_formats.h"
#include "test_lzw.h"

// BMP, Netpbm, TGA and GIF, which the viewer reads but does not write. Each test writes a file
// of one kind byte by byte, from the formats' specifications, holding the shared test picture
// (left half red, right half blue, the bottom right transparent where the kind has alpha).

using namespace iv;
using namespace iv::test::formats;

namespace {

void put16(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void put32(Bytes& b, std::uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}

// These formats are often stored bottom-up or mirrored, which the shared picture cannot show
// when opaque: its top and bottom look alike. So the first pixels of its top row are green.
constexpr int kMarkWidth = 8;
constexpr std::array<std::uint8_t, 4> kMark{20, 200, 20, 255};

std::array<std::uint8_t, 4> rgbaAt(int x, int y) {
    std::array<std::uint8_t, 4> rgba{};
    pixelAt(x, y, rgba);
    return y == 0 && x < kMarkWidth ? kMark : rgba;
}

// Index 0 red, 1 blue, 2 the green mark, as a palette of three holds the picture; a palette of
// two (one bit) leaves the mark out.
int indexAt(int x, int y, int bits = 8) {
    if (bits > 1 && y == 0 && x < kMarkWidth) {
        return 2;
    }
    return x < kWidth / 2 ? 0 : 1;
}

// Upright and not mirrored: the mark top left, not bottom left or top right.
void expectUpright(ImageSource& source) {
    expectNear(shown(source, 2, 0), {kMark[0], kMark[1], kMark[2]}, 5);
    expectNear(shown(source, 2, kHeight - 1), {200, 40, 20}, 5);
    expectNear(shown(source, kWidth - 3, 0), {20, 40, 200}, 5);
}

// ---- BMP ----

struct BmpOptions {
    int bits = 24;
    std::uint32_t compression = 0;  // 0 none, 1 RLE8, 2 RLE4, 3 bit fields, 4 JPEG
    int headerSize = 40;            // 12 is OS/2 1.x
    bool topDown = false;
    std::array<std::uint32_t, 4> masks{};  // red, green, blue, alpha, for bit fields
    bool grayRamp = false;                 // an 8-bit palette of 256 grays, the value x * 4
    bool omitPalette = false;              // no palette at all, the pixels right after the header
    bool withAlpha = true;                 // for 32 bits: the picture's alpha, or all 0
    Bytes icc;                             // embedded, in a 124-byte header
};

std::uint32_t packed(const std::array<std::uint8_t, 4>& rgba, const std::array<std::uint32_t, 4>& masks) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        if (masks[i] == 0) {
            continue;
        }
        const int shift = std::countr_zero(masks[i]);
        const std::uint32_t top = masks[i] >> shift;
        value |= ((rgba[i] * top + 127) / 255) << shift;
    }
    return value;
}

Bytes encodeBmp(const BmpOptions& o) {
    const bool paletted = o.bits <= 8;
    std::vector<std::array<std::uint8_t, 3>> palette;  // red, green, blue
    if (o.grayRamp) {
        for (int i = 0; i < 256; ++i) {
            palette.push_back({static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i)});
        }
    } else if (paletted) {
        palette = {{200, 40, 20}, {20, 40, 200}};
        if (o.bits > 1) {
            palette.push_back({kMark[0], kMark[1], kMark[2]});
        }
    }
    if (o.omitPalette) {
        palette.clear();
    }
    const bool core = o.headerSize == 12;
    const std::size_t entryBytes = core ? 3 : 4;
    const bool masksAfter = o.compression == 3 && o.headerSize == 40;

    Bytes pixels;
    const auto storedY = [&](int s) { return o.topDown ? s : kHeight - 1 - s; };
    if (o.compression == 1 || o.compression == 2) {
        for (int s = 0; s < kHeight; ++s) {
            const auto run = [&](int count, int index) {
                pixels.push_back(static_cast<std::uint8_t>(count));
                pixels.push_back(static_cast<std::uint8_t>(o.compression == 2 ? index << 4 | index : index));
            };
            if (storedY(s) == 0) {
                run(kMarkWidth, 2);
                run(kWidth / 2 - kMarkWidth, 0);
            } else {
                run(kWidth / 2, 0);
            }
            run(kWidth / 2, 1);
            pixels.push_back(0);
            pixels.push_back(0);
        }
        pixels.push_back(0);
        pixels.push_back(1);
    } else {
        const std::size_t stride = (static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(o.bits) + 31) / 32 * 4;
        for (int s = 0; s < kHeight; ++s) {
            const int y = storedY(s);
            Bytes row(stride, 0);
            for (int x = 0; x < kWidth; ++x) {
                auto rgba = rgbaAt(x, y);
                if (!o.withAlpha) {
                    rgba[3] = 0;
                }
                if (paletted) {
                    const int index = o.grayRamp ? x * 4 : indexAt(x, y, o.bits);
                    const int perByte = 8 / o.bits;
                    row[static_cast<std::size_t>(x / perByte)] |= static_cast<std::uint8_t>(index << (8 - o.bits * (x % perByte + 1)));
                } else if (o.bits == 24) {
                    for (std::size_t c = 0; c < 3; ++c) {
                        row[static_cast<std::size_t>(x) * 3 + c] = rgba[2 - c];
                    }
                } else {
                    std::array<std::uint32_t, 4> masks = o.masks;
                    if (o.compression == 0) {
                        masks = o.bits == 16 ? std::array<std::uint32_t, 4>{0x7C00, 0x03E0, 0x001F, 0}
                                             : std::array<std::uint32_t, 4>{0xFF0000, 0xFF00, 0xFF, 0xFF000000};
                    }
                    const std::uint32_t value = packed(rgba, masks);
                    for (int b = 0; b < o.bits / 8; ++b) {
                        row[static_cast<std::size_t>(x * (o.bits / 8) + b)] = static_cast<std::uint8_t>(value >> (8 * b));
                    }
                }
            }
            pixels.insert(pixels.end(), row.begin(), row.end());
        }
    }

    const std::size_t paletteBytes = palette.size() * entryBytes;
    const std::size_t offset = 14 + static_cast<std::size_t>(o.headerSize) + (masksAfter ? 12 : 0) + paletteBytes;
    Bytes b{'B', 'M'};
    put32(b, static_cast<std::uint32_t>(offset + pixels.size() + o.icc.size()));
    put32(b, 0);
    put32(b, static_cast<std::uint32_t>(offset));
    put32(b, static_cast<std::uint32_t>(o.headerSize));
    if (core) {
        put16(b, kWidth);
        put16(b, kHeight);
        put16(b, 1);
        put16(b, static_cast<std::uint32_t>(o.bits));
    } else {
        put32(b, kWidth);
        put32(b, static_cast<std::uint32_t>(o.topDown ? -kHeight : kHeight));
        put16(b, 1);
        put16(b, static_cast<std::uint32_t>(o.bits));
        put32(b, o.compression);
        put32(b, static_cast<std::uint32_t>(pixels.size()));
        put32(b, 2835);
        put32(b, 2835);
        put32(b, static_cast<std::uint32_t>(palette.size()));
        put32(b, 0);
        if (o.headerSize >= 52) {
            for (std::size_t i = 0; i < (o.headerSize >= 56 ? 4U : 3U); ++i) {
                put32(b, o.masks[i]);
            }
        }
        if (o.headerSize >= 108) {
            put32(b, o.icc.empty() ? 0x73524742 : 0x4D424544);  // 'sRGB' or 'MBED'
            b.resize(14 + 108, 0);                              // end points and gamma
        }
        if (o.headerSize >= 124) {
            put32(b, 4);  // intent
            put32(b, static_cast<std::uint32_t>(offset - 14 + pixels.size()));
            put32(b, static_cast<std::uint32_t>(o.icc.size()));
            put32(b, 0);
        }
        if (masksAfter) {
            for (std::size_t i = 0; i < 3; ++i) {
                put32(b, o.masks[i]);
            }
        }
    }
    for (const auto& c : palette) {
        b.push_back(c[2]);
        b.push_back(c[1]);
        b.push_back(c[0]);
        if (!core) {
            b.push_back(0);
        }
    }
    EXPECT_EQ(b.size(), offset);
    b.insert(b.end(), pixels.begin(), pixels.end());
    b.insert(b.end(), o.icc.begin(), o.icc.end());
    return b;
}

std::shared_ptr<ImageSource> openBmp(const BmpOptions& options, const std::string& name = "x.bmp") {
    return open(write(name, encodeBmp(options)));
}

}  // namespace

// Why: 24 bits is the commonest BMP, stored bottom-up with rows padded to four bytes, in BGR.
// Input: the test picture so, and once top-down (a negative height). The truth: the picture,
// not flipped, swapped or shifted, without alpha.
TEST(Bmp, TwentyFourBitsEitherWayUp) {
    for (const bool topDown : {false, true}) {
        BmpOptions options;
        options.topDown = topDown;
        const auto source = openBmp(options);
        ASSERT_TRUE(source);
        expectPicture(*source, 0);
        expectUpright(*source);
        expectNoAlpha(*source);
        EXPECT_EQ(source->subimages()[0].stored.container, "BMP");
    }
}

// Why: palette pictures come in 1, 4 and 8 bits, packed from the high bits, plain or RLE, and
// OS/2 1.x stores its palette in three-byte entries. Input: the picture as a two-colour palette
// in each. The truth: the picture, in colour.
TEST(Bmp, PalettesAtEveryDepthPlainAndRle) {
    struct Kind {
        int bits;
        std::uint32_t compression;
        int headerSize;
    };
    for (const Kind kind : {Kind{1, 0, 40}, Kind{4, 0, 40}, Kind{8, 0, 40}, Kind{8, 1, 40}, Kind{4, 2, 40}, Kind{8, 0, 12}}) {
        BmpOptions options;
        options.bits = kind.bits;
        options.compression = kind.compression;
        options.headerSize = kind.headerSize;
        const auto source = openBmp(options);
        ASSERT_TRUE(source) << kind.bits << " " << kind.compression;
        expectPicture(*source, 0);
        if (kind.bits > 1) {
            expectUpright(*source);
        }
        EXPECT_EQ(source->subimages()[0].format, PixelFormat::Bgrx8);
    }
}

// Why: scientific and scanned pictures are often 8-bit BMPs with a palette of grays, which are
// gray pictures and should be treated as such. Input: a ramp, each column x * 4, in a palette of
// 256 grays. The truth: an 8-bit gray picture with those values.
TEST(Bmp, AGrayPaletteIsAGrayPicture) {
    BmpOptions options;
    options.bits = 8;
    options.grayRamp = true;
    const auto source = openBmp(options);
    ASSERT_TRUE(source);
    EXPECT_EQ(source->subimages()[0].format, PixelFormat::Gray8);
    const auto tile = source->readTile({0, 0, 0, 0}, {});
    ASSERT_TRUE(tile);
    EXPECT_EQ(reinterpret_cast<const std::uint8_t*>(tile->row(5))[10], 40);
}

// Why: some writers of 8-bit gray BMPs leave the palette out, meaning a ramp of grays, and the
// palette must never be read out of the pixels that follow it. Input: the ramp of x * 4 with no
// palette, the pixels right after the header. The truth: a gray picture with those values, as
// with the palette.
TEST(Bmp, AMissingPaletteMeansGrays) {
    BmpOptions options;
    options.bits = 8;
    options.grayRamp = true;
    options.omitPalette = true;
    const auto source = openBmp(options);
    ASSERT_TRUE(source);
    EXPECT_EQ(source->subimages()[0].format, PixelFormat::Gray8);
    const auto tile = source->readTile({0, 0, 0, 0}, {});
    ASSERT_TRUE(tile);
    const auto* row = reinterpret_cast<const std::uint8_t*>(tile->row(5));
    EXPECT_EQ(row[0], 0);
    EXPECT_EQ(row[10], 40);
    EXPECT_EQ(row[63], 252);
}

// Why: 16-bit BMPs are 5-5-5 without bit fields and usually 5-6-5 with them, and both must be
// widened to 8 bits. Input: the picture in each. The truth: the picture, within what five bits
// can hold.
TEST(Bmp, SixteenBitsWithAndWithoutBitFields) {
    BmpOptions plain;
    plain.bits = 16;
    const auto a = openBmp(plain);
    ASSERT_TRUE(a);
    expectPicture(*a, 5);
    BmpOptions fields;
    fields.bits = 16;
    fields.compression = 3;
    fields.masks = {0xF800, 0x07E0, 0x001F, 0};
    const auto b = openBmp(fields);
    ASSERT_TRUE(b);
    expectPicture(*b, 5);
    expectUpright(*b);
    expectNoAlpha(*b);
}

// Why: 32-bit BMPs carry alpha in bit fields of a later header, or just in the fourth byte, and
// many leave that byte 0, meaning opaque, not invisible. Input: alpha in a V5 header's bit
// fields, alpha in the fourth byte of a plain one, and the fourth byte all 0. The truth: alpha
// kept in the first two, an opaque picture in the third.
TEST(Bmp, AlphaFromBitFieldsOrTheFourthByteButNotWhenAllZero) {
    BmpOptions v5;
    v5.bits = 32;
    v5.compression = 3;
    v5.headerSize = 124;
    v5.masks = {0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000};
    const auto a = openBmp(v5);
    ASSERT_TRUE(a);
    expectAlphaKept(*a, 0);

    BmpOptions plain;
    plain.bits = 32;
    const auto b = openBmp(plain);
    ASSERT_TRUE(b);
    expectAlphaKept(*b, 0);

    plain.withAlpha = false;
    const auto c = openBmp(plain);
    ASSERT_TRUE(c);
    expectNoAlpha(*c);
    expectPicture(*c, 0);
    expectUpright(*c);
}

// Why: bit fields wider than eight bits (10-10-10-2) hold more than 8-bit colour, which the
// 16-bit pipeline keeps. Input: the picture in 10-bit fields with 2 bits of alpha. The truth:
// a 16-bit picture, the colours right.
TEST(Bmp, WideBitFieldsKeepTheirPrecision) {
    BmpOptions options;
    options.bits = 32;
    options.compression = 3;
    options.headerSize = 56;
    options.masks = {0x3FF00000, 0x000FFC00, 0x000003FF, 0};
    const auto source = openBmp(options);
    ASSERT_TRUE(source);
    EXPECT_EQ(source->subimages()[0].format, PixelFormat::Rgba16);
    expectPicture(*source, 1);
}

// Why: a V5 header may embed the picture's colour profile, which has to be used like any other
// format's. Input: a V5 BMP with some profile bytes after the pixels. The truth: the same bytes.
TEST(Bmp, ReadsAnEmbeddedProfile) {
    BmpOptions options;
    options.bits = 32;
    options.compression = 3;
    options.headerSize = 124;
    options.masks = {0x00FF0000, 0x0000FF00, 0x000000FF, 0};
    options.icc = Bytes{1, 2, 3, 4, 5, 6, 7, 8};
    const auto source = openBmp(options);
    ASSERT_TRUE(source);
    EXPECT_EQ(source->subimages()[0].iccProfile, options.icc);
}

// Why: a BMP holding a JPEG or PNG is a printer's format nobody else reads; it is refused with a
// reason rather than shown as noise. Input: a header saying JPEG. The truth: an error naming it.
TEST(Bmp, RefusesAJpegInside) {
    BmpOptions options;
    options.compression = 4;
    const auto source = openImageSource(write("jpeg.bmp", encodeBmp(options)));
    ASSERT_FALSE(source);
    EXPECT_NE(source.error().find("JPEG"), std::string::npos);
}

// Why: BMP headers are a classic way to crash a viewer: sizes that overflow, palettes and RLE that
// run past the end. Input: damaged and cut copies of several kinds. The truth: no crash or hang.
TEST(Bmp, SurvivesDamage) {
    survivesDamage("x.bmp", encodeBmp({}));
    BmpOptions rle;
    rle.bits = 8;
    rle.compression = 1;
    survivesDamage("rle.bmp", encodeBmp(rle));
    BmpOptions v5;
    v5.bits = 32;
    v5.compression = 3;
    v5.headerSize = 124;
    v5.masks = {0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000};
    survivesDamage("v5.bmp", encodeBmp(v5));
}

// ---- Netpbm ----

namespace {

Bytes text(const std::string& s) { return Bytes(s.begin(), s.end()); }

void append(Bytes& b, const std::string& s) { b.insert(b.end(), s.begin(), s.end()); }

// The test picture as P3 (text) or P6 (binary) at a maximum value of 255, without alpha.
Bytes encodePpm(bool asText) {
    Bytes b = text(std::string(asText ? "P3" : "P6") + "\n# made by a test\n" + std::to_string(kWidth) + " " +
                   std::to_string(kHeight) + "\n255\n");
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const auto rgba = rgbaAt(x, y);
            for (std::size_t c = 0; c < 3; ++c) {
                if (asText) {
                    append(b, std::to_string(rgba[c]) + (x % 8 == 7 ? "\n" : " "));
                } else {
                    b.push_back(rgba[c]);
                }
            }
        }
    }
    return b;
}

// The test picture as PAM, colour with alpha, 8 or 16 bits.
Bytes encodePam(bool sixteen) {
    const int maxval = sixteen ? 65535 : 255;
    Bytes b = text("P7\nWIDTH " + std::to_string(kWidth) + "\nHEIGHT " + std::to_string(kHeight) +
                   "\nDEPTH 4\nMAXVAL " + std::to_string(maxval) + "\nTUPLTYPE RGB_ALPHA\nENDHDR\n");
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            for (const std::uint8_t v : rgbaAt(x, y)) {
                if (sixteen) {
                    const int w = v * 257;
                    b.push_back(static_cast<std::uint8_t>(w >> 8));
                    b.push_back(static_cast<std::uint8_t>(w));
                } else {
                    b.push_back(v);
                }
            }
        }
    }
    return b;
}

// Floats, rows bottom-up: each pixel (x / width, 2.5, -1) in colour, or x / width in gray.
Bytes encodePfm(bool colour, bool littleEndian) {
    Bytes b = text(std::string(colour ? "PF" : "Pf") + "\n" + std::to_string(kWidth) + " " + std::to_string(kHeight) +
                   "\n" + (littleEndian ? "-1.0" : "1.0") + "\n");
    const auto put = [&](float f) {
        auto raw = std::bit_cast<std::uint32_t>(f);
        for (int i = 0; i < 4; ++i) {
            const int byte = littleEndian ? i : 3 - i;
            b.push_back(static_cast<std::uint8_t>(raw >> (8 * byte)));
        }
    };
    for (int stored = 0; stored < kHeight; ++stored) {
        const int y = kHeight - 1 - stored;
        for (int x = 0; x < kWidth; ++x) {
            put(static_cast<float>(x) / kWidth + (y == 0 ? 0.5F : 0.0F));
            if (colour) {
                put(2.5F);
                put(-1.0F);
            }
        }
    }
    return b;
}

std::uint16_t word(const ImageBuffer& image, int x, int y, int channel = 0, int channels = 1) {
    return reinterpret_cast<const std::uint16_t*>(image.row(y))[x * channels + channel];
}

}  // namespace

// Why: PPM comes as numbers in text or as bytes, and text may have comments and break lines
// anywhere. Input: the test picture as both. The truth: the picture, without alpha.
TEST(Netpbm, PpmAsTextAndBinary) {
    for (const bool asText : {true, false}) {
        const auto source = open(write(asText ? "t.ppm" : "b.ppm", encodePpm(asText)));
        ASSERT_TRUE(source);
        expectPicture(*source, 0);
        expectUpright(*source);
        expectNoAlpha(*source);
        EXPECT_EQ(source->subimages()[0].stored.container, "PPM");
    }
}

// Why: PBM is 1 for black, packed eight to a byte in binary, and in text its digits need no
// space between them. Input: a 10 x 2 picture, black on the left four columns, as P1 written
// without spaces and as P4. The truth: an 8-bit gray picture, 0 on the left and 255 on the right.
TEST(Netpbm, PbmOneIsBlackInTextAndPacked) {
    const Bytes p1 = text("P1\n10 2\n1111000000\n111100 0000\n");
    Bytes packed = text("P4\n10 2\n");
    for (int row = 0; row < 2; ++row) {
        packed.push_back(0xF0);
        packed.push_back(0x00);
    }
    for (const auto& [name, bytes] : {std::pair{std::string("t.pbm"), p1}, std::pair{std::string("b.pbm"), packed}}) {
        const auto source = open(write(name, bytes));
        ASSERT_TRUE(source) << name;
        EXPECT_EQ(source->subimages()[0].format, PixelFormat::Gray8);
        const auto tile = source->readTile({0, 0, 0, 0}, {});
        ASSERT_TRUE(tile);
        const auto* row = reinterpret_cast<const std::uint8_t*>(tile->row(1));
        EXPECT_EQ(row[3], 0) << name;
        EXPECT_EQ(row[4], 255) << name;
        EXPECT_EQ(row[9], 255) << name;
    }
}

// Why: PGM and PPM go up to 16 bits, as big-endian words, and any maximum value: 1023 for 10-bit
// cameras, 100 in hand-made files. Input: gray at 65535, 1023 and 100, text and binary. The
// truth: 16-bit gray where the maximum is above 255, values scaled to the full range.
TEST(Netpbm, AnyMaximumValueUpToSixteenBits) {
    Bytes deep = text("P5\n2 1\n65535\n");
    deep.insert(deep.end(), {0x12, 0x34, 0xFF, 0xFF});
    const auto a = open(write("deep.pgm", deep));
    ASSERT_TRUE(a);
    EXPECT_EQ(a->subimages()[0].format, PixelFormat::Gray16);
    auto tile = a->readTile({0, 0, 0, 0}, {});
    ASSERT_TRUE(tile);
    EXPECT_EQ(word(*tile, 0, 0), 0x1234);
    EXPECT_EQ(word(*tile, 1, 0), 0xFFFF);

    const auto b = open(write("ten.pgm", text("P2 2 1 1023 1023 0\n")));
    ASSERT_TRUE(b);
    tile = b->readTile({0, 0, 0, 0}, {});
    ASSERT_TRUE(tile);
    EXPECT_EQ(word(*tile, 0, 0), 65535);
    EXPECT_EQ(word(*tile, 1, 0), 0);

    const auto c = open(write("hundred.pgm", text("P2 1 1 100 50\n")));
    ASSERT_TRUE(c);
    EXPECT_EQ(c->subimages()[0].format, PixelFormat::Gray8);
    tile = c->readTile({0, 0, 0, 0}, {});
    ASSERT_TRUE(tile);
    EXPECT_EQ(reinterpret_cast<const std::uint8_t*>(tile->row(0))[0], 128) << "50 of 100, rounded";
}

// Why: PAM carries alpha, in 8 or 16 bits, declared by name in its header. Input: the test
// picture as RGB_ALPHA at both depths. The truth: the picture with its alpha kept.
TEST(Netpbm, PamKeepsAlphaAtBothDepths) {
    for (const bool sixteen : {false, true}) {
        const auto source = open(write(sixteen ? "16.pam" : "8.pam", encodePam(sixteen)));
        ASSERT_TRUE(source);
        EXPECT_EQ(source->subimages()[0].format, sixteen ? PixelFormat::Rgba16 : PixelFormat::Bgrx8);
        expectAlphaKept(*source, 0);
        expectPicture(*source, 0);
    }
}

// Why: PFM holds floats beyond 0..1 in linear light, bottom-up, in the byte order the sign of its
// scale gives. Input: colour little-endian and gray big-endian, the top row brighter by 0.5. The
// truth: float pictures with the values as written, the right way up, with a linear profile.
TEST(Netpbm, PfmFloatsEitherByteOrderBottomUp) {
    const auto colour = open(write("c.pfm", encodePfm(true, true)));
    ASSERT_TRUE(colour);
    const SubimageInfo& info = colour->subimages()[0];
    EXPECT_EQ(info.format, PixelFormat::Rgba32F);
    EXPECT_TRUE(info.stored.floatSamples);
    EXPECT_FALSE(info.iccProfile.empty()) << "linear light, not sRGB";
    auto tile = colour->readTile({0, 0, 0, 0}, {});
    ASSERT_TRUE(tile);
    const auto* top = reinterpret_cast<const float*>(tile->row(0));
    EXPECT_FLOAT_EQ(top[16 * 4], 16.0F / kWidth + 0.5F) << "the top row is the last stored";
    EXPECT_FLOAT_EQ(top[16 * 4 + 1], 2.5F);
    EXPECT_FLOAT_EQ(top[16 * 4 + 2], -1.0F);

    const auto gray = open(write("g.pfm", encodePfm(false, false)));
    ASSERT_TRUE(gray);
    EXPECT_EQ(gray->subimages()[0].format, PixelFormat::Gray32F);
    tile = gray->readTile({0, 0, 0, 0}, {});
    ASSERT_TRUE(tile);
    EXPECT_FLOAT_EQ(reinterpret_cast<const float*>(tile->row(5))[32], 0.5F);
}

// Why: hand-written headers are easy to damage, and a parser of text must not run past the end.
// Input: damaged and cut copies of text, binary, PAM and PFM files, and a PAM header that never
// ends. The truth: no crash or hang, and the endless header refused.
TEST(Netpbm, SurvivesDamage) {
    survivesDamage("t.ppm", encodePpm(true));
    survivesDamage("b.ppm", encodePpm(false));
    survivesDamage("x.pam", encodePam(true));
    survivesDamage("x.pfm", encodePfm(true, true));
    EXPECT_FALSE(openImageSource(write("endless.pam", text("P7\nWIDTH 2\nHEIGHT 2\n"))));
}

// ---- TGA ----

namespace {

struct TgaOptions {
    int type = 2;        // 1 mapped, 2 true colour, 3 gray; 9, 10, 11 the same run-length packed
    int bits = 24;       // per stored pixel
    int alphaBits = 0;   // the descriptor's count
    bool topDown = false;
    bool rightToLeft = false;
    int attributes = -1;  // the v2 footer's alpha type; -1 writes no footer
    bool premultiply = false;
    bool zeroAlpha = false;  // alpha bits declared but every one 0
};

// Pixels in stored order: whole pixels, run-length packed across rows when the type says so.
Bytes packTga(const std::vector<Bytes>& pixels, bool rle) {
    Bytes out;
    if (!rle) {
        for (const auto& p : pixels) {
            out.insert(out.end(), p.begin(), p.end());
        }
        return out;
    }
    std::size_t i = 0;
    while (i < pixels.size()) {
        std::size_t run = 1;
        while (i + run < pixels.size() && run < 128 && pixels[i + run] == pixels[i]) {
            ++run;
        }
        if (run > 1) {
            out.push_back(static_cast<std::uint8_t>(0x80 | (run - 1)));
            out.insert(out.end(), pixels[i].begin(), pixels[i].end());
            i += run;
            continue;
        }
        std::size_t raw = 1;
        while (i + raw < pixels.size() && raw < 128 && pixels[i + raw] != pixels[i + raw - 1]) {
            ++raw;
        }
        out.push_back(static_cast<std::uint8_t>(raw - 1));
        for (std::size_t k = 0; k < raw; ++k) {
            out.insert(out.end(), pixels[i + k].begin(), pixels[i + k].end());
        }
        i += raw;
    }
    return out;
}

Bytes encodeTga(const TgaOptions& o) {
    const int base = o.type > 8 ? o.type - 8 : o.type;
    const std::vector<std::array<std::uint8_t, 4>> palette{{200, 40, 20, 255}, {20, 40, 200, 255}, kMark};
    std::vector<Bytes> pixels;
    for (int s = 0; s < kHeight; ++s) {
        const int y = o.topDown ? s : kHeight - 1 - s;
        for (int sx = 0; sx < kWidth; ++sx) {
            const int x = o.rightToLeft ? kWidth - 1 - sx : sx;
            auto c = rgbaAt(x, y);
            if (o.zeroAlpha) {
                c[3] = 0;
            } else if (o.premultiply) {
                for (std::size_t i = 0; i < 3; ++i) {
                    c[i] = static_cast<std::uint8_t>((c[i] * c[3] + 127) / 255);
                }
            }
            if (base == 1) {
                pixels.push_back({static_cast<std::uint8_t>(indexAt(x, y))});
            } else if (base == 3) {
                pixels.push_back(o.bits == 16 ? Bytes{c[0], c[3]} : Bytes{c[0]});
            } else if (o.bits == 16) {
                const auto five = [](int v) { return (v * 31 + 127) / 255; };
                const int v = five(c[0]) << 10 | five(c[1]) << 5 | five(c[2]) | (c[3] >= 128 ? 0x8000 : 0);
                pixels.push_back({static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8)});
            } else if (o.bits == 24) {
                pixels.push_back({c[2], c[1], c[0]});
            } else {
                pixels.push_back({c[2], c[1], c[0], c[3]});
            }
        }
    }
    Bytes b;
    const std::string id = "test";
    b.push_back(static_cast<std::uint8_t>(id.size()));
    b.push_back(base == 1 ? 1 : 0);
    b.push_back(static_cast<std::uint8_t>(o.type));
    put16(b, 0);
    put16(b, base == 1 ? static_cast<std::uint32_t>(palette.size()) : 0);
    b.push_back(base == 1 ? 24 : 0);
    put16(b, 0);
    put16(b, 0);
    put16(b, kWidth);
    put16(b, kHeight);
    b.push_back(static_cast<std::uint8_t>(o.bits));
    b.push_back(static_cast<std::uint8_t>(o.alphaBits | (o.rightToLeft ? 0x10 : 0) | (o.topDown ? 0x20 : 0)));
    append(b, id);
    if (base == 1) {
        for (const auto& c : palette) {
            b.insert(b.end(), {c[2], c[1], c[0]});
        }
    }
    const Bytes packed = packTga(pixels, o.type > 8);
    b.insert(b.end(), packed.begin(), packed.end());
    if (o.attributes >= 0) {
        const auto extension = static_cast<std::uint32_t>(b.size());
        Bytes area(495, 0);
        area[0] = 495 & 0xFF;
        area[1] = 495 >> 8;
        area[494] = static_cast<std::uint8_t>(o.attributes);
        b.insert(b.end(), area.begin(), area.end());
        put32(b, extension);
        put32(b, 0);
        append(b, std::string("TRUEVISION-XFILE.") + '\0');
    }
    return b;
}

std::shared_ptr<ImageSource> openTga(const TgaOptions& options, const std::string& name = "x.tga") {
    return open(write(name, encodeTga(options)));
}

}  // namespace

// Why: TGA starts from any corner, bottom left by default, and many tools write it top-down or,
// rarely, mirrored. Input: 24-bit true colour from each corner. The truth: the picture upright.
TEST(Tga, TrueColourFromEveryCorner) {
    for (const bool topDown : {false, true}) {
        for (const bool rightToLeft : {false, true}) {
            TgaOptions options;
            options.topDown = topDown;
            options.rightToLeft = rightToLeft;
            const auto source = openTga(options);
            ASSERT_TRUE(source) << topDown << rightToLeft;
            expectPicture(*source, 0);
            expectUpright(*source);
            expectNoAlpha(*source);
            EXPECT_EQ(source->subimages()[0].stored.container, "TGA");
        }
    }
}

// Why: every kind of TGA comes plain or run-length packed, and its packets run across rows.
// Input: mapped, true colour (16, 24, 32 bits) and gray, each plain and packed. The truth: the
// picture in each, gray as an 8-bit gray picture holding the red values.
TEST(Tga, EveryTypePlainAndPacked) {
    for (const bool rle : {false, true}) {
        for (const int bits : {16, 24, 32}) {
            TgaOptions options;
            options.type = rle ? 10 : 2;
            options.bits = bits;
            const auto source = openTga(options);
            ASSERT_TRUE(source) << bits << rle;
            expectPicture(*source, bits == 16 ? 5 : 0);
            expectUpright(*source);
        }
        TgaOptions mapped;
        mapped.type = rle ? 9 : 1;
        mapped.bits = 8;
        const auto m = openTga(mapped);
        ASSERT_TRUE(m) << rle;
        expectPicture(*m, 0);
        expectUpright(*m);

        TgaOptions gray;
        gray.type = rle ? 11 : 3;
        gray.bits = 8;
        const auto g = openTga(gray);
        ASSERT_TRUE(g) << rle;
        EXPECT_EQ(g->subimages()[0].format, PixelFormat::Gray8);
        const auto tile = g->readTile({0, 0, 0, 0}, {});
        ASSERT_TRUE(tile);
        EXPECT_EQ(reinterpret_cast<const std::uint8_t*>(tile->row(0))[2], kMark[0]);
        EXPECT_EQ(reinterpret_cast<const std::uint8_t*>(tile->row(kHeight - 1))[2], 200);
    }
}

// Why: alpha in TGA is declared by the descriptor's bit count, but the v2 footer may say it means
// nothing or is premultiplied, and many files declare it and leave it 0. Input: 32 bits with
// alpha bits and no footer, a footer saying straight, one saying none, one saying premultiplied,
// and alpha bits all 0. The truth: alpha kept in the first two and the fourth (as straight
// alpha), and an opaque picture in the third and fifth.
TEST(Tga, AlphaAsTheFooterSaysAndNotWhenAllZero) {
    TgaOptions options;
    options.bits = 32;
    options.alphaBits = 8;
    for (const int attributes : {-1, 3}) {
        options.attributes = attributes;
        const auto source = openTga(options);
        ASSERT_TRUE(source) << attributes;
        expectAlphaKept(*source, 0);
    }
    options.attributes = 0;
    const auto none = openTga(options);
    ASSERT_TRUE(none);
    expectNoAlpha(*none);

    options.attributes = 4;
    options.premultiply = true;
    const auto premultiplied = openTga(options);
    ASSERT_TRUE(premultiplied);
    expectAlphaKept(*premultiplied, 2);

    TgaOptions zero;
    zero.bits = 32;
    zero.alphaBits = 8;
    zero.zeroAlpha = true;
    const auto opaque = openTga(zero);
    ASSERT_TRUE(opaque);
    expectNoAlpha(*opaque);
    expectPicture(*opaque, 0);
}

// Why: with no magic number, TGA is tried only for files so named, and only when the header is
// one a TGA can have, so other files are not shown as noise. Input: a good TGA named .dat, and
// a .tga holding text. The truth: neither opens as a TGA.
TEST(Tga, OnlyFilesNamedSoWithAPlausibleHeader) {
    EXPECT_FALSE(openImageSource(write("x.dat", encodeTga({}))));
    EXPECT_FALSE(openImageSource(write("text.tga", text("Hello, this is not a picture at all.\n"))));
    EXPECT_TRUE(openImageSource(write("UPPER.TGA", encodeTga({}))));
}

// Why: packets and colour maps that run past the end are how TGA readers crash. Input: damaged
// and cut copies of packed, mapped and footed files. The truth: no crash or hang.
TEST(Tga, SurvivesDamage) {
    TgaOptions packed;
    packed.type = 10;
    packed.bits = 32;
    packed.alphaBits = 8;
    packed.attributes = 3;
    survivesDamage("p.tga", encodeTga(packed));
    TgaOptions mapped;
    mapped.type = 9;
    mapped.bits = 8;
    survivesDamage("m.tga", encodeTga(mapped));
}

// ---- GIF ----

namespace {

struct GifOptions {
    bool interlaced = false;
    bool localPalette = false;
    bool transparent = false;  // the bottom right through a transparent index
    bool rowRamp = false;      // a palette of 256 grays, each row y * 5, instead of the picture
    int offset = 0;            // the frame this far in from the screen's top left
    bool secondFrame = false;
    int delay = 0;             // hundredths of a second
};

Bytes encodeGif(const GifOptions& o) {
    std::vector<std::array<std::uint8_t, 3>> palette;
    if (o.rowRamp) {
        for (int i = 0; i < 256; ++i) {
            palette.push_back({static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i)});
        }
    } else {
        palette = {{200, 40, 20}, {20, 40, 200}, {kMark[0], kMark[1], kMark[2]}, {0, 0, 0}};
    }
    const int sizeBits = o.rowRamp ? 7 : 1;  // 2^(n+1) entries
    const int minCodeSize = o.rowRamp ? 8 : 2;
    const auto putPalette = [&](Bytes& b) {
        for (const auto& c : palette) {
            b.insert(b.end(), c.begin(), c.end());
        }
    };
    const auto putData = [&](Bytes& b, const Bytes& indices) {
        b.push_back(static_cast<std::uint8_t>(minCodeSize));
        const Bytes data = iv::test::encodeGifLzw(indices, minCodeSize);
        for (std::size_t at = 0; at < data.size(); at += 255) {
            const std::size_t n = std::min<std::size_t>(255, data.size() - at);
            b.push_back(static_cast<std::uint8_t>(n));
            b.insert(b.end(), data.begin() + static_cast<std::ptrdiff_t>(at), data.begin() + static_cast<std::ptrdiff_t>(at + n));
        }
        b.push_back(0);
    };
    const auto putControl = [&](Bytes& b, bool transparent) {
        b.insert(b.end(), {0x21, 0xF9, 4, static_cast<std::uint8_t>(transparent ? 1 : 0)});
        put16(b, static_cast<std::uint32_t>(o.delay));
        b.insert(b.end(), {3, 0});
    };

    // Rows in the order stored: interlaced in four passes.
    std::vector<int> order;
    if (o.interlaced) {
        for (const auto& [start, step] : {std::pair{0, 8}, std::pair{4, 8}, std::pair{2, 4}, std::pair{1, 2}}) {
            for (int y = start; y < kHeight; y += step) {
                order.push_back(y);
            }
        }
    } else {
        for (int y = 0; y < kHeight; ++y) {
            order.push_back(y);
        }
    }
    Bytes indices;
    for (const int y : order) {
        for (int x = 0; x < kWidth; ++x) {
            int index = o.rowRamp ? y * 5 : indexAt(x, y);
            if (o.transparent && x >= kWidth / 2 && y >= kHeight / 2) {
                index = 3;
            }
            indices.push_back(static_cast<std::uint8_t>(index));
        }
    }

    Bytes b = text("GIF89a");
    put16(b, static_cast<std::uint32_t>(kWidth + o.offset));
    put16(b, static_cast<std::uint32_t>(kHeight + o.offset));
    b.push_back(static_cast<std::uint8_t>(o.localPalette ? 0 : 0x80 | sizeBits));
    b.insert(b.end(), {0, 0});
    if (!o.localPalette) {
        putPalette(b);
    }
    if (o.secondFrame) {
        b.insert(b.end(), {0x21, 0xFF, 11});
        append(b, "NETSCAPE2.0");
        b.insert(b.end(), {3, 1, 0, 0, 0});
    }
    putControl(b, o.transparent);
    b.push_back(0x2C);
    put16(b, static_cast<std::uint32_t>(o.offset));
    put16(b, static_cast<std::uint32_t>(o.offset));
    put16(b, kWidth);
    put16(b, kHeight);
    b.push_back(static_cast<std::uint8_t>((o.localPalette ? 0x80 | sizeBits : 0) | (o.interlaced ? 0x40 : 0)));
    if (o.localPalette) {
        putPalette(b);
    }
    putData(b, indices);
    if (o.secondFrame) {
        putControl(b, false);
        b.push_back(0x2C);
        put16(b, 0);
        put16(b, 0);
        put16(b, 1);
        put16(b, 1);
        b.push_back(0);
        putData(b, Bytes{1});
    }
    b.push_back(0x3B);
    return b;
}

std::shared_ptr<ImageSource> openGif(const GifOptions& options) { return open(write("x.gif", encodeGif(options))); }

}  // namespace

// Why: the plain GIF, one frame filling its screen, is what most GIFs on disk are. Input: the
// picture with a global palette and with a local one. The truth: the picture upright, opaque,
// not marked animated.
TEST(Gif, OneFrameWithAGlobalOrLocalPalette) {
    for (const bool local : {false, true}) {
        GifOptions options;
        options.localPalette = local;
        const auto source = openGif(options);
        ASSERT_TRUE(source) << local;
        expectPicture(*source, 0);
        expectUpright(*source);
        expectNoAlpha(*source);
        EXPECT_EQ(source->subimages()[0].stored.container, "GIF");
        EXPECT_FALSE(source->subimages()[0].stored.animated);
    }
}

// Why: interlaced GIFs store every eighth row first, in four passes, which must be put back in
// place; and a palette of grays makes a gray picture. Input: rows of gray y * 5, interlaced and
// not. The truth: an 8-bit gray picture whose every row y holds y * 5.
TEST(Gif, InterlacedRowsGoBackInPlace) {
    for (const bool interlaced : {false, true}) {
        GifOptions options;
        options.rowRamp = true;
        options.interlaced = interlaced;
        const auto source = openGif(options);
        ASSERT_TRUE(source);
        EXPECT_EQ(source->subimages()[0].format, PixelFormat::Gray8);
        const auto tile = source->readTile({0, 0, 0, 0}, {});
        ASSERT_TRUE(tile);
        for (int y = 0; y < kHeight; ++y) {
            EXPECT_EQ(reinterpret_cast<const std::uint8_t*>(tile->row(y))[7], y * 5) << "row " << y << " " << interlaced;
        }
    }
}

// Why: a GIF's transparency is one palette index, named in the control block before the frame,
// and a frame smaller than the screen leaves the rest of it clear. Input: the picture with its
// bottom right in the transparent index; and the picture as a frame 10 pixels in from the
// screen's corner. The truth: alpha 0 there and opaque colour elsewhere; the frame in place.
TEST(Gif, TransparencyAndAFrameSmallerThanTheScreen) {
    GifOptions clear;
    clear.transparent = true;
    const auto a = openGif(clear);
    ASSERT_TRUE(a);
    EXPECT_TRUE(a->subimages()[0].alpha);
    EXPECT_EQ(decoded(*a, kWidth - 8, kHeight - 8)[3], 0);
    EXPECT_EQ(decoded(*a, 8, 8), (std::array<int, 4>{200, 40, 20, 255}));
    EXPECT_EQ(decoded(*a, kWidth - 8, 8), (std::array<int, 4>{20, 40, 200, 255}));

    GifOptions inset;
    inset.offset = 10;
    const auto b = openGif(inset);
    ASSERT_TRUE(b);
    EXPECT_EQ(b->subimages()[0].levels[0].size, (Size{kWidth + 10, kHeight + 10}));
    EXPECT_EQ(decoded(*b, 5, 5)[3], 0);
    EXPECT_EQ(decoded(*b, 12, 10), (std::array<int, 4>{kMark[0], kMark[1], kMark[2], 255}));
    EXPECT_EQ(decoded(*b, 12, 11), (std::array<int, 4>{200, 40, 20, 255}));
}

// Why: animated GIFs are shown by their first frame for now, marked as animated, with the
// frame's delay kept. Input: the picture, then a second frame of one pixel, 0.1 s apart. The
// truth: the first frame, marked animated, a delay of 100 ms.
TEST(Gif, AnAnimationShowsItsFirstFrameMarked) {
    GifOptions options;
    options.secondFrame = true;
    options.delay = 10;
    const auto source = openGif(options);
    ASSERT_TRUE(source);
    expectPicture(*source, 0);
    expectUpright(*source);
    EXPECT_TRUE(source->subimages()[0].stored.animated);
    EXPECT_EQ(source->subimages()[0].frameDelay, std::chrono::milliseconds(100));
}

// Why: GIF's blocks and LZW data are where damaged and hostile files hit. Input: damaged and cut
// copies of interlaced, transparent and animated files. The truth: no crash or hang.
TEST(Gif, SurvivesDamage) {
    GifOptions all;
    all.interlaced = true;
    all.transparent = true;
    all.secondFrame = true;
    survivesDamage("x.gif", encodeGif(all));
    GifOptions ramp;
    ramp.rowRamp = true;
    ramp.offset = 3;
    survivesDamage("r.gif", encodeGif(ramp));
}
