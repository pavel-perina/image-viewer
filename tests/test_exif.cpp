#include "test_exif.h"

#include <array>
#include <string_view>

namespace iv::test {

namespace {

struct Entry {
    std::uint16_t tag;
    std::uint16_t type;
    std::uint32_t count;
    std::vector<std::uint8_t> value;
};

class Writer {
public:
    explicit Writer(bool little) : m_little(little) {}

    std::vector<std::uint8_t> u16(std::uint32_t v) const {
        return m_little ? std::vector<std::uint8_t>{std::uint8_t(v), std::uint8_t(v >> 8)}
                        : std::vector<std::uint8_t>{std::uint8_t(v >> 8), std::uint8_t(v)};
    }
    std::vector<std::uint8_t> u32(std::uint32_t v) const {
        auto low = u16(v & 0xFFFF);
        auto high = u16(v >> 16);
        auto& first = m_little ? low : high;
        const auto& second = m_little ? high : low;
        first.insert(first.end(), second.begin(), second.end());
        return first;
    }
    std::vector<std::uint8_t> rational(std::uint32_t n, std::uint32_t d) const {
        auto out = u32(n);
        const auto low = u32(d);
        out.insert(out.end(), low.begin(), low.end());
        return out;
    }
    static std::vector<std::uint8_t> text(std::string_view s) {
        std::vector<std::uint8_t> out(s.begin(), s.end());
        out.push_back(0);
        return out;
    }

    // Lays out a directory at the given offset of the TIFF structure, values after it.
    void directory(std::vector<std::uint8_t>& tiff, const std::vector<Entry>& entries) const {
        auto append = [&](const std::vector<std::uint8_t>& bytes) { tiff.insert(tiff.end(), bytes.begin(), bytes.end()); };
        append(u16(static_cast<std::uint32_t>(entries.size())));
        std::size_t valueAt = tiff.size() + entries.size() * 12 + 4;
        std::vector<std::uint8_t> values;
        for (const Entry& entry : entries) {
            append(u16(entry.tag));
            append(u16(entry.type));
            append(u32(entry.count));
            if (entry.value.size() <= 4) {
                auto inlined = entry.value;
                inlined.resize(4, 0);
                append(inlined);
            } else {
                append(u32(static_cast<std::uint32_t>(valueAt + values.size())));
                values.insert(values.end(), entry.value.begin(), entry.value.end());
            }
        }
        append(u32(0));
        append(values);
    }

private:
    bool m_little;
};

}  // namespace

std::vector<std::uint8_t> exampleExifBlock(bool littleEndian) {
    const Writer w(littleEndian);
    const auto make = Writer::text("Canon");
    const auto model = Writer::text("Canon EOS 5D");
    const auto date = Writer::text("2024:05:01 12:30:00");
    const auto lens = Writer::text("EF 50mm f/1.4");

    constexpr std::uint16_t kAscii = 2, kShort = 3, kLong = 4, kRational = 5, kSRational = 10;
    std::vector<std::uint8_t> exifIfd;
    // The directory is placed at a known offset, so first build it to learn the sizes.
    const auto buildExif = [&](std::size_t at) {
        std::vector<std::uint8_t> out(at, 0);
        w.directory(out, {
                             {0x829A, kRational, 1, w.rational(1, 250)},
                             {0x829D, kRational, 1, w.rational(28, 10)},
                             {0x8827, kShort, 1, w.u16(400)},
                             {0x9003, kAscii, static_cast<std::uint32_t>(date.size()), date},
                             {0x9204, kSRational, 1, w.rational(static_cast<std::uint32_t>(-2), 3)},
                             {0x9207, kShort, 1, w.u16(5)},
                             {0x920A, kRational, 1, w.rational(50, 1)},
                             {0xA403, kShort, 1, w.u16(1)},
                             {0xA405, kShort, 1, w.u16(75)},
                             {0xA434, kAscii, static_cast<std::uint32_t>(lens.size()), lens},
                         });
        return out;
    };

    // The main directory comes first, then the EXIF directory after its values.
    const auto buildMain = [&](std::uint32_t exifOffset) {
        std::vector<std::uint8_t> out;
        out.push_back(littleEndian ? 'I' : 'M');
        out.push_back(littleEndian ? 'I' : 'M');
        const auto magic = w.u16(42);
        out.insert(out.end(), magic.begin(), magic.end());
        const auto first = w.u32(8);
        out.insert(out.end(), first.begin(), first.end());
        w.directory(out, {
                             {0x010F, kAscii, static_cast<std::uint32_t>(make.size()), make},
                             {0x0110, kAscii, static_cast<std::uint32_t>(model.size()), model},
                             {0x0112, kShort, 1, w.u16(6)},
                             {0x8769, kLong, 1, w.u32(exifOffset)},
                         });
        return out;
    };

    const std::size_t exifAt = buildMain(0).size();
    std::vector<std::uint8_t> tiff = buildMain(static_cast<std::uint32_t>(exifAt));
    const std::vector<std::uint8_t> withExif = buildExif(exifAt);
    tiff.insert(tiff.end(), withExif.begin() + static_cast<std::ptrdiff_t>(exifAt), withExif.end());

    std::vector<std::uint8_t> block{'E', 'x', 'i', 'f', 0, 0};
    block.insert(block.end(), tiff.begin(), tiff.end());
    return block;
}

}  // namespace iv::test
