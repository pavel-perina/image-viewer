#include "core/gif_source.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <new>
#include <optional>
#include <utility>

#include "core/byte_reader.h"
#include "core/decompress.h"
#include "core/whole_image_source.h"

namespace iv {

namespace {

using Palette = std::vector<std::array<std::uint8_t, 3>>;

struct Frame {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    bool interlaced = false;
    int minCodeSize = 0;
    Palette palette;
    std::optional<std::uint8_t> transparent;
    int delay = 0;  // hundredths of a second
    std::vector<std::uint8_t> data;  // the LZW data, its sub-blocks joined
};

struct Gif {
    int width = 0;
    int height = 0;
    Frame first;
    bool animated = false;
    bool alpha = false;
    bool gray = false;
};

Palette readPalette(ByteReader& r, int sizeBits) {
    Palette palette;
    const int entries = 1 << (sizeBits + 1);
    for (int i = 0; i < entries; ++i) {
        const auto c = r.bytes(3);
        if (r.failed()) {
            break;
        }
        palette.push_back({c[0], c[1], c[2]});
    }
    return palette;
}

// Data sub-blocks: a length byte, that many bytes, until a length of 0.
void readSubBlocks(ByteReader& r, std::vector<std::uint8_t>* into) {
    for (;;) {
        const std::uint8_t length = r.u8();
        if (length == 0 || r.failed()) {
            return;
        }
        const auto block = r.bytes(length);
        if (into != nullptr) {
            into->insert(into->end(), block.begin(), block.end());
        }
    }
}

Result<Gif> parse(std::span<const std::uint8_t> bytes) {
    ByteReader r(bytes, 6);
    Gif gif;
    gif.width = r.le16();
    gif.height = r.le16();
    const std::uint8_t flags = r.u8();
    r.skip(2);  // background index, aspect ratio
    if (r.failed() || gif.width == 0 || gif.height == 0) {
        return std::unexpected("invalid GIF header");
    }
    Palette global;
    if ((flags & 0x80) != 0) {
        global = readPalette(r, flags & 7);
    }

    std::optional<std::uint8_t> transparent;
    int delay = 0;
    bool haveFirst = false;
    while (!r.failed()) {
        const std::uint8_t kind = r.u8();
        if (kind == 0x3B || r.failed()) {
            break;  // the trailer, or the file ends without one
        }
        if (kind == 0x21) {
            const std::uint8_t label = r.u8();
            if (label == 0xF9 && !haveFirst) {
                std::vector<std::uint8_t> control;
                readSubBlocks(r, &control);
                if (control.size() >= 4) {
                    delay = control[1] | control[2] << 8;
                    transparent = (control[0] & 1) != 0 ? std::optional<std::uint8_t>(control[3]) : std::nullopt;
                }
            } else {
                readSubBlocks(r, nullptr);
            }
            continue;
        }
        if (kind != 0x2C) {
            if (haveFirst) {
                break;  // stray bytes after a whole first frame: show it
            }
            return std::unexpected("damaged GIF block");
        }
        if (haveFirst) {
            gif.animated = true;  // a second frame; nothing more is needed of it
            break;
        }
        Frame& f = gif.first;
        f.left = r.le16();
        f.top = r.le16();
        f.width = r.le16();
        f.height = r.le16();
        const std::uint8_t frameFlags = r.u8();
        f.interlaced = (frameFlags & 0x40) != 0;
        f.palette = (frameFlags & 0x80) != 0 ? readPalette(r, frameFlags & 7) : global;
        f.minCodeSize = r.u8();
        readSubBlocks(r, &f.data);
        f.transparent = transparent;
        f.delay = delay;
        if (r.failed() && f.data.empty()) {
            return std::unexpected("truncated GIF");
        }
        haveFirst = true;
    }
    if (!haveFirst) {
        return std::unexpected("GIF has no picture");
    }
    Frame& f = gif.first;
    if (f.palette.empty()) {
        return std::unexpected("GIF has no colour table");
    }
    if (f.width == 0 || f.height == 0 || f.minCodeSize < 2 || f.minCodeSize > 8) {
        return std::unexpected("invalid GIF frame");
    }
    f.palette.resize(256, {0, 0, 0});
    const bool covers = f.left == 0 && f.top == 0 && f.width >= gif.width && f.height >= gif.height;
    gif.alpha = f.transparent.has_value() || !covers;
    gif.gray = std::ranges::all_of(f.palette, [](const auto& c) { return c[0] == c[1] && c[1] == c[2]; });
    return gif;
}

// The picture row a decoded row goes to: interlaced frames come in four passes.
int rowOf(int decoded, int height, bool interlaced) {
    if (!interlaced) {
        return decoded;
    }
    constexpr std::array<std::pair<int, int>, 4> kPasses{{{0, 8}, {4, 8}, {2, 4}, {1, 2}}};
    for (const auto [start, step] : kPasses) {
        const int rows = start < height ? (height - start + step - 1) / step : 0;
        if (decoded < rows) {
            return start + decoded * step;
        }
        decoded -= rows;
    }
    return -1;
}

Result<ImageBuffer> decode(const Gif& gif, const std::stop_token& stop) {
    const Frame& f = gif.first;
    const std::size_t count = static_cast<std::size_t>(f.width) * static_cast<std::size_t>(f.height);
    std::vector<std::uint8_t> indices(count, 0);
    const std::size_t decoded = decodeGifLzw(f.data, f.minCodeSize, indices);
    if (stop.stop_requested()) {
        return std::unexpected("cancelled");
    }
    const bool grayOut = gif.gray && !gif.alpha;
    const ImageBuffer image = ImageBuffer::create({gif.width, gif.height}, grayOut ? PixelFormat::Gray8 : PixelFormat::Bgrx8);
    for (int y = 0; y < gif.height; ++y) {
        std::memset(image.row(y), 0, static_cast<std::size_t>(gif.width) * (grayOut ? 1 : 4));
    }
    for (int row = 0; row < f.height; ++row) {
        const int y = f.top + rowOf(row, f.height, f.interlaced);
        if (y < 0 || y >= gif.height) {
            continue;
        }
        auto* out = reinterpret_cast<std::uint8_t*>(image.row(y));
        for (int col = 0; col < f.width; ++col) {
            const int x = f.left + col;
            const std::size_t at = static_cast<std::size_t>(row) * static_cast<std::size_t>(f.width) + static_cast<std::size_t>(col);
            if (x >= gif.width) {
                break;
            }
            // What a short file lacks stays transparent, or the first colour without alpha.
            const bool missing = at >= decoded;
            const std::uint8_t index = indices[at];
            const auto& c = f.palette[index];
            if (grayOut) {
                out[x] = c[0];
                continue;
            }
            const bool clear = (missing && gif.alpha) || (!missing && f.transparent == index);
            std::uint8_t* p = out + static_cast<std::size_t>(x) * 4;
            if (!clear) {
                p[0] = c[2];
                p[1] = c[1];
                p[2] = c[0];
                p[3] = 255;
            }
        }
    }
    if (!gif.alpha && !grayOut) {
        for (int y = 0; y < gif.height; ++y) {
            auto* out = reinterpret_cast<std::uint8_t*>(image.row(y));
            for (int x = 0; x < gif.width; ++x) {
                out[static_cast<std::size_t>(x) * 4 + 3] = 255;
            }
        }
    }
    return image.withAlpha(gif.alpha);
}

}  // namespace

bool isGif(std::span<const std::uint8_t> header) {
    return header.size() >= 6 && std::memcmp(header.data(), "GIF8", 4) == 0 && (header[4] == '7' || header[4] == '9') &&
           header[5] == 'a';
}

Result<std::shared_ptr<ImageSource>> openGifSource(std::vector<std::uint8_t> bytes) {
    if (!isGif(bytes)) {
        return std::unexpected("not a GIF file");
    }
    auto gif = parse(bytes);
    if (!gif) {
        return std::unexpected(gif.error());
    }
    const PixelFormat format = gif->gray && !gif->alpha ? PixelFormat::Gray8 : PixelFormat::Bgrx8;
    const Size size{gif->width, gif->height};
    if (!fitsInMemoryLimit(size, format) ||
        !fitsInMemoryLimit({gif->first.width, gif->first.height}, PixelFormat::Gray8)) {
        return std::unexpected("image is too large");
    }

    SubimageInfo info;
    info.format = format;
    info.alpha = gif->alpha;
    info.frameDelay = std::chrono::milliseconds(gif->first.delay * 10);
    StoredFormat& stored = info.stored;
    stored.container = "GIF";
    stored.compression = "LZW, lossless";
    stored.colorModel = gif->gray ? "Grayscale palette" : "Palette";
    if (gif->first.transparent) {
        stored.colorModel += " + transparency";
    }
    stored.bitsPerSample = 8;
    stored.animated = gif->animated;
    // The file is no longer needed: the frame's data was copied out of it.
    return makeWholeImageSource(std::move(info), size, [gif = std::move(*gif)](const std::stop_token& stop) -> Result<ImageBuffer> {
        try {
            return decode(gif, stop);
        } catch (const std::bad_alloc&) {
            return std::unexpected("not enough memory to decode image");
        }
    });
}

}  // namespace iv
