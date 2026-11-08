#include "core/jxl_source.h"

#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#include <jxl/thread_parallel_runner.h>
#include <jxl/thread_parallel_runner_cxx.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <new>
#include <string>
#include <thread>
#include <utility>

#include "core/image_ops.h"
#include "core/whole_image_source.h"

namespace iv {

namespace {

constexpr std::size_t kMaxBoxBytes = 16 * 1024 * 1024;

struct Header {
    JxlBasicInfo basic{};
    std::vector<std::uint8_t> icc;
    ExifData exif;
    std::vector<std::uint8_t> rawExif;
    std::vector<std::uint8_t> xmp;
    bool recompressedJpeg = false;
};

// The container prefixes an EXIF box with the offset of the TIFF structure inside it.
void readExifBox(const std::vector<std::uint8_t>& box, Header& header) {
    if (box.size() < 4) {
        return;
    }
    const std::uint32_t offset = static_cast<std::uint32_t>(box[0]) << 24 | box[1] << 16 | box[2] << 8 | box[3];
    if (offset <= box.size() - 4) {
        const auto data = std::span(box).subspan(4 + offset);
        header.exif = parseExif(data);
        const auto tiff = exifTiffStructure(data);
        header.rawExif.assign(tiff.begin(), tiff.end());
    }
}

Result<Header> readHeader(const std::vector<std::uint8_t>& bytes) {
    const JxlDecoderPtr decoder = JxlDecoderMake(nullptr);
    if (!decoder) {
        throw std::bad_alloc();
    }
    if (JxlDecoderSubscribeEvents(decoder.get(), JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_BOX |
                                                     JXL_DEC_JPEG_RECONSTRUCTION) != JXL_DEC_SUCCESS) {
        return std::unexpected("cannot start JPEG XL decoder");
    }
    JxlDecoderSetDecompressBoxes(decoder.get(), JXL_TRUE);
    JxlDecoderSetInput(decoder.get(), bytes.data(), bytes.size());
    JxlDecoderCloseInput(decoder.get());

    Header header;
    bool haveBasic = false;
    std::vector<std::uint8_t> box;
    enum class Box { None, Exif, Xmp } reading = Box::None;
    const auto finishBox = [&] {
        if (reading == Box::None) {
            return;
        }
        box.resize(box.size() - JxlDecoderReleaseBoxBuffer(decoder.get()));
        if (reading == Box::Exif) {
            readExifBox(box, header);
        } else {
            header.xmp = box;
        }
        reading = Box::None;
    };
    for (;;) {
        const JxlDecoderStatus status = JxlDecoderProcessInput(decoder.get());
        switch (status) {
        case JXL_DEC_BASIC_INFO:
            if (JxlDecoderGetBasicInfo(decoder.get(), &header.basic) != JXL_DEC_SUCCESS) {
                return std::unexpected("cannot read JPEG XL header");
            }
            haveBasic = true;
            break;
        case JXL_DEC_COLOR_ENCODING: {
            std::size_t size = 0;
            if (JxlDecoderGetICCProfileSize(decoder.get(), JXL_COLOR_PROFILE_TARGET_DATA, &size) == JXL_DEC_SUCCESS && size > 0) {
                header.icc.resize(size);
                if (JxlDecoderGetColorAsICCProfile(decoder.get(), JXL_COLOR_PROFILE_TARGET_DATA, header.icc.data(), size) != JXL_DEC_SUCCESS) {
                    header.icc.clear();
                }
            }
            break;
        }
        case JXL_DEC_JPEG_RECONSTRUCTION:
            header.recompressedJpeg = true;
            break;
        case JXL_DEC_BOX: {
            finishBox();
            JxlBoxType type{};
            if (JxlDecoderGetBoxType(decoder.get(), type, JXL_TRUE) != JXL_DEC_SUCCESS) {
                break;
            }
            if (std::memcmp(type, "Exif", 4) == 0 && header.rawExif.empty()) {
                reading = Box::Exif;
            } else if (std::memcmp(type, "xml ", 4) == 0 && header.xmp.empty()) {
                reading = Box::Xmp;
            }
            if (reading != Box::None) {
                box.resize(64 * 1024);
                JxlDecoderSetBoxBuffer(decoder.get(), box.data(), box.size());
            }
            break;
        }
        case JXL_DEC_BOX_NEED_MORE_OUTPUT: {
            const std::size_t unused = JxlDecoderReleaseBoxBuffer(decoder.get());
            const std::size_t used = box.size() - unused;
            if (box.size() >= kMaxBoxBytes) {
                return haveBasic ? Result<Header>(std::move(header)) : std::unexpected("cannot read JPEG XL header");
            }
            box.resize(box.size() * 2);
            JxlDecoderSetBoxBuffer(decoder.get(), box.data() + used, box.size() - used);
            break;
        }
        case JXL_DEC_SUCCESS:
            finishBox();
            return header;
        default:
            // What is damaged may only be further on, which reading the pixels will report.
            if (haveBasic) {
                return header;
            }
            return std::unexpected("cannot read JPEG XL header");
        }
    }
}

// libjxl has no BGRA output, and Bgrx8 needs it.
void swapRedAndBlue(const ImageBuffer& image) {
    for (int y = 0; y < image.height(); ++y) {
        auto* row = reinterpret_cast<std::uint8_t*>(image.row(y));
        for (int x = 0; x < image.width(); ++x, row += 4) {
            std::swap(row[0], row[2]);
        }
    }
}

Result<ImageBuffer> decode(const std::vector<std::uint8_t>& bytes, PixelFormat format, Size size, bool hasAlpha,
                           const std::stop_token& stop) {
    const JxlDecoderPtr decoder = JxlDecoderMake(nullptr);
    const auto runner = JxlThreadParallelRunnerMake(nullptr, std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
    if (!decoder || !runner) {
        throw std::bad_alloc();
    }
    if (JxlDecoderSetParallelRunner(decoder.get(), JxlThreadParallelRunner, runner.get()) != JXL_DEC_SUCCESS ||
        JxlDecoderSubscribeEvents(decoder.get(), JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS) {
        return std::unexpected("cannot start JPEG XL decoder");
    }
    JxlDecoderSetInput(decoder.get(), bytes.data(), bytes.size());
    JxlDecoderCloseInput(decoder.get());

    const ImageBuffer image = ImageBuffer::create(size, format);
    JxlPixelFormat pixels{};
    pixels.num_channels = format == PixelFormat::Gray8 || format == PixelFormat::Gray16 || format == PixelFormat::Gray32F ? 1 : 4;
    pixels.data_type = format == PixelFormat::Bgrx8 || format == PixelFormat::Gray8       ? JXL_TYPE_UINT8
                       : format == PixelFormat::Gray16 || format == PixelFormat::Rgba16 ? JXL_TYPE_UINT16
                                                                                         : JXL_TYPE_FLOAT;
    pixels.endianness = JXL_NATIVE_ENDIAN;
    pixels.align = image.stride();

    for (;;) {
        if (stop.stop_requested()) {
            return std::unexpected("cancelled");
        }
        switch (JxlDecoderProcessInput(decoder.get())) {
        case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
            if (JxlDecoderSetImageOutBuffer(decoder.get(), &pixels, image.row(0), image.stride() * static_cast<std::size_t>(size.height)) !=
                JXL_DEC_SUCCESS) {
                return std::unexpected("cannot decode JPEG XL image");
            }
            break;
        case JXL_DEC_FULL_IMAGE:  // the first frame, which is all that is shown of an animation
            if (format == PixelFormat::Bgrx8) {
                swapRedAndBlue(image);
            }
            if (hasAlpha) {
                flattenAlpha(image);
            }
            return image;
        case JXL_DEC_ERROR:
        case JXL_DEC_NEED_MORE_INPUT:
            return std::unexpected("cannot decode JPEG XL image");
        case JXL_DEC_SUCCESS:
            return std::unexpected("JPEG XL file has no image");
        default:
            break;
        }
    }
}

}  // namespace

bool isJxl(std::span<const std::uint8_t> h) {
    static constexpr std::array<std::uint8_t, 12> kContainer{0, 0, 0, 0x0C, 'J', 'X', 'L', ' ', 0x0D, 0x0A, 0x87, 0x0A};
    if (h.size() >= 2 && h[0] == 0xFF && h[1] == 0x0A) {
        return true;
    }
    return h.size() >= kContainer.size() && std::equal(kContainer.begin(), kContainer.end(), h.begin());
}

Result<std::shared_ptr<ImageSource>> openJxlSource(std::vector<std::uint8_t> bytes) {
    auto header = readHeader(bytes);
    if (!header) {
        return std::unexpected(header.error());
    }
    const JxlBasicInfo& basic = header->basic;
    const Size size{static_cast<int>(basic.xsize), static_cast<int>(basic.ysize)};
    const bool hasAlpha = basic.alpha_bits > 0;
    const bool gray = basic.num_color_channels == 1;
    const bool isFloat = basic.exponent_bits_per_sample > 0;

    SubimageInfo info;
    if (basic.bits_per_sample <= 8 && !isFloat) {
        info.format = gray && !hasAlpha ? PixelFormat::Gray8 : PixelFormat::Bgrx8;
    } else if (isFloat) {
        info.format = gray && !hasAlpha ? PixelFormat::Gray32F : PixelFormat::Rgba32F;
    } else {
        info.format = gray && !hasAlpha ? PixelFormat::Gray16 : PixelFormat::Rgba16;
    }
    if (basic.xsize > 1'000'000'000 || basic.ysize > 1'000'000'000 || !fitsInMemoryLimit(size, info.format)) {
        return std::unexpected("image is too large");
    }
    info.iccProfile = std::move(header->icc);
    info.exif = std::move(header->exif);
    info.exif.orientation = Orientation::Normal;  // libjxl hands over the pixels already turned
    info.rawExif = std::move(header->rawExif);
    info.xmp = std::move(header->xmp);

    StoredFormat& stored = info.stored;
    stored.container = "JPEG XL";
    stored.bitsPerSample = static_cast<int>(basic.bits_per_sample);
    stored.floatSamples = isFloat;
    stored.animated = basic.have_animation != JXL_FALSE;
    stored.colorModel = std::string(gray ? "Grayscale" : "RGB") + (hasAlpha ? " + alpha" : "");
    // Lossless has to keep the original colour space, while lossy normally converts to XYB. A
    // lossy file could keep it too, so that case can only be called likely.
    stored.compression = header->recompressedJpeg       ? "JPEG XL, recompressed JPEG"
                         : basic.uses_original_profile ? "JPEG XL, likely lossless"
                                                       : "JPEG XL, lossy";

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
