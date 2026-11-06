#include "core/exif.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

namespace iv {

namespace {

constexpr std::size_t kEntrySize = 12;
constexpr std::size_t kMaxTextLength = 256;

enum Type : std::uint32_t { Byte = 1, Ascii = 2, Short = 3, Long = 4, Rational = 5, SRational = 10 };

enum Tag : std::uint32_t {
    InteropIndex = 0x0001,
    Make = 0x010F,
    Model = 0x0110,
    OrientationTag = 0x0112,
    DateTime = 0x0132,
    ExposureTime = 0x829A,
    FNumber = 0x829D,
    ExifIfd = 0x8769,
    Iso = 0x8827,
    DateTimeOriginal = 0x9003,
    ExposureBias = 0x9204,
    FocalLength = 0x920A,
    ColorSpaceTag = 0xA001,
    InteropIfd = 0xA005,
    FocalLength35mm = 0xA405,
    LensMake = 0xA433,
    LensModel = 0xA434,
};

class TiffReader {
public:
    TiffReader(std::span<const std::uint8_t> data, bool littleEndian) : m_data(data), m_littleEndian(littleEndian) {}

    std::uint32_t read16(std::size_t at) const {
        if (at > m_data.size() || m_data.size() - at < 2) {
            return 0;
        }
        return m_littleEndian ? m_data[at] | m_data[at + 1] << 8 : m_data[at] << 8 | m_data[at + 1];
    }

    std::uint32_t read32(std::size_t at) const {
        if (at > m_data.size() || m_data.size() - at < 4) {
            return 0;
        }
        return m_littleEndian ? read16(at) | read16(at + 2) << 16 : read16(at) << 16 | read16(at + 2);
    }

    std::size_t size() const { return m_data.size(); }

    // The bytes of an entry's value, or nothing if they lie outside the data.
    std::span<const std::uint8_t> value(std::size_t entry) const {
        static constexpr auto typeSize = [](std::uint32_t type) -> std::uint64_t {
            switch (type) {
            case Byte:
            case Ascii:     return 1;
            case Short:     return 2;
            case Long:      return 4;
            case Rational:
            case SRational: return 8;
            default:        return 0;
            }
        };
        const std::uint64_t bytes = typeSize(read16(entry + 2)) * read32(entry + 4);
        if (bytes == 0) {
            return {};
        }
        const std::uint64_t at = bytes <= 4 ? entry + 8 : read32(entry + 8);
        if (at > m_data.size() || bytes > m_data.size() - at) {
            return {};
        }
        return m_data.subspan(static_cast<std::size_t>(at), static_cast<std::size_t>(bytes));
    }

    std::string text(std::size_t entry) const {
        if (read16(entry + 2) != Ascii) {
            return {};
        }
        const auto bytes = value(entry);
        return cleanExifText({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    }

    std::optional<double> rational(std::size_t entry) const {
        const std::uint32_t type = read16(entry + 2);
        if ((type != Rational && type != SRational) || value(entry).size() < 8) {
            return std::nullopt;
        }
        const std::size_t at = static_cast<std::size_t>(value(entry).data() - m_data.data());
        const std::uint32_t numerator = read32(at);
        const std::uint32_t denominator = read32(at + 4);
        if (denominator == 0) {
            return std::nullopt;  // "unknown" in EXIF
        }
        if (type == SRational) {
            return static_cast<double>(static_cast<std::int32_t>(numerator)) / static_cast<std::int32_t>(denominator);
        }
        return static_cast<double>(numerator) / denominator;
    }

    std::optional<std::uint32_t> integer(std::size_t entry) const {
        const std::uint32_t type = read16(entry + 2);
        if ((type != Short && type != Long) || value(entry).empty()) {
            return std::nullopt;
        }
        const std::size_t at = static_cast<std::size_t>(value(entry).data() - m_data.data());
        return type == Short ? read16(at) : read32(at);
    }

    // Calls visit with the position of each entry of the directory at the given offset.
    template <typename Visit>
    void forEachEntry(std::size_t ifd, Visit visit) const {
        if (ifd > m_data.size() || m_data.size() - ifd < 2) {
            return;
        }
        const std::size_t count = read16(ifd);
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t entry = ifd + 2 + i * kEntrySize;
            if (entry + kEntrySize > m_data.size()) {
                break;
            }
            visit(entry);
        }
    }

private:
    std::span<const std::uint8_t> m_data;
    bool m_littleEndian;
};

}  // namespace

std::string cleanExifText(std::string_view text) {
    text = text.substr(0, std::min(text.find('\0'), kMaxTextLength));
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F) {
            out.push_back(c);
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    const auto first = out.find_first_not_of(' ');
    return first == std::string::npos ? std::string() : out.substr(first);
}

std::string normalizeExifDate(std::string_view date) {
    static constexpr std::string_view kPattern = "dddd:dd:dd dd:dd:dd";
    if (date.size() < kPattern.size()) {
        return {};
    }
    std::string out(date.substr(0, kPattern.size()));
    for (std::size_t i = 0; i < kPattern.size(); ++i) {
        const bool digit = std::isdigit(static_cast<unsigned char>(out[i])) != 0;
        if (kPattern[i] == 'd' ? !digit : out[i] != kPattern[i]) {
            return {};
        }
    }
    if (out.starts_with("0000")) {
        return {};
    }
    out[4] = '-';
    out[7] = '-';
    return out;
}

ExifData parseExif(std::span<const std::uint8_t> app1) {
    ExifData exif;
    static constexpr std::array<std::uint8_t, 6> kSignature{'E', 'x', 'i', 'f', 0, 0};
    const bool signed_ = app1.size() >= kSignature.size() && std::equal(kSignature.begin(), kSignature.end(), app1.begin());
    const auto tiff = signed_ ? app1.subspan(kSignature.size()) : app1;
    if (tiff.size() < 8) {
        return exif;
    }
    if (!(tiff[0] == 'I' && tiff[1] == 'I') && !(tiff[0] == 'M' && tiff[1] == 'M')) {
        return exif;
    }
    const TiffReader reader(tiff, tiff[0] == 'I');
    if (reader.read16(2) != 42) {
        return exif;
    }
    exif.present = true;

    std::string dateTime;
    std::size_t exifIfd = 0;
    reader.forEachEntry(reader.read32(4), [&](std::size_t entry) {
        switch (reader.read16(entry)) {
        case Make:     exif.make = reader.text(entry); break;
        case Model:    exif.model = reader.text(entry); break;
        case DateTime: dateTime = reader.text(entry); break;
        case ExifIfd:  exifIfd = reader.integer(entry).value_or(0); break;
        case OrientationTag:
            if (const auto value = reader.integer(entry); value && *value >= 1 && *value <= 8) {
                exif.orientation = static_cast<Orientation>(*value);
            }
            break;
        default: break;
        }
    });

    std::string lensMake;
    std::string dateOriginal;
    std::uint64_t colorSpace = 0;
    std::size_t interopIfd = 0;
    if (exifIfd != 0) {
        reader.forEachEntry(exifIfd, [&](std::size_t entry) {
            switch (reader.read16(entry)) {
            case ExposureTime:     exif.exposureSeconds = reader.rational(entry); break;
            case FNumber:          exif.fNumber = reader.rational(entry); break;
            case ExposureBias:     exif.exposureBias = reader.rational(entry); break;
            case FocalLength:      exif.focalLength = reader.rational(entry); break;
            case DateTimeOriginal: dateOriginal = reader.text(entry); break;
            case LensMake:         lensMake = reader.text(entry); break;
            case LensModel:        exif.lens = reader.text(entry); break;
            case ColorSpaceTag:    colorSpace = reader.integer(entry).value_or(0); break;
            case InteropIfd:       interopIfd = reader.integer(entry).value_or(0); break;
            case Iso:
                if (const auto value = reader.integer(entry); value && *value > 0) {
                    exif.iso = static_cast<int>(*value);
                }
                break;
            case FocalLength35mm:
                if (const auto value = reader.integer(entry); value && *value > 0) {
                    exif.focalLength35mm = static_cast<double>(*value);
                }
                break;
            default: break;
            }
        });
    }
    // Design rule for cameras: "R03" marks Adobe RGB, and 1 would mean sRGB.
    if (interopIfd != 0 && colorSpace != 1) {
        reader.forEachEntry(interopIfd, [&](std::size_t entry) {
            if (reader.read16(entry) == InteropIndex && reader.text(entry) == "R03") {
                exif.adobeRgb = true;
            }
        });
    }
    exif.dateTaken = normalizeExifDate(dateOriginal);
    if (exif.dateTaken.empty()) {
        exif.dateTaken = normalizeExifDate(dateTime);
    }
    if (exif.lens.empty()) {
        exif.lens = lensMake;
    }
    return exif;
}

}  // namespace iv
