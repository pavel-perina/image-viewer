#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <utility>
#include <vector>

#include "core/byte_reader.h"
#include "core/decompress.h"
#include "test_lzw.h"

using namespace iv;
using Bytes = std::vector<std::uint8_t>;
using iv::test::encodeGifLzw;

namespace {

Bytes randomValues(std::size_t count, int range, unsigned seed) {
    std::mt19937 random(seed);
    Bytes values(count);
    for (auto& v : values) {
        v = static_cast<std::uint8_t>(random() % static_cast<unsigned>(range));
    }
    return values;
}

}  // namespace

// Why: the reader is what keeps hand-written parsers from reading past the end of a file.
// Input: four bytes read as numbers of each size and order, then beyond them. The truth: the
// values a little- or big-endian reading gives, and 0 and failed once past the end.
TEST(ByteReader, ReadsBothOrdersAndFailsPastTheEnd) {
    const Bytes bytes{0x01, 0x02, 0x03, 0x04};
    ByteReader le(bytes);
    EXPECT_EQ(le.le32(), 0x04030201U);
    EXPECT_FALSE(le.failed());
    EXPECT_EQ(le.u8(), 0);
    EXPECT_TRUE(le.failed());
    ByteReader be(bytes);
    EXPECT_EQ(be.be16(), 0x0102);
    EXPECT_EQ(be.be16(), 0x0304);
    EXPECT_EQ(be.bytes(1).size(), 0U);
    EXPECT_TRUE(be.failed());
    ByteReader far(bytes, 9);
    EXPECT_TRUE(far.failed());
}

// Why: GIF pictures are LZW data; a wrong code width or table entry shifts every pixel after it.
// Input: random values of each width a GIF uses, long enough to widen codes to 12 bits and fill
// the table, encoded by the reference encoder above. The truth: the values that went in.
TEST(GifLzw, DecodesWhatTheReferenceEncoderWroteAtEveryWidth) {
    for (const int minCodeSize : {2, 3, 4, 8}) {
        const Bytes values = randomValues(20000, 1 << minCodeSize, static_cast<unsigned>(minCodeSize));
        for (const bool clearWhenFull : {true, false}) {
            const Bytes data = encodeGifLzw(values, minCodeSize, clearWhenFull);
            Bytes out(values.size(), 0xEE);
            EXPECT_EQ(decodeGifLzw(data, minCodeSize, out), values.size()) << minCodeSize << " " << clearWhenFull;
            EXPECT_EQ(out, values) << minCodeSize << " " << clearWhenFull;
        }
    }
}

// Why: long runs of one value make the codes that refer to themselves (a string and its own
// first value), which a decoder gets wrong most easily. Input: runs of one value and a picture
// of one value only. The truth: the values that went in.
TEST(GifLzw, RunsOfOneValueRoundTrip) {
    Bytes values(5000, 3);
    for (std::size_t i = 2000; i < 2100; ++i) {
        values[i] = 1;
    }
    Bytes out(values.size());
    EXPECT_EQ(decodeGifLzw(encodeGifLzw(values, 2), 2, out), values.size());
    EXPECT_EQ(out, values);
}

// Why: a damaged or short file must end the picture early, never overrun the output.
// Input: the data cut short, a code never defined, more data than the picture holds, and a
// width a GIF cannot have. The truth: no more values than there is room for, the rest
// untouched, and nothing for the impossible width.
TEST(GifLzw, DamagedDataEndsEarlyWithinTheOutput) {
    const Bytes values = randomValues(3000, 16, 7);
    const Bytes data = encodeGifLzw(values, 4);
    Bytes out(values.size(), 0xEE);
    const std::size_t written = decodeGifLzw(std::span(data).first(data.size() / 2), 4, out);
    EXPECT_LT(written, values.size());
    EXPECT_GT(written, 0U);
    EXPECT_TRUE(std::equal(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(written), values.begin()));
    EXPECT_EQ(out.back(), 0xEE);

    // Five-bit codes: clear (16), the value 1, then 30, far past the next code to be defined.
    const Bytes undefined{0x30, 0x78};
    Bytes small(10, 0xEE);
    EXPECT_EQ(decodeGifLzw(undefined, 4, small), 1U);
    EXPECT_EQ(small[0], 1);
    EXPECT_EQ(small[1], 0xEE);

    Bytes tiny(100, 0xEE);
    EXPECT_EQ(decodeGifLzw(data, 4, tiny), 100U) << "stops when the picture is full";
    EXPECT_EQ(decodeGifLzw(data, 0, tiny), 0U);
    EXPECT_EQ(decodeGifLzw(data, 12, tiny), 0U);
}

// Why: BMP keeps palette pictures as RLE8, with ends of lines, jumps and literal groups padded
// to 16-bit words. Input: a 4 x 3 picture written by hand in each kind of instruction. The
// truth: the indices worked out by hand from the BMP specification, with the jumped-over
// pixels left uncovered.
TEST(BmpRle, Rle8RunsLiteralsLinesAndJumps) {
    const Bytes data{
        3, 7,           // three of index 7
        0, 0,           // end of line
        0, 3, 1, 2, 3, 0,  // three literal indices, padded to an even length
        0, 0,           // end of line
        0, 2, 2, 0,     // jump two right, none down
        2, 9,           // two of index 9
        0, 1,           // end of picture
    };
    Bytes indices(12, 0);
    Bytes covered(12, 0);
    decodeBmpRle(data, 8, 4, 3, indices, covered);
    EXPECT_EQ(indices, (Bytes{7, 7, 7, 0, 1, 2, 3, 0, 0, 0, 9, 9}));
    EXPECT_EQ(covered, (Bytes{1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 1, 1}));
}

// Why: RLE4 packs two indices to a byte, which runs alternate between, and literal groups are
// counted in indices but padded in bytes (a group has at least three: 0 1 and 0 2 mean the end
// and a jump). Input: a 6 x 1 picture: a run of three, then three literals in two bytes. The
// truth: the alternating indices, then the literals in order.
TEST(BmpRle, Rle4AlternatesTheHalvesOfAByte) {
    const Bytes data{3, 0x12, 0, 3, 0x34, 0x50, 0, 1};
    Bytes indices(6, 0);
    Bytes covered(6, 0);
    decodeBmpRle(data, 4, 6, 1, indices, covered);
    EXPECT_EQ(indices, (Bytes{1, 2, 1, 3, 4, 5}));
    EXPECT_EQ(covered, (Bytes{1, 1, 1, 1, 1, 1}));
}

// Why: runs past the right edge, jumps out of the picture and a literal group cut off by the end
// of the file must not write outside the picture. Input: each of them on a 2 x 2 picture. The
// truth: only pixels inside are written.
TEST(BmpRle, StaysInsideThePicture) {
    Bytes indices(4, 0);
    Bytes covered(4, 0);
    decodeBmpRle(Bytes{200, 5, 0, 2, 255, 255, 0, 9, 1}, 8, 2, 2, indices, covered);
    EXPECT_EQ(indices, (Bytes{5, 5, 0, 0}));
    decodeBmpRle(Bytes{1, 1}, 6, 2, 2, indices, covered);  // a depth RLE does not have: nothing
    EXPECT_EQ(indices, (Bytes{5, 5, 0, 0}));
}

// Why: TGA's run-length packets carry whole pixels and may run from one line into the next.
// Input: two-byte pixels in a run of three and a raw group of two, then the same data cut short.
// The truth: the five pixels, and only the whole ones there is data for.
TEST(PacketRle, RunsAndRawGroupsOfWholePixels) {
    const Bytes data{0x82, 0xAA, 0xBB, 0x01, 0x01, 0x02, 0x03, 0x04};
    Bytes out(10, 0);
    std::size_t used = 0;
    EXPECT_EQ(decodePacketRle(data, 2, out, &used), 5U);
    EXPECT_EQ(used, data.size());
    EXPECT_EQ(out, (Bytes{0xAA, 0xBB, 0xAA, 0xBB, 0xAA, 0xBB, 0x01, 0x02, 0x03, 0x04}));

    Bytes shortOut(10, 0);
    EXPECT_EQ(decodePacketRle(std::span(data).first(6), 2, shortOut), 4U) << "the half pixel is left out";
    Bytes room(4, 0);
    EXPECT_EQ(decodePacketRle(data, 2, room), 2U) << "no more than there is room for";
}
