#include "core/exif.h"

#include <algorithm>
#include <array>

namespace iv {

Orientation parseExifOrientation(std::span<const std::uint8_t> app1) {
    static constexpr std::array<std::uint8_t, 6> kSignature{'E', 'x', 'i', 'f', 0, 0};
    if (app1.size() < kSignature.size() ||
        !std::equal(kSignature.begin(), kSignature.end(), app1.begin())) {
        return Orientation::Normal;
    }
    const auto tiff = app1.subspan(kSignature.size());
    if (tiff.size() < 8) {
        return Orientation::Normal;
    }

    bool littleEndian;
    if (tiff[0] == 'I' && tiff[1] == 'I') {
        littleEndian = true;
    } else if (tiff[0] == 'M' && tiff[1] == 'M') {
        littleEndian = false;
    } else {
        return Orientation::Normal;
    }

    const auto read16 = [&](std::size_t at) -> std::uint32_t {
        return littleEndian ? tiff[at] | tiff[at + 1] << 8 : tiff[at] << 8 | tiff[at + 1];
    };
    const auto read32 = [&](std::size_t at) -> std::uint32_t {
        return littleEndian ? read16(at) | read16(at + 2) << 16 : read16(at) << 16 | read16(at + 2);
    };

    if (read16(2) != 42) {
        return Orientation::Normal;
    }
    const std::size_t ifd = read32(4);
    if (ifd > tiff.size() || tiff.size() - ifd < 2) {
        return Orientation::Normal;
    }

    static constexpr std::size_t kEntrySize = 12;
    static constexpr std::uint32_t kOrientationTag = 0x0112;
    static constexpr std::uint32_t kTypeShort = 3;
    const std::size_t count = read16(ifd);
    const std::size_t firstEntry = ifd + 2;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t entry = firstEntry + i * kEntrySize;
        if (entry + kEntrySize > tiff.size()) {
            break;
        }
        if (read16(entry) == kOrientationTag && read16(entry + 2) == kTypeShort) {
            const std::uint32_t value = read16(entry + 8);
            return value >= 1 && value <= 8 ? static_cast<Orientation>(value) : Orientation::Normal;
        }
    }
    return Orientation::Normal;
}

}  // namespace iv
