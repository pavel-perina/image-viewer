#pragma once

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace iv::test {

// A plain GIF LZW encoder, written from the GIF89a specification, as the reference the decoder
// is checked against. With clearWhenFull it starts a new table when the old one is full, as
// most encoders do; without, it keeps the full table, which decoders must accept too.
inline std::vector<std::uint8_t> encodeGifLzw(const std::vector<std::uint8_t>& in, int minCodeSize, bool clearWhenFull = true) {
    std::vector<std::uint8_t> out;
    std::uint32_t bits = 0;
    int bitCount = 0;
    int codeSize = minCodeSize + 1;
    const int clear = 1 << minCodeSize;
    const auto emit = [&](int code) {
        bits |= static_cast<std::uint32_t>(code) << bitCount;
        bitCount += codeSize;
        while (bitCount >= 8) {
            out.push_back(static_cast<std::uint8_t>(bits & 0xFF));
            bits >>= 8;
            bitCount -= 8;
        }
    };
    std::map<std::pair<int, int>, int> table;
    int next = clear + 2;
    int emitted = 0;  // codes since the last clear
    emit(clear);
    if (in.empty()) {
        emit(clear + 1);
    } else {
        int current = in[0];
        for (std::size_t i = 1; i < in.size(); ++i) {
            const auto key = std::pair{current, static_cast<int>(in[i])};
            if (const auto it = table.find(key); it != table.end()) {
                current = it->second;
                continue;
            }
            emit(current);
            ++emitted;
            if (next < 4096) {
                table[key] = next++;
                // The decoder adds each code one step later, so it widens one code later too.
                if (next - 1 == (1 << codeSize) && codeSize < 12) {
                    ++codeSize;
                }
            }
            if (next == 4096 && clearWhenFull) {
                emit(clear);
                table.clear();
                next = clear + 2;
                codeSize = minCodeSize + 1;
                emitted = 0;
            }
            current = in[i];
        }
        emit(current);
        ++emitted;
        // The decoder adds an entry on reading the last code, and may widen for the end code.
        if (emitted >= 2 && next < 4096 && next == (1 << codeSize) && codeSize < 12) {
            ++codeSize;
        }
        emit(clear + 1);
    }
    if (bitCount > 0) {
        out.push_back(static_cast<std::uint8_t>(bits & 0xFF));
    }
    return out;
}

}  // namespace iv::test
