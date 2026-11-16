#include "core/png_source.h"

#include <png.h>

#include <algorithm>
#include <array>
#include <bit>
#include <csetjmp>
#include <cstring>
#include <new>
#include <string>
#include <utility>

#include "core/color.h"
#include "core/exif.h"
#include "core/image_ops.h"
#include "core/whole_image_source.h"

namespace iv {

namespace {

constexpr std::array<std::uint8_t, 8> kSignature{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
constexpr png_uint_32 kMaxDimension = 1u << 30;

struct MemoryReader {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t position;
};

void readFromMemory(png_structp png, png_bytep out, png_size_t length) {
    auto* reader = static_cast<MemoryReader*>(png_get_io_ptr(png));
    if (length > reader->size - reader->position) {
        png_error(png, "file is truncated");
    }
    std::memcpy(out, reader->data + reader->position, length);
    reader->position += length;
}

struct ErrorState {
    char message[256] = {};
};

[[noreturn]] void onError(png_structp png, png_const_charp message) {
    auto* state = static_cast<ErrorState*>(png_get_error_ptr(png));
    std::strncpy(state->message, message, sizeof(state->message) - 1);
    png_longjmp(png, 1);
}

void onWarning(png_structp, png_const_charp) {}

// libpng reports errors with longjmp, so the functions calling into it set a jump target and
// keep only trivially destructible locals.
class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : m_memory{bytes.data(), bytes.size(), 0} {
        m_png = png_create_read_struct(PNG_LIBPNG_VER_STRING, &m_errors, onError, onWarning);
        if (!m_png) {
            throw std::bad_alloc();
        }
        m_info = png_create_info_struct(m_png);
        if (!m_info) {
            png_destroy_read_struct(&m_png, nullptr, nullptr);
            throw std::bad_alloc();
        }
    }
    ~Reader() { png_destroy_read_struct(&m_png, &m_info, nullptr); }
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    const char* error() const { return m_errors.message[0] ? m_errors.message : "cannot read PNG image"; }
    png_structp png() const { return m_png; }
    png_infop info() const { return m_info; }

    bool readInfo() {
        if (setjmp(png_jmpbuf(m_png))) {
            return false;
        }
        png_set_read_fn(m_png, &m_memory, readFromMemory);
        png_set_user_limits(m_png, kMaxDimension, kMaxDimension);
        png_read_info(m_png, m_info);
        return true;
    }

    // Returns false on error, or with stopped set when the stop token fired.
    bool readPixels(const ImageBuffer& out, const std::stop_token& stop, bool& stopped) {
        if (setjmp(png_jmpbuf(m_png))) {
            return false;
        }
        const png_byte colorType = png_get_color_type(m_png, m_info);
        const png_byte depth = png_get_bit_depth(m_png, m_info);
        if (colorType == PNG_COLOR_TYPE_PALETTE) {
            png_set_palette_to_rgb(m_png);
        }
        if ((colorType & PNG_COLOR_MASK_COLOR) == 0 && depth < 8) {
            png_set_expand_gray_1_2_4_to_8(m_png);
        }
        if (png_get_valid(m_png, m_info, PNG_INFO_tRNS)) {
            png_set_tRNS_to_alpha(m_png);
        }
        if (depth == 16 && std::endian::native == std::endian::little) {
            png_set_swap(m_png);
        }
        if (out.format() != PixelFormat::Gray16 && out.format() != PixelFormat::Gray8) {
            if ((colorType & PNG_COLOR_MASK_COLOR) == 0) {
                png_set_gray_to_rgb(m_png);
            }
            png_set_add_alpha(m_png, depth == 16 ? 0xFFFF : 0xFF, PNG_FILLER_AFTER);  // only if it has none
            if (out.format() == PixelFormat::Bgrx8) {
                png_set_bgr(m_png);
            }
        }
        const int passes = png_set_interlace_handling(m_png);
        png_read_update_info(m_png, m_info);
        if (png_get_rowbytes(m_png, m_info) > out.stride()) {
            png_error(m_png, "unexpected row size");
        }
        for (int pass = 0; pass < passes; ++pass) {
            for (int y = 0; y < out.height(); ++y) {
                if (stop.stop_requested()) {
                    stopped = true;
                    return false;
                }
                png_read_row(m_png, reinterpret_cast<png_bytep>(out.row(y)), nullptr);
            }
        }
        return true;
    }

private:
    ErrorState m_errors;
    MemoryReader m_memory;
    png_structp m_png = nullptr;
    png_infop m_info = nullptr;
};

// The packet of an iTXt chunk with the XMP keyword. The XMP specification has it uncompressed,
// so a compressed one is left out.
void readXmp(std::span<const std::uint8_t> chunk, std::vector<std::uint8_t>& xmp) {
    constexpr std::string_view kKeyword{"XML:com.adobe.xmp\0", 18};
    if (chunk.size() < kKeyword.size() + 2 || !std::equal(kKeyword.begin(), kKeyword.end(), chunk.begin()) ||
        chunk[kKeyword.size()] != 0) {
        return;
    }
    // After the flag and the method come a language tag and a translated keyword, each ending in NUL.
    auto at = chunk.begin() + static_cast<std::ptrdiff_t>(kKeyword.size()) + 2;
    for (int i = 0; i < 2; ++i) {
        at = std::find(at, chunk.end(), std::uint8_t{0});
        if (at == chunk.end()) {
            return;
        }
        ++at;
    }
    xmp.assign(at, chunk.end());
}

// libpng stops at the pixels, while EXIF may follow them, and it does not know about APNG.
void readExtraChunks(const std::vector<std::uint8_t>& bytes, SubimageInfo& info) {
    std::size_t at = kSignature.size();
    while (bytes.size() >= 12 && at <= bytes.size() - 12) {
        const std::size_t length = static_cast<std::size_t>(bytes[at]) << 24 | bytes[at + 1] << 16 | bytes[at + 2] << 8 | bytes[at + 3];
        const auto type = std::string_view(reinterpret_cast<const char*>(bytes.data()) + at + 4, 4);
        if (length > bytes.size() - at - 12) {
            return;
        }
        if (type == "acTL") {
            info.stored.animated = true;
        } else if (type == "eXIf") {
            const auto data = std::span(bytes).subspan(at + 8, length);
            info.exif = parseExif(data);
            info.orientation = info.exif.orientation;
            const auto tiff = exifTiffStructure(data);
            info.rawExif.assign(tiff.begin(), tiff.end());
        } else if (type == "iTXt" && info.xmp.empty()) {
            readXmp(std::span(bytes).subspan(at + 8, length), info.xmp);
        } else if (type == "IEND") {
            return;
        }
        at += length + 12;
    }
}

Result<ImageBuffer> decode(const std::vector<std::uint8_t>& bytes, PixelFormat format, Size size, bool hasAlpha,
                           const std::stop_token& stop) {
    Reader reader(bytes);
    if (!reader.readInfo()) {
        return std::unexpected(reader.error());
    }
    const ImageBuffer image = ImageBuffer::create(size, format);
    bool stopped = false;
    if (!reader.readPixels(image, stop, stopped)) {
        return std::unexpected(stopped ? "cancelled" : reader.error());
    }
    return image.withAlpha(hasAlpha);
}

// In the order the PNG specification gives them precedence.
IccBytes describedProfile(png_structp png, png_infop info, IccBytes embedded) {
#ifdef PNG_cICP_SUPPORTED
    png_byte primaries = 0;
    png_byte transfer = 0;
    png_byte matrix = 0;
    png_byte range = 0;
    if (png_get_cICP(png, info, &primaries, &transfer, &matrix, &range) != 0) {
        return profileFromCicp(primaries, transfer);
    }
#endif
    if (!embedded.empty()) {
        return embedded;
    }
    int intent = 0;
    if (png_get_sRGB(png, info, &intent) != 0) {
        return {};
    }
    double gamma = 0;
    if (png_get_gAMA(png, info, &gamma) == 0) {
        return {};
    }
    RgbPrimaries p;
    const bool hasPrimaries = png_get_cHRM(png, info, &p.white.x, &p.white.y, &p.red.x, &p.red.y, &p.green.x, &p.green.y,
                                           &p.blue.x, &p.blue.y) != 0;
    return profileFromPngGamma(gamma, hasPrimaries ? &p : nullptr);
}

}  // namespace

bool isPng(std::span<const std::uint8_t> h) {
    return h.size() >= kSignature.size() && std::equal(kSignature.begin(), kSignature.end(), h.begin());
}

Result<std::shared_ptr<ImageSource>> openPngSource(std::vector<std::uint8_t> bytes) {
    Reader reader(bytes);
    if (!reader.readInfo()) {
        return std::unexpected(reader.error());
    }
    const png_structp png = reader.png();
    const png_infop pngInfo = reader.info();
    const auto width = png_get_image_width(png, pngInfo);
    const auto height = png_get_image_height(png, pngInfo);
    const int depth = png_get_bit_depth(png, pngInfo);
    const int colorType = png_get_color_type(png, pngInfo);
    const bool hasAlpha = (colorType & PNG_COLOR_MASK_ALPHA) != 0 || png_get_valid(png, pngInfo, PNG_INFO_tRNS) != 0;
    const bool gray = (colorType & PNG_COLOR_MASK_COLOR) == 0;
    const Size size{static_cast<int>(width), static_cast<int>(height)};

    SubimageInfo info;
    info.format = gray && !hasAlpha ? (depth < 16 ? PixelFormat::Gray8 : PixelFormat::Gray16)
                                    : depth < 16 ? PixelFormat::Bgrx8 : PixelFormat::Rgba16;
    info.alpha = hasAlpha;
    if (!fitsInMemoryLimit(size, info.format)) {
        return std::unexpected("image is too large");
    }

    png_charp iccName = nullptr;
    int iccCompression = 0;
    png_bytep icc = nullptr;
    png_uint_32 iccLength = 0;
    if (png_get_iCCP(png, pngInfo, &iccName, &iccCompression, &icc, &iccLength) != 0 && icc) {
        info.iccProfile.assign(icc, icc + iccLength);
    }
    info.iccProfile = describedProfile(png, pngInfo, std::move(info.iccProfile));
    readExtraChunks(bytes, info);

    StoredFormat& stored = info.stored;
    stored.container = "PNG";
    stored.compression = "Deflate, lossless";
    stored.bitsPerSample = depth;
    stored.colorModel = colorType == PNG_COLOR_TYPE_PALETTE ? "Palette" : gray ? "Grayscale" : "RGB";
    if (hasAlpha) {
        stored.colorModel += " + alpha";
    }

    const PixelFormat format = info.format;
    return makeWholeImageSource(std::move(info), size,
                                [bytes = std::move(bytes), format, size, hasAlpha](const std::stop_token& stop) -> Result<ImageBuffer> {
        try {
            return decode(bytes, format, size, hasAlpha, stop);
        } catch (const std::bad_alloc&) {
            return std::unexpected("not enough memory to decode image");
        }
    });
}

}  // namespace iv
