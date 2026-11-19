#include "core/decompress.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace iv {

std::size_t decodeGifLzw(std::span<const std::uint8_t> data, int minCodeSize, std::span<std::uint8_t> out) {
    if (minCodeSize < 1 || minCodeSize > 11) {
        return 0;
    }
    constexpr int kMaxCodes = 4096;
    const int clear = 1 << minCodeSize;
    const int end = clear + 1;
    // Each code is its prefix code and a last value; first is the value it starts with, and
    // length how many it stands for, so that it can be written backwards into place.
    std::array<std::uint16_t, kMaxCodes> prefix{};
    std::array<std::uint8_t, kMaxCodes> last{};
    std::array<std::uint8_t, kMaxCodes> first{};
    std::array<std::uint16_t, kMaxCodes> length{};
    for (int code = 0; code < clear; ++code) {
        last[static_cast<std::size_t>(code)] = static_cast<std::uint8_t>(code);
        first[static_cast<std::size_t>(code)] = static_cast<std::uint8_t>(code);
        length[static_cast<std::size_t>(code)] = 1;
    }

    int codeSize = minCodeSize + 1;
    int next = clear + 2;
    int previous = -1;
    std::size_t written = 0;
    std::uint32_t bits = 0;
    int bitCount = 0;
    std::size_t at = 0;
    while (written < out.size()) {
        while (bitCount < codeSize) {
            if (at >= data.size()) {
                return written;
            }
            bits |= static_cast<std::uint32_t>(data[at++]) << bitCount;
            bitCount += 8;
        }
        const int code = static_cast<int>(bits & ((1U << codeSize) - 1));
        bits >>= codeSize;
        bitCount -= codeSize;

        if (code == clear) {
            codeSize = minCodeSize + 1;
            next = clear + 2;
            previous = -1;
            continue;
        }
        if (code == end) {
            break;
        }
        if (previous < 0) {
            if (code >= clear) {
                break;  // the first code after a clear has to be a value
            }
            out[written++] = static_cast<std::uint8_t>(code);
            previous = code;
            continue;
        }
        if (code > next || (code == next && next >= kMaxCodes)) {
            break;  // damaged: a code not yet defined
        }
        // A code just being defined (code == next) is the previous string and its own first value.
        const bool known = code < next;
        const int source = known ? code : previous;
        const auto count = static_cast<std::size_t>(length[static_cast<std::size_t>(source)]) + (known ? 0 : 1);
        if (count > out.size() - written) {
            break;  // would overrun: the picture ends here
        }
        const std::uint8_t head = first[static_cast<std::size_t>(source)];
        std::size_t pos = written + count;
        if (!known) {
            out[--pos] = head;
        }
        for (int c = source; pos > written; c = prefix[static_cast<std::size_t>(c)]) {
            out[--pos] = last[static_cast<std::size_t>(c)];
        }
        written += count;

        if (next < kMaxCodes) {
            const auto n = static_cast<std::size_t>(next);
            prefix[n] = static_cast<std::uint16_t>(previous);
            last[n] = head;
            first[n] = first[static_cast<std::size_t>(previous)];
            length[n] = static_cast<std::uint16_t>(length[static_cast<std::size_t>(previous)] + 1);
            ++next;
            if (next == (1 << codeSize) && codeSize < 12) {
                ++codeSize;
            }
        }
        previous = code;
    }
    return written;
}

void decodeBmpRle(std::span<const std::uint8_t> data, int bitsPerIndex, int width, int height,
                  std::span<std::uint8_t> indices, std::span<std::uint8_t> covered) {
    const auto pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (width <= 0 || height <= 0 || indices.size() < pixels || covered.size() < pixels || (bitsPerIndex != 4 && bitsPerIndex != 8)) {
        return;
    }
    const bool four = bitsPerIndex == 4;
    int x = 0;
    int y = 0;
    const auto put = [&](std::uint8_t index) {
        if (x < width && y < height) {
            const auto i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            indices[i] = index;
            covered[i] = 1;
        }
        ++x;
    };
    std::size_t at = 0;
    while (at + 1 < data.size() && y < height) {
        const std::uint8_t count = data[at];
        const std::uint8_t value = data[at + 1];
        at += 2;
        if (count > 0) {  // a run: in RLE4 the two halves of the byte take turns
            for (int i = 0; i < count; ++i) {
                put(four ? static_cast<std::uint8_t>(i % 2 == 0 ? value >> 4 : value & 0x0F) : value);
            }
            continue;
        }
        if (value == 0) {  // end of line
            x = 0;
            ++y;
        } else if (value == 1) {  // end of picture
            return;
        } else if (value == 2) {  // jump right and down
            if (at + 1 >= data.size()) {
                return;
            }
            x += data[at];
            y += data[at + 1];
            at += 2;
        } else {  // value indices as they are, padded to a whole number of 16-bit words
            const std::size_t bytes = four ? (static_cast<std::size_t>(value) + 1) / 2 : value;
            const std::size_t padded = (bytes + 1) & ~std::size_t{1};
            if (bytes > data.size() - at) {
                return;
            }
            for (int i = 0; i < value; ++i) {
                const std::uint8_t byte = data[at + (four ? static_cast<std::size_t>(i) / 2 : static_cast<std::size_t>(i))];
                put(four ? static_cast<std::uint8_t>(i % 2 == 0 ? byte >> 4 : byte & 0x0F) : byte);
            }
            at += std::min(padded, data.size() - at);
        }
    }
}

std::size_t decodePacketRle(std::span<const std::uint8_t> data, int pixelBytes, std::span<std::uint8_t> out,
                            std::size_t* used) {
    if (pixelBytes <= 0) {
        if (used) {
            *used = 0;
        }
        return 0;
    }
    const auto size = static_cast<std::size_t>(pixelBytes);
    const std::size_t capacity = out.size() / size;
    std::size_t written = 0;
    std::size_t at = 0;
    while (written < capacity && at < data.size()) {
        const std::uint8_t header = data[at++];
        const std::size_t count = std::min<std::size_t>((header & 0x7F) + 1U, capacity - written);
        if (header & 0x80) {  // one pixel, repeated
            if (size > data.size() - at) {
                break;
            }
            for (std::size_t i = 0; i < count; ++i) {
                std::memcpy(out.data() + (written + i) * size, data.data() + at, size);
            }
            at += size;
        } else {  // pixels as they are
            const std::size_t available = std::min(count, (data.size() - at) / size);
            std::memcpy(out.data() + written * size, data.data() + at, available * size);
            at += available * size;
            if (available < count) {
                written += available;
                break;
            }
        }
        written += count;
    }
    if (used) {
        *used = at;
    }
    return written;
}

}  // namespace iv
