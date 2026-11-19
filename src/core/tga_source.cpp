#include "core/tga_source.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <new>
#include <string_view>
#include <utility>

#include "core/byte_reader.h"
#include "core/decompress.h"
#include "core/whole_image_source.h"

namespace iv {

namespace {

constexpr std::size_t kHeaderSize = 18;
constexpr std::size_t kFooterSize = 26;
constexpr std::string_view kSignature{"TRUEVISION-XFILE.\0", 18};

// What the v2 extension area says the alpha channel holds.
enum class AlphaMeaning { Unknown, None, Straight, Premultiplied };

struct Header {
    std::size_t idLength = 0;
    bool hasMap = false;
    int type = 0;
    int mapFirst = 0;
    int mapLength = 0;
    int mapEntryBits = 0;
    int width = 0;
    int height = 0;
    int bits = 0;
    int alphaBits = 0;
    bool rightToLeft = false;
    bool topDown = false;

    bool rle() const { return type >= 9; }
    bool mapped() const { return type == 1 || type == 9; }
    bool gray() const { return type == 3 || type == 11; }
};

Header parse(std::span<const std::uint8_t> bytes) {
    ByteReader r(bytes);
    Header h;
    h.idLength = r.u8();
    h.hasMap = r.u8() == 1;
    h.type = r.u8();
    h.mapFirst = r.le16();
    h.mapLength = r.le16();
    h.mapEntryBits = r.u8();
    r.skip(4);  // the origin on screen
    h.width = r.le16();
    h.height = r.le16();
    h.bits = r.u8();
    const std::uint8_t descriptor = r.u8();
    h.alphaBits = descriptor & 0x0F;
    h.rightToLeft = (descriptor & 0x10) != 0;
    h.topDown = (descriptor & 0x20) != 0;
    return h;
}

bool valid(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderSize || bytes[1] > 1 || (bytes[17] & 0xC0) != 0) {
        return false;
    }
    const Header h = parse(bytes);
    if (h.width == 0 || h.height == 0) {
        return false;
    }
    const auto entryOk = [&] { return h.mapEntryBits == 15 || h.mapEntryBits == 16 || h.mapEntryBits == 24 || h.mapEntryBits == 32; };
    switch (h.type) {
    case 1:
    case 9:
        return h.hasMap && h.mapLength > 0 && entryOk() && (h.bits == 8 || h.bits == 16);
    case 2:
    case 10:
        return (!h.hasMap || entryOk()) && (h.bits == 15 || h.bits == 16 || h.bits == 24 || h.bits == 32) && h.alphaBits <= 8;
    case 3:
    case 11:
        return !h.hasMap && (h.bits == 8 || h.bits == 16) && (h.bits == 8 ? h.alphaBits == 0 : h.alphaBits == 8 || h.alphaBits == 0);
    default:
        return false;
    }
}

AlphaMeaning alphaMeaning(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderSize + kFooterSize) {
        return AlphaMeaning::Unknown;
    }
    const auto footer = bytes.last(kFooterSize);
    if (std::memcmp(footer.data() + 8, kSignature.data(), kSignature.size()) != 0) {
        return AlphaMeaning::Unknown;
    }
    ByteReader f(footer);
    const std::uint32_t extension = f.le32();
    ByteReader e(bytes, extension);
    if (extension == 0 || e.le16() < 495) {
        return AlphaMeaning::Unknown;
    }
    e.seek(extension + 494);
    const std::uint8_t type = e.u8();
    if (e.failed()) {
        return AlphaMeaning::Unknown;
    }
    switch (type) {
    case 3:
        return AlphaMeaning::Straight;
    case 4:
        return AlphaMeaning::Premultiplied;
    case 0:
    case 1:
    case 2:
        return AlphaMeaning::None;  // none, or undefined data that is not to be shown as alpha
    default:
        return AlphaMeaning::Unknown;
    }
}

// One stored pixel of 15, 16, 24 or 32 bits as red, green, blue, alpha.
std::array<std::uint8_t, 4> colourOf(const std::uint8_t* p, int bits) {
    if (bits == 15 || bits == 16) {
        const int v = p[0] | p[1] << 8;
        const auto five = [](int x) { return static_cast<std::uint8_t>((x << 3) | (x >> 2)); };
        return {five((v >> 10) & 31), five((v >> 5) & 31), five(v & 31), static_cast<std::uint8_t>((v & 0x8000) != 0 ? 255 : 0)};
    }
    return {p[2], p[1], p[0], bits == 32 ? p[3] : std::uint8_t{255}};
}

struct Plan {
    Header h;
    std::size_t dataAt = 0;
    std::vector<std::array<std::uint8_t, 4>> palette;
    bool alpha = false;
    bool premultiplied = false;
    PixelFormat format = PixelFormat::Bgrx8;
};

int pixelBytes(const Header& h) { return (h.bits + 7) / 8; }

// The stored pixels, unpacked from run-length packets where they are packed, in stored order.
std::vector<std::uint8_t> storedPixels(std::span<const std::uint8_t> bytes, const Plan& p) {
    const std::size_t size = static_cast<std::size_t>(p.h.width) * static_cast<std::size_t>(p.h.height) *
                             static_cast<std::size_t>(pixelBytes(p.h));
    std::vector<std::uint8_t> pixels(size, 0);
    const auto data = p.dataAt <= bytes.size() ? bytes.subspan(p.dataAt) : std::span<const std::uint8_t>{};
    if (p.h.rle()) {
        decodePacketRle(data, pixelBytes(p.h), pixels);
    } else {
        std::memcpy(pixels.data(), data.data(), std::min(size, data.size()));
    }
    return pixels;
}

std::array<std::uint8_t, 4> rgbaAt(const Plan& p, const std::uint8_t* px) {
    if (p.h.mapped()) {
        const int index = (p.h.bits == 16 ? px[0] | px[1] << 8 : px[0]) - p.h.mapFirst;
        return index >= 0 && static_cast<std::size_t>(index) < p.palette.size() ? p.palette[static_cast<std::size_t>(index)]
                                                                                : std::array<std::uint8_t, 4>{0, 0, 0, 255};
    }
    if (p.h.gray()) {
        return {px[0], px[0], px[0], p.h.bits == 16 ? px[1] : std::uint8_t{255}};
    }
    return colourOf(px, p.h.bits);
}

// Whether the alpha a file stores means anything: many leave it all 0, meaning nothing.
bool alphaIsUsed(const Plan& p, const std::vector<std::uint8_t>& pixels) {
    const std::size_t step = static_cast<std::size_t>(pixelBytes(p.h));
    bool anyVisible = false;
    bool anyNotOpaque = false;
    for (std::size_t at = 0; at + step <= pixels.size(); at += step) {
        const std::uint8_t a = rgbaAt(p, pixels.data() + at)[3];
        anyVisible = anyVisible || a != 0;
        anyNotOpaque = anyNotOpaque || a != 255;
        if (anyVisible && anyNotOpaque) {
            return true;
        }
    }
    return false;
}

Result<ImageBuffer> decode(std::span<const std::uint8_t> bytes, const Plan& p, const std::stop_token& stop) {
    const std::vector<std::uint8_t> pixels = storedPixels(bytes, p);
    const bool grayOut = p.format == PixelFormat::Gray8;
    const ImageBuffer image = ImageBuffer::create({p.h.width, p.h.height}, p.format);
    const std::size_t step = static_cast<std::size_t>(pixelBytes(p.h));
    for (int stored = 0; stored < p.h.height; ++stored) {
        if (stop.stop_requested()) {
            return std::unexpected("cancelled");
        }
        auto* row = reinterpret_cast<std::uint8_t*>(image.row(p.h.topDown ? stored : p.h.height - 1 - stored));
        const std::uint8_t* in = pixels.data() + static_cast<std::size_t>(stored) * static_cast<std::size_t>(p.h.width) * step;
        for (int sx = 0; sx < p.h.width; ++sx) {
            const int x = p.h.rightToLeft ? p.h.width - 1 - sx : sx;
            auto c = rgbaAt(p, in + static_cast<std::size_t>(sx) * step);
            if (grayOut) {
                row[x] = c[0];
                continue;
            }
            if (!p.alpha) {
                c[3] = 255;
            } else if (p.premultiplied && c[3] != 0) {
                for (std::size_t i = 0; i < 3; ++i) {
                    c[i] = static_cast<std::uint8_t>(std::min(255, (c[i] * 255 + c[3] / 2) / c[3]));
                }
            }
            std::uint8_t* out = row + static_cast<std::size_t>(x) * 4;
            out[0] = c[2];
            out[1] = c[1];
            out[2] = c[0];
            out[3] = c[3];
        }
    }
    return image.withAlpha(p.alpha);
}

}  // namespace

bool isTga(std::span<const std::uint8_t> header) { return valid(header); }

Result<std::shared_ptr<ImageSource>> openTgaSource(std::vector<std::uint8_t> bytes) {
    if (!valid(bytes)) {
        return std::unexpected("not a TGA file");
    }
    Plan p;
    p.h = parse(bytes);
    std::size_t at = kHeaderSize + p.h.idLength;
    if (p.h.hasMap) {
        const std::size_t entryBytes = static_cast<std::size_t>((p.h.mapEntryBits + 7) / 8);
        ByteReader m(bytes, at);
        for (int i = 0; i < p.h.mapLength; ++i) {
            const auto entry = m.bytes(entryBytes);
            if (m.failed()) {
                return std::unexpected("truncated TGA colour map");
            }
            if (p.h.mapped()) {
                p.palette.push_back(colourOf(entry.data(), p.h.mapEntryBits));
            }
        }
        at += entryBytes * static_cast<std::size_t>(p.h.mapLength);
    }
    p.dataAt = at;

    // Alpha: what the footer says, else the descriptor's alpha bits, or a 32-bit palette.
    const AlphaMeaning meaning = alphaMeaning(bytes);
    const bool stores = p.h.mapped() ? p.h.mapEntryBits == 32 || (p.h.mapEntryBits == 16 && p.h.alphaBits == 1)
                                     : p.h.alphaBits > 0 && (p.h.bits == 32 || p.h.bits == 16);
    p.alpha = stores && meaning != AlphaMeaning::None;
    p.premultiplied = meaning == AlphaMeaning::Premultiplied;
    if (p.h.mapped() && p.h.mapEntryBits == 16 && p.h.alphaBits == 0) {
        for (auto& c : p.palette) {
            c[3] = 255;
        }
    }
    const Size size{p.h.width, p.h.height};
    if (!fitsInMemoryLimit(size, PixelFormat::Bgrx8)) {
        return std::unexpected("image is too large");
    }
    // Settled now, so that what the source promises is what decoding gives.
    if (p.alpha) {
        try {
            p.alpha = alphaIsUsed(p, storedPixels(bytes, p));
        } catch (const std::bad_alloc&) {
            return std::unexpected("not enough memory to decode image");
        }
    }
    p.format = p.h.gray() && !p.alpha ? PixelFormat::Gray8 : PixelFormat::Bgrx8;

    SubimageInfo info;
    info.format = p.format;
    info.alpha = p.alpha;
    StoredFormat& stored = info.stored;
    stored.container = "TGA";
    stored.compression = p.h.rle() ? "RLE, lossless" : "None";
    stored.colorModel = p.h.mapped() ? "Palette" : p.h.gray() ? "Grayscale" : "RGB";
    if (p.alpha) {
        stored.colorModel += p.premultiplied ? " + premultiplied alpha" : " + alpha";
    }
    stored.bitsPerSample = p.h.mapped() ? p.h.bits : p.h.gray() || p.h.bits >= 24 ? 8 : 5;

    return makeWholeImageSource(std::move(info), size, [bytes = std::move(bytes), p = std::move(p)](const std::stop_token& stop) -> Result<ImageBuffer> {
        try {
            return decode(bytes, p, stop);
        } catch (const std::bad_alloc&) {
            return std::unexpected("not enough memory to decode image");
        }
    });
}

}  // namespace iv
