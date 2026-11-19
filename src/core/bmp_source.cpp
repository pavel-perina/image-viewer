#include "core/bmp_source.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#include "core/byte_reader.h"
#include "core/decompress.h"
#include "core/whole_image_source.h"

namespace iv {

namespace {

enum Compression : std::uint32_t {
    Rgb = 0,
    Rle8 = 1,
    Rle4 = 2,
    BitFields = 3,
    Jpeg = 4,
    Png = 5,
    AlphaBitFields = 6,
};

constexpr std::uint32_t kEmbeddedProfile = 0x4D424544;  // 'MBED'

// One channel of a pixel as a bit field: where it is and how wide.
struct Field {
    std::uint32_t mask = 0;
    int shift = 0;
    int bits = 0;

    Field() = default;
    explicit Field(std::uint32_t m) : mask(m) {
        if (mask != 0) {
            shift = std::countr_zero(mask);
            bits = std::popcount(mask);
        }
    }
    // Contiguous, as a mask of scattered bits means nothing sensible.
    bool valid() const { return mask == 0 || bits == 32 || (mask >> shift) == (1U << bits) - 1U; }
    // The value brought to 0..max.
    std::uint32_t scaled(std::uint32_t pixel, std::uint32_t max) const {
        if (bits == 0) {
            return 0;
        }
        const std::uint64_t top = bits >= 32 ? 0xFFFFFFFFULL : (1ULL << bits) - 1;
        const std::uint64_t value = (pixel & mask) >> shift;
        return static_cast<std::uint32_t>((value * max + top / 2) / top);
    }
};

struct Header {
    int width = 0;
    int height = 0;
    bool topDown = false;
    int bits = 0;
    std::uint32_t compression = Rgb;
    std::size_t pixelOffset = 0;
    std::vector<std::array<std::uint8_t, 3>> palette;  // red, green, blue
    std::array<Field, 4> fields{};                     // red, green, blue, alpha
    bool alpha = false;
    bool gray = false;  // a palette of grays only
    PixelFormat format = PixelFormat::Bgrx8;
    std::vector<std::uint8_t> iccProfile;
};

std::size_t rowBytes(int width, int bits) {
    return (static_cast<std::size_t>(width) * static_cast<std::size_t>(bits) + 31) / 32 * 4;
}

std::uint32_t pixelAt(const std::uint8_t* row, int x, int bits) {
    const std::uint8_t* p = row + static_cast<std::size_t>(x) * static_cast<std::size_t>(bits / 8);
    return bits == 16 ? static_cast<std::uint32_t>(p[0] | p[1] << 8)
                      : static_cast<std::uint32_t>(p[0] | p[1] << 8 | p[2] << 16 | static_cast<std::uint32_t>(p[3]) << 24);
}

// Whether the alpha a 16- or 32-bit picture stores means anything: many files leave it all 0
// (or all opaque), and those are shown opaque.
bool alphaIsUsed(std::span<const std::uint8_t> bytes, const Header& h) {
    const Field& a = h.fields[3];
    if (a.bits == 0) {
        return false;
    }
    const std::size_t stride = rowBytes(h.width, h.bits);
    bool anyVisible = false;
    bool anyNotOpaque = false;
    const std::uint32_t full = a.bits >= 32 ? 0xFFFFFFFFU : (1U << a.bits) - 1U;
    for (int y = 0; y < h.height; ++y) {
        const std::size_t at = h.pixelOffset + static_cast<std::size_t>(y) * stride;
        if (at > bytes.size() || stride > bytes.size() - at) {
            break;
        }
        for (int x = 0; x < h.width; ++x) {
            const std::uint32_t value = (pixelAt(bytes.data() + at, x, h.bits) & a.mask) >> a.shift;
            anyVisible = anyVisible || value != 0;
            anyNotOpaque = anyNotOpaque || value != full;
            if (anyVisible && anyNotOpaque) {
                return true;
            }
        }
    }
    return false;
}

Result<Header> readHeader(std::span<const std::uint8_t> bytes) {
    ByteReader r(bytes, 10);
    Header h;
    h.pixelOffset = r.le32();
    const std::uint32_t headerSize = r.le32();
    const bool core = headerSize == 12;  // OS/2 1.x: 16-bit sizes, palette entries of 3 bytes
    if (core) {
        h.width = r.le16();
        h.height = static_cast<std::int16_t>(r.le16());
        r.le16();  // planes
        h.bits = r.le16();
    } else if (headerSize >= 16 && headerSize <= 124) {
        h.width = r.le32signed();
        h.height = r.le32signed();
        r.le16();
        h.bits = r.le16();
        if (headerSize >= 20) {
            h.compression = r.le32();
        }
    } else {
        return std::unexpected("unknown BMP header");
    }
    if (r.failed()) {
        return std::unexpected("truncated BMP header");
    }
    // OS/2 2.x numbers its compressions differently past RLE4, and nobody writes those.
    if (headerSize == 64 && h.compression > Rle4) {
        return std::unexpected("unsupported OS/2 BMP compression");
    }
    if (h.compression == Jpeg || h.compression == Png) {
        return std::unexpected("BMP holding a JPEG or PNG is not supported");
    }
    if (h.compression > AlphaBitFields) {
        return std::unexpected("unsupported BMP compression");
    }
    if (h.height == std::numeric_limits<int>::min() || h.width <= 0 || h.height == 0) {
        return std::unexpected("invalid BMP size");
    }
    h.topDown = h.height < 0;
    h.height = std::abs(h.height);
    const bool paletted = h.bits == 1 || h.bits == 4 || h.bits == 8;
    if (!paletted && h.bits != 16 && h.bits != 24 && h.bits != 32) {
        return std::unexpected("unsupported BMP depth");
    }
    if ((h.compression == Rle8 && h.bits != 8) || (h.compression == Rle4 && h.bits != 4) ||
        ((h.compression == BitFields || h.compression == AlphaBitFields) && h.bits != 16 && h.bits != 32)) {
        return std::unexpected("BMP compression does not suit its depth");
    }

    const std::size_t headerStart = 14;
    const std::size_t afterHeader = headerStart + headerSize;
    // Masks: inside a header of 52 bytes or more, otherwise after a 40-byte one.
    std::size_t maskAt = 0;
    int maskCount = 0;
    if (h.compression == BitFields || h.compression == AlphaBitFields) {
        maskAt = headerSize >= 52 ? headerStart + 40 : afterHeader;
        maskCount = headerSize >= 56 || h.compression == AlphaBitFields ? 4 : 3;
    } else if (headerSize >= 56 && (h.bits == 16 || h.bits == 32)) {
        maskAt = headerStart + 52;  // only the alpha mask counts without bit fields
    }
    std::array<std::uint32_t, 4> masks{};
    if (h.bits == 16) {
        masks = {0x7C00, 0x03E0, 0x001F, 0};
    } else if (h.bits == 32) {
        masks = {0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000};  // the fourth byte, if used
    }
    if (maskCount > 0) {
        ByteReader m(bytes, maskAt);
        for (int i = 0; i < maskCount; ++i) {
            masks[static_cast<std::size_t>(i)] = m.le32();
        }
        if (maskCount == 3) {
            masks[3] = 0;
        }
        if (m.failed()) {
            return std::unexpected("truncated BMP bit fields");
        }
    } else if (maskAt != 0) {
        // A later header's alpha mask, even without bit fields; 0 keeps the fourth byte a
        // candidate, which is used only if it varies.
        ByteReader m(bytes, maskAt);
        if (const std::uint32_t alpha = m.le32(); alpha != 0) {
            masks[3] = alpha;
        }
    }
    for (std::size_t i = 0; i < 4; ++i) {
        h.fields[i] = Field(masks[i]);
        if (!h.fields[i].valid()) {
            return std::unexpected("invalid BMP bit fields");
        }
    }

    if (paletted) {
        ByteReader c(bytes, headerStart + 32);
        const std::uint32_t used = headerSize >= 36 ? c.le32() : 0;
        const std::size_t entries = std::min<std::size_t>(used != 0 ? used : (1U << h.bits), 256);
        const std::size_t entryBytes = core ? 3 : 4;
        // The palette ends where the pixels begin, whatever the header counts.
        const std::size_t room = h.pixelOffset > afterHeader ? (h.pixelOffset - afterHeader) / entryBytes : 0;
        ByteReader p(bytes, afterHeader);
        for (std::size_t i = 0; i < std::min(entries, room); ++i) {
            const auto entry = p.bytes(entryBytes);
            if (p.failed()) {
                break;  // a short palette: the rest stays black
            }
            h.palette.push_back({entry[2], entry[1], entry[0]});
        }
        const std::size_t size = std::size_t{1} << h.bits;
        if (h.palette.empty()) {
            // Some writers of gray pictures leave the palette out and mean a ramp of grays.
            for (std::size_t i = 0; i < size; ++i) {
                const auto v = static_cast<std::uint8_t>(i * 255 / (size - 1));
                h.palette.push_back({v, v, v});
            }
        }
        h.palette.resize(size, {0, 0, 0});
        h.gray = std::ranges::all_of(h.palette, [](const auto& c) { return c[0] == c[1] && c[1] == c[2]; });
        h.fields = {};
    }

    if (headerSize >= 124) {
        ByteReader v(bytes, headerStart + 56);
        const std::uint32_t space = v.le32();
        ByteReader q(bytes, headerStart + 112);
        const std::uint32_t profileAt = q.le32();
        const std::uint32_t profileSize = q.le32();
        const std::size_t at = headerStart + profileAt;
        if (space == kEmbeddedProfile && !v.failed() && !q.failed() && at <= bytes.size() &&
            profileSize <= bytes.size() - at && profileSize > 0) {
            h.iccProfile.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                                bytes.begin() + static_cast<std::ptrdiff_t>(at + profileSize));
        }
    }

    h.alpha = !paletted && alphaIsUsed(bytes, h);
    if (!h.alpha) {
        h.fields[3] = Field();
    }
    const bool wide = std::ranges::any_of(h.fields, [](const Field& f) { return f.bits > 8; });
    h.format = h.gray && !h.alpha ? PixelFormat::Gray8 : wide ? PixelFormat::Rgba16 : PixelFormat::Bgrx8;
    return h;
}

void writePixel(const ImageBuffer& image, int x, int y, const std::array<std::uint32_t, 4>& rgba) {
    std::byte* row = image.row(y);
    switch (image.format()) {
    case PixelFormat::Gray8:
        reinterpret_cast<std::uint8_t*>(row)[x] = static_cast<std::uint8_t>(rgba[0]);
        break;
    case PixelFormat::Rgba16: {
        auto* p = reinterpret_cast<std::uint16_t*>(row) + static_cast<std::size_t>(x) * 4;
        for (std::size_t i = 0; i < 4; ++i) {
            p[i] = static_cast<std::uint16_t>(rgba[i]);
        }
        break;
    }
    default: {
        auto* p = reinterpret_cast<std::uint8_t*>(row) + static_cast<std::size_t>(x) * 4;
        p[0] = static_cast<std::uint8_t>(rgba[2]);
        p[1] = static_cast<std::uint8_t>(rgba[1]);
        p[2] = static_cast<std::uint8_t>(rgba[0]);
        p[3] = static_cast<std::uint8_t>(rgba[3]);
        break;
    }
    }
}

Result<ImageBuffer> decode(std::span<const std::uint8_t> bytes, const Header& h, const std::stop_token& stop) {
    const ImageBuffer image = ImageBuffer::create({h.width, h.height}, h.format);
    const std::uint32_t max = h.format == PixelFormat::Rgba16 ? 65535 : 255;
    const std::span<const std::uint8_t> data = h.pixelOffset <= bytes.size() ? bytes.subspan(h.pixelOffset) : std::span<const std::uint8_t>{};
    // Stored rows count from the bottom unless the height was negative.
    const auto target = [&](int stored) { return h.topDown ? stored : h.height - 1 - stored; };
    const auto colour = [&](std::uint8_t index) {
        const auto& c = h.palette[index];
        return std::array<std::uint32_t, 4>{c[0], c[1], c[2], 255};
    };

    if (h.compression == Rle8 || h.compression == Rle4) {
        const std::size_t pixels = static_cast<std::size_t>(h.width) * static_cast<std::size_t>(h.height);
        std::vector<std::uint8_t> indices(pixels, 0);
        std::vector<std::uint8_t> covered(pixels, 0);
        decodeBmpRle(data, h.bits, h.width, h.height, indices, covered);
        for (int y = 0; y < h.height; ++y) {
            for (int x = 0; x < h.width; ++x) {
                writePixel(image, x, target(y), colour(indices[static_cast<std::size_t>(y) * static_cast<std::size_t>(h.width) + static_cast<std::size_t>(x)]));
            }
        }
        return image.withAlpha(false);
    }

    const std::size_t stride = rowBytes(h.width, h.bits);
    std::vector<std::uint8_t> padded(stride, 0);
    for (int y = 0; y < h.height; ++y) {
        if (stop.stop_requested()) {
            return std::unexpected("cancelled");
        }
        const std::size_t at = static_cast<std::size_t>(y) * stride;
        // A truncated file leaves the rows it lacks black.
        std::ranges::fill(padded, std::uint8_t{0});
        if (at < data.size()) {
            std::memcpy(padded.data(), data.data() + at, std::min(stride, data.size() - at));
        }
        const std::uint8_t* row = padded.data();
        const int out = target(y);
        for (int x = 0; x < h.width; ++x) {
            if (h.bits <= 8) {
                const int perByte = 8 / h.bits;
                const int shift = 8 - h.bits * (x % perByte + 1);
                const auto index = static_cast<std::uint8_t>((row[x / perByte] >> shift) & ((1 << h.bits) - 1));
                writePixel(image, x, out, colour(index));
            } else if (h.bits == 24) {
                const std::uint8_t* p = row + static_cast<std::size_t>(x) * 3;
                writePixel(image, x, out, {p[2], p[1], p[0], 255});
            } else {
                const std::uint32_t pixel = pixelAt(row, x, h.bits);
                writePixel(image, x, out,
                           {h.fields[0].scaled(pixel, max), h.fields[1].scaled(pixel, max), h.fields[2].scaled(pixel, max),
                            h.alpha ? h.fields[3].scaled(pixel, max) : max});
            }
        }
    }
    return image.withAlpha(h.alpha);
}

}  // namespace

bool isBmp(std::span<const std::uint8_t> header) {
    return header.size() >= 2 && header[0] == 'B' && header[1] == 'M';
}

Result<std::shared_ptr<ImageSource>> openBmpSource(std::vector<std::uint8_t> bytes) {
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
    info.alpha = header->alpha;
    info.iccProfile = header->iccProfile;
    StoredFormat& stored = info.stored;
    stored.container = "BMP";
    const bool paletted = header->bits <= 8;
    stored.compression = header->compression == Rle8 || header->compression == Rle4 ? "RLE, lossless" : "None";
    stored.colorModel = paletted ? (header->gray ? "Grayscale palette" : "Palette") : "RGB";
    if (header->alpha) {
        stored.colorModel += " + alpha";
    }
    int widest = 0;
    for (const Field& f : header->fields) {
        widest = std::max(widest, f.bits);
    }
    stored.bitsPerSample = paletted ? header->bits : header->bits == 24 ? 8 : widest;

    return makeWholeImageSource(std::move(info), size,
                                [bytes = std::move(bytes), h = std::move(*header)](const std::stop_token& stop) -> Result<ImageBuffer> {
        try {
            return decode(bytes, h, stop);
        } catch (const std::bad_alloc&) {
            return std::unexpected("not enough memory to decode image");
        }
    });
}

}  // namespace iv
