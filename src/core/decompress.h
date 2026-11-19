#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace iv {

// Decompression that formats parsed by hand share. Each works on bytes in memory and never
// reads or writes past what it is given: damaged data ends the output early, and what was not
// written keeps what the caller put there.

// GIF's LZW: codes stored least significant bit first, from minCodeSize + 1 bits up to 12, with
// a clear code (1 << minCodeSize) and an end code after it; a full table is kept until the next
// clear code. The data is the image's sub-blocks joined, without their length bytes. Returns
// how many values it wrote.
std::size_t decodeGifLzw(std::span<const std::uint8_t> data, int minCodeSize, std::span<std::uint8_t> out);

// BMP's RLE8 and RLE4 (bitsPerIndex 8 or 4): runs of one palette index, groups of literal
// indices, ends of lines, jumps and the end of the picture. Rows are in stored order, the first
// stored row first, width indices each. A pixel written gets 1 in `covered`; pixels a jump or an
// early end skip keep their index and 0, as some files leave them to show the background.
void decodeBmpRle(std::span<const std::uint8_t> data, int bitsPerIndex, int width, int height,
                  std::span<std::uint8_t> indices, std::span<std::uint8_t> covered);

// Run-length packets as TGA stores them: a byte whose top bit says whether one pixel repeats or
// pixels follow as they are, and whose low seven bits are the count less one. Pixels are
// pixelBytes long; packets may run on from one line to the next. Returns how many pixels it
// wrote, and how many bytes of data it read in `used` if given.
std::size_t decodePacketRle(std::span<const std::uint8_t> data, int pixelBytes, std::span<std::uint8_t> out,
                            std::size_t* used = nullptr);

}  // namespace iv
