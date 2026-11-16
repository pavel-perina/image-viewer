#include "core/avif_source.h"

#include <avif/avif.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include <thread>
#include <utility>

#include "core/color.h"
#include "core/image_ops.h"
#include "core/whole_image_source.h"

namespace iv {

namespace {

class Decoder {
public:
    explicit Decoder(const std::vector<std::uint8_t>& bytes) : m_decoder(avifDecoderCreate()) {
        if (!m_decoder) {
            throw std::bad_alloc();
        }
        m_decoder->maxThreads = static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
        m_decoder->strictFlags = AVIF_STRICT_DISABLED;  // show what real files contain
        m_status = avifDecoderSetIOMemory(m_decoder, bytes.data(), bytes.size());
        if (m_status == AVIF_RESULT_OK) {
            m_status = avifDecoderParse(m_decoder);
        }
    }
    ~Decoder() { avifDecoderDestroy(m_decoder); }
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    avifDecoder* operator->() const { return m_decoder; }
    avifDecoder* get() const { return m_decoder; }
    avifResult status() const { return m_status; }

    std::string error(const char* fallback) const {
        const char* detail = m_decoder->diag.error;
        return detail && *detail ? std::string(detail) : std::string(fallback);
    }

private:
    avifDecoder* m_decoder;
    avifResult m_status = AVIF_RESULT_OK;
};

// HEIF stores orientation as rotation and mirroring, which the same EXIF values can express.
Orientation orientationOf(const avifImage& image) {
    const bool rotated = (image.transformFlags & AVIF_TRANSFORM_IROT) != 0;
    const bool mirrored = (image.transformFlags & AVIF_TRANSFORM_IMIR) != 0;
    const int angle = rotated ? image.irot.angle : 0;
    int exif = 1;
    if (!mirrored) {
        exif = angle == 1 ? 8 : angle == 2 ? 3 : angle == 3 ? 6 : 1;
    } else if (image.imir.axis == 0) {  // top and bottom swapped
        exif = angle == 1 ? 5 : angle == 2 ? 2 : angle == 3 ? 7 : 4;
    } else {                            // left and right swapped
        exif = angle == 1 ? 7 : angle == 2 ? 4 : angle == 3 ? 5 : 2;
    }
    return static_cast<Orientation>(exif);
}

Result<ImageBuffer> decode(const std::vector<std::uint8_t>& bytes, PixelFormat format, Size size, const std::stop_token& stop) {
    Decoder decoder(bytes);
    if (decoder.status() != AVIF_RESULT_OK) {
        return std::unexpected(decoder.error("cannot read AVIF image"));
    }
    if (stop.stop_requested()) {
        return std::unexpected("cancelled");
    }
    if (avifDecoderNextImage(decoder.get()) != AVIF_RESULT_OK) {
        return std::unexpected(decoder.error("cannot decode AVIF image"));
    }
    avifImage* const image = decoder->image;
    if (static_cast<int>(image->width) != size.width || static_cast<int>(image->height) != size.height) {
        return std::unexpected("AVIF image changed size");
    }
    if (stop.stop_requested()) {
        return std::unexpected("cancelled");
    }

    const ImageBuffer out = ImageBuffer::create(size, format);
    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, image);
    const bool is8 = format == PixelFormat::Bgrx8;
    rgb.depth = is8 ? 8 : 16;
    rgb.format = is8 ? AVIF_RGB_FORMAT_BGRA : AVIF_RGB_FORMAT_RGBA;
    rgb.pixels = reinterpret_cast<std::uint8_t*>(out.row(0));
    rgb.rowBytes = static_cast<std::uint32_t>(out.stride());
    if (avifImageYUVToRGB(image, &rgb) != AVIF_RESULT_OK) {
        return std::unexpected("cannot convert AVIF image");
    }
    // The colours come without alpha multiplied in, which is what the conversion gives unless
    // asked otherwise, also for a file that stores them multiplied.
    return out.withAlpha(decoder->alphaPresent != 0);
}

}  // namespace

bool isAvif(std::span<const std::uint8_t> h) {
    if (h.size() < 12 || std::memcmp(h.data() + 4, "ftyp", 4) != 0) {
        return false;
    }
    const auto brand = std::string_view(reinterpret_cast<const char*>(h.data()) + 8, 4);
    return brand == "avif" || brand == "avis" || brand == "mif1" || brand == "msf1" || brand == "miaf";
}

Result<std::shared_ptr<ImageSource>> openAvifSource(std::vector<std::uint8_t> bytes) {
    const Decoder decoder(bytes);
    if (decoder.status() != AVIF_RESULT_OK) {
        return std::unexpected(decoder.error("cannot read AVIF header"));
    }
    const avifImage& image = *decoder->image;
    const Size size{static_cast<int>(image.width), static_cast<int>(image.height)};

    SubimageInfo info;
    info.format = image.depth > 8 ? PixelFormat::Rgba16 : PixelFormat::Bgrx8;
    if (!fitsInMemoryLimit(size, info.format)) {
        return std::unexpected("image is too large");
    }
    info.orientation = orientationOf(image);
    if (image.icc.size > 0) {
        info.iccProfile.assign(image.icc.data, image.icc.data + image.icc.size);
    } else {
        info.iccProfile = profileFromCicp(image.colorPrimaries, image.transferCharacteristics);
    }
    if (image.exif.size > 0) {
        info.exif = parseExif({image.exif.data, image.exif.size});
        info.exif.orientation = info.orientation;  // the container decides, not the EXIF block
        const auto tiff = exifTiffStructure({image.exif.data, image.exif.size});
        info.rawExif.assign(tiff.begin(), tiff.end());
    }
    if (image.xmp.size > 0) {
        info.xmp.assign(image.xmp.data, image.xmp.data + image.xmp.size);
    }

    StoredFormat& stored = info.stored;
    stored.container = "AVIF";
    stored.bitsPerSample = static_cast<int>(image.depth);
    stored.animated = decoder->imageCount > 1;
    const bool gray = image.yuvFormat == AVIF_PIXEL_FORMAT_YUV400;
    stored.colorModel = gray ? "Grayscale" : "YCbCr";
    const bool identity = image.matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_IDENTITY;
    if (identity) {
        stored.colorModel = "RGB";
    }
    if (decoder->alphaPresent) {
        info.alpha = true;
        stored.colorModel += " + alpha";
    }
    // AV1 has no flag for it. Storing RGB directly is how lossless AVIF is made, and lossy
    // encoders do not do that, so this is reliable in practice though not guaranteed.
    stored.compression = identity ? "AV1, lossless" : "AV1, lossy";
    switch (image.yuvFormat) {
    case AVIF_PIXEL_FORMAT_YUV444: stored.chromaSubsampling = identity ? "" : "4:4:4"; break;
    case AVIF_PIXEL_FORMAT_YUV422: stored.chromaSubsampling = "4:2:2"; break;
    case AVIF_PIXEL_FORMAT_YUV420: stored.chromaSubsampling = "4:2:0"; break;
    default: break;
    }

    const PixelFormat format = info.format;
    return makeWholeImageSource(std::move(info), size,
                                [bytes = std::move(bytes), format, size](const std::stop_token& stop) -> Result<ImageBuffer> {
        try {
            return decode(bytes, format, size, stop);
        } catch (const std::bad_alloc&) {
            return std::unexpected("not enough memory to decode image");
        }
    });
}

}  // namespace iv
