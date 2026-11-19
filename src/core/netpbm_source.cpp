#include "core/netpbm_source.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/color.h"
#include "core/whole_image_source.h"

namespace iv {

namespace {

bool isSpace(std::uint8_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

// Reads the header's numbers and words, past white space and # comments.
class Tokens {
public:
    Tokens(std::span<const std::uint8_t> bytes, std::size_t at) : m_bytes(bytes), m_at(at) {}

    std::size_t position() const { return m_at; }

    void skipSpace() {
        while (m_at < m_bytes.size()) {
            if (m_bytes[m_at] == '#') {
                while (m_at < m_bytes.size() && m_bytes[m_at] != '\n' && m_bytes[m_at] != '\r') {
                    ++m_at;
                }
            } else if (isSpace(m_bytes[m_at])) {
                ++m_at;
            } else {
                return;
            }
        }
    }

    std::string_view word() {
        skipSpace();
        const std::size_t start = m_at;
        while (m_at < m_bytes.size() && !isSpace(m_bytes[m_at]) && m_bytes[m_at] != '#') {
            ++m_at;
        }
        return {reinterpret_cast<const char*>(m_bytes.data()) + start, m_at - start};
    }

    std::optional<std::uint32_t> number() {
        const std::string_view w = word();
        std::uint32_t value = 0;
        const auto [end, error] = std::from_chars(w.data(), w.data() + w.size(), value);
        if (w.empty() || error != std::errc() || end != w.data() + w.size()) {
            return std::nullopt;
        }
        return value;
    }

    // Binary data begins after exactly one white space character.
    bool endHeader() {
        if (m_at >= m_bytes.size() || !isSpace(m_bytes[m_at])) {
            return false;
        }
        ++m_at;
        return true;
    }

private:
    std::span<const std::uint8_t> m_bytes;
    std::size_t m_at;
};

enum class Kind { Bitmap, Gray, Colour, Pam, Float };

struct Header {
    Kind kind = Kind::Gray;
    bool text = false;
    int width = 0;
    int height = 0;
    int depth = 1;          // samples per pixel: 1 gray, 2 gray + alpha, 3 colour, 4 colour + alpha
    std::uint32_t maxval = 1;
    bool littleEndian = false;  // PFM only
    std::size_t dataAt = 0;
    PixelFormat format = PixelFormat::Gray8;
};

Result<Header> readHeader(std::span<const std::uint8_t> bytes) {
    Header h;
    const char type = static_cast<char>(bytes[1]);
    Tokens t(bytes, 2);
    const auto size = [&]() -> bool {
        const auto w = t.number();
        const auto ht = t.number();
        if (!w || !ht || *w == 0 || *ht == 0 || *w > std::numeric_limits<int>::max() || *ht > std::numeric_limits<int>::max()) {
            return false;
        }
        h.width = static_cast<int>(*w);
        h.height = static_cast<int>(*ht);
        return true;
    };

    if (type == 'F' || type == 'f') {
        h.kind = Kind::Float;
        h.depth = type == 'F' ? 3 : 1;
        if (!size()) {
            return std::unexpected("invalid PFM size");
        }
        const std::string scaleText(t.word());
        double scale = 0;
        try {
            scale = std::stod(scaleText);
        } catch (...) {
            return std::unexpected("invalid PFM scale");
        }
        if (scale == 0 || !std::isfinite(scale)) {
            return std::unexpected("invalid PFM scale");
        }
        h.littleEndian = scale < 0;
    } else if (type == '7') {
        h.kind = Kind::Pam;
        std::optional<std::uint32_t> width, height, depth, maxval;
        std::string tupleType;
        for (;;) {
            const std::string_view key = t.word();
            if (key.empty()) {
                return std::unexpected("PAM header has no ENDHDR");
            }
            if (key == "ENDHDR") {
                break;
            }
            if (key == "TUPLTYPE") {
                tupleType = t.word();
            } else if (key == "WIDTH") {
                width = t.number();
            } else if (key == "HEIGHT") {
                height = t.number();
            } else if (key == "DEPTH") {
                depth = t.number();
            } else if (key == "MAXVAL") {
                maxval = t.number();
            } else {
                return std::unexpected("unknown PAM header line");
            }
        }
        if (!width || !height || !depth || !maxval || *width == 0 || *height == 0 || *width > std::numeric_limits<int>::max() ||
            *height > std::numeric_limits<int>::max() || *depth < 1 || *depth > 4 || *maxval < 1 || *maxval > 65535) {
            return std::unexpected("invalid PAM header");
        }
        h.width = static_cast<int>(*width);
        h.height = static_cast<int>(*height);
        h.depth = static_cast<int>(*depth);
        h.maxval = *maxval;
    } else {
        const int n = type - '0';
        h.text = n <= 3;
        h.kind = n == 1 || n == 4 ? Kind::Bitmap : n == 2 || n == 5 ? Kind::Gray : Kind::Colour;
        h.depth = h.kind == Kind::Colour ? 3 : 1;
        if (!size()) {
            return std::unexpected("invalid Netpbm size");
        }
        if (h.kind != Kind::Bitmap) {
            const auto maxval = t.number();
            if (!maxval || *maxval < 1 || *maxval > 65535) {
                return std::unexpected("invalid Netpbm maximum value");
            }
            h.maxval = *maxval;
        }
    }
    // Text samples may follow after any white space; binary ones after exactly one character.
    if (!t.endHeader()) {
        return std::unexpected("truncated Netpbm header");
    }
    h.dataAt = t.position();

    const bool wide = h.maxval > 255;
    if (h.kind == Kind::Float) {
        h.format = h.depth == 1 ? PixelFormat::Gray32F : PixelFormat::Rgba32F;
    } else if (h.depth == 1) {
        h.format = wide ? PixelFormat::Gray16 : PixelFormat::Gray8;
    } else {
        h.format = wide ? PixelFormat::Rgba16 : PixelFormat::Bgrx8;
    }
    return h;
}

// The next sample of a text picture; P1 samples are single digits that need no space between.
std::optional<std::uint32_t> textSample(std::span<const std::uint8_t> bytes, std::size_t& at, bool bitmap) {
    while (at < bytes.size()) {
        if (bytes[at] == '#') {
            while (at < bytes.size() && bytes[at] != '\n') {
                ++at;
            }
        } else if (isSpace(bytes[at])) {
            ++at;
        } else {
            break;
        }
    }
    if (bitmap) {
        if (at < bytes.size() && (bytes[at] == '0' || bytes[at] == '1')) {
            return static_cast<std::uint32_t>(bytes[at++] - '0');
        }
        return std::nullopt;
    }
    std::uint32_t value = 0;
    const auto* begin = reinterpret_cast<const char*>(bytes.data()) + at;
    const auto [end, error] = std::from_chars(begin, reinterpret_cast<const char*>(bytes.data()) + bytes.size(), value);
    if (error != std::errc() || end == begin) {
        return std::nullopt;
    }
    at += static_cast<std::size_t>(end - begin);
    return value;
}

Result<ImageBuffer> decodeFloat(std::span<const std::uint8_t> bytes, const Header& h, const std::stop_token& stop) {
    const ImageBuffer image = ImageBuffer::create({h.width, h.height}, h.format);
    const auto samples = static_cast<std::size_t>(h.width) * static_cast<std::size_t>(h.depth);
    const bool swap = h.littleEndian != (std::endian::native == std::endian::little);
    for (int stored = 0; stored < h.height; ++stored) {
        if (stop.stop_requested()) {
            return std::unexpected("cancelled");
        }
        // Rows go from the bottom up.
        auto* out = reinterpret_cast<float*>(image.row(h.height - 1 - stored));
        const std::size_t at = h.dataAt + static_cast<std::size_t>(stored) * samples * 4;
        for (int x = 0; x < h.width; ++x) {
            for (int c = 0; c < 4 && c < (h.depth == 1 ? 1 : 4); ++c) {
                float value = 1.0F;  // the alpha PFM does not have
                if (c < h.depth) {
                    const std::size_t s = at + (static_cast<std::size_t>(x) * static_cast<std::size_t>(h.depth) + static_cast<std::size_t>(c)) * 4;
                    value = 0.0F;
                    if (s <= bytes.size() && bytes.size() - s >= 4) {
                        std::uint32_t raw = 0;
                        std::memcpy(&raw, bytes.data() + s, 4);
                        value = std::bit_cast<float>(swap ? std::byteswap(raw) : raw);
                        if (!std::isfinite(value)) {
                            value = 0.0F;
                        }
                    }
                }
                out[static_cast<std::size_t>(x) * (h.depth == 1 ? 1 : 4) + static_cast<std::size_t>(c)] = value;
            }
        }
    }
    return image.withAlpha(false);
}

Result<ImageBuffer> decode(std::span<const std::uint8_t> bytes, const Header& h, const std::stop_token& stop) {
    if (h.kind == Kind::Float) {
        return decodeFloat(bytes, h, stop);
    }
    const ImageBuffer image = ImageBuffer::create({h.width, h.height}, h.format);
    const bool wide = h.maxval > 255;
    const std::uint32_t outMax = wide ? 65535 : 255;
    const bool bitmap = h.kind == Kind::Bitmap;
    const bool alpha = h.depth == 2 || h.depth == 4;
    const auto scale = [&](std::uint32_t v) {
        if (bitmap) {
            return v != 0 ? 0U : outMax;  // in PBM, 1 is black
        }
        v = std::min(v, h.maxval);
        return (v * outMax + h.maxval / 2) / h.maxval;
    };
    // Binary samples: bytes, or 16-bit big-endian words above 255; PBM packs eight to a byte,
    // each row starting on a new byte.
    const std::size_t sampleBytes = wide ? 2 : 1;
    const std::size_t rowBytes = bitmap ? (static_cast<std::size_t>(h.width) + 7) / 8
                                        : static_cast<std::size_t>(h.width) * static_cast<std::size_t>(h.depth) * sampleBytes;
    std::size_t textAt = h.dataAt;
    bool textEnded = false;
    std::array<std::uint32_t, 4> sample{};
    for (int y = 0; y < h.height; ++y) {
        if (stop.stop_requested()) {
            return std::unexpected("cancelled");
        }
        const std::size_t rowAt = h.dataAt + static_cast<std::size_t>(y) * rowBytes;
        for (int x = 0; x < h.width; ++x) {
            for (int c = 0; c < h.depth; ++c) {
                std::uint32_t v = 0;
                if (h.text) {
                    if (!textEnded) {
                        const auto read = textSample(bytes, textAt, bitmap);
                        textEnded = !read;
                        v = read.value_or(0);
                    }
                } else if (bitmap) {
                    const std::size_t at = rowAt + static_cast<std::size_t>(x) / 8;
                    v = at < bytes.size() ? (bytes[at] >> (7 - x % 8)) & 1U : 0;
                } else {
                    const std::size_t at = rowAt + (static_cast<std::size_t>(x) * static_cast<std::size_t>(h.depth) + static_cast<std::size_t>(c)) * sampleBytes;
                    if (at < bytes.size() && bytes.size() - at >= sampleBytes) {
                        v = wide ? static_cast<std::uint32_t>(bytes[at] << 8 | bytes[at + 1]) : bytes[at];
                    }
                }
                sample[static_cast<std::size_t>(c)] = scale(v);
            }
            // Gray goes to all three colours where the picture has a fourth channel.
            const std::uint32_t r = sample[0];
            const std::uint32_t g = h.depth >= 3 ? sample[1] : sample[0];
            const std::uint32_t b = h.depth >= 3 ? sample[2] : sample[0];
            const std::uint32_t a = h.depth == 2 ? sample[1] : h.depth == 4 ? sample[3] : outMax;
            std::byte* row = image.row(y);
            switch (h.format) {
            case PixelFormat::Gray8:
                reinterpret_cast<std::uint8_t*>(row)[x] = static_cast<std::uint8_t>(r);
                break;
            case PixelFormat::Gray16:
                reinterpret_cast<std::uint16_t*>(row)[x] = static_cast<std::uint16_t>(r);
                break;
            case PixelFormat::Rgba16: {
                auto* p = reinterpret_cast<std::uint16_t*>(row) + static_cast<std::size_t>(x) * 4;
                p[0] = static_cast<std::uint16_t>(r);
                p[1] = static_cast<std::uint16_t>(g);
                p[2] = static_cast<std::uint16_t>(b);
                p[3] = static_cast<std::uint16_t>(a);
                break;
            }
            default: {
                auto* p = reinterpret_cast<std::uint8_t*>(row) + static_cast<std::size_t>(x) * 4;
                p[0] = static_cast<std::uint8_t>(b);
                p[1] = static_cast<std::uint8_t>(g);
                p[2] = static_cast<std::uint8_t>(r);
                p[3] = static_cast<std::uint8_t>(a);
                break;
            }
            }
        }
    }
    return image.withAlpha(alpha);
}

}  // namespace

bool isNetpbm(std::span<const std::uint8_t> header) {
    if (header.size() < 3 || header[0] != 'P' || !isSpace(header[2])) {
        return false;
    }
    const std::uint8_t type = header[1];
    return (type >= '1' && type <= '7') || type == 'F' || type == 'f';
}

Result<std::shared_ptr<ImageSource>> openNetpbmSource(std::vector<std::uint8_t> bytes) {
    if (!isNetpbm(bytes)) {
        return std::unexpected("not a Netpbm file");
    }
    auto header = readHeader(bytes);
    if (!header) {
        return std::unexpected(header.error());
    }
    const Size size{header->width, header->height};
    if (!fitsInMemoryLimit(size, header->format)) {
        return std::unexpected("image is too large");
    }

    SubimageInfo info;
    info.format = header->format;
    info.alpha = header->depth == 2 || header->depth == 4;
    StoredFormat& stored = info.stored;
    const Kind kind = header->kind;
    stored.container = kind == Kind::Bitmap ? "PBM" : kind == Kind::Gray ? "PGM" : kind == Kind::Colour ? "PPM"
                     : kind == Kind::Pam    ? "PAM" : "PFM";
    stored.compression = header->text ? "None, as text" : "None";
    stored.colorModel = header->depth <= 2 ? (kind == Kind::Bitmap ? "Black and white" : "Grayscale") : "RGB";
    if (info.alpha) {
        stored.colorModel += " + alpha";
    }
    if (kind == Kind::Float) {
        stored.bitsPerSample = 32;
        stored.floatSamples = true;
        info.iccProfile = makeRgbProfile("Linear sRGB", kSrgbPrimaries, 1.0);
    } else {
        stored.bitsPerSample = kind == Kind::Bitmap ? 1 : std::bit_width(header->maxval);
    }

    return makeWholeImageSource(std::move(info), size,
                                [bytes = std::move(bytes), h = *header](const std::stop_token& stop) -> Result<ImageBuffer> {
        try {
            return decode(bytes, h, stop);
        } catch (const std::bad_alloc&) {
            return std::unexpected("not enough memory to decode image");
        }
    });
}

}  // namespace iv
