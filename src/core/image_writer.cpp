#include "core/image_writer.h"

#include <algorithm>
#include <cctype>
#include <csetjmp>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <thread>

#include <jpeglib.h>
#include <tiffio.h>

#ifdef IV_HAVE_PNG
#include <png.h>
#endif
#ifdef IV_HAVE_WEBP
#include <webp/encode.h>
#include <webp/mux.h>
#endif
#ifdef IV_HAVE_AVIF
#include <avif/avif.h>
#endif
#ifdef IV_HAVE_JXL
#include <jxl/encode.h>
#include <jxl/encode_cxx.h>
#include <jxl/thread_parallel_runner.h>
#include <jxl/thread_parallel_runner_cxx.h>
#endif

#include "core/exif.h"
#include "core/file_io.h"

namespace iv {

namespace {

enum class Depth { Eight, Sixteen, Float };

// Interleaved samples in rows without padding, in native byte order: gray or R, G, B.
struct Packed {
    int width = 0;
    int height = 0;
    int channels = 3;  // 1 or 3
    int bits = 8;      // 8, 16, or 32 for float
    std::vector<std::uint8_t> bytes;

    std::size_t rowBytes() const { return static_cast<std::size_t>(width) * channels * (bits / 8); }
    const std::uint8_t* row(int y) const { return bytes.data() + rowBytes() * static_cast<std::size_t>(y); }
    std::uint8_t* row(int y) { return bytes.data() + rowBytes() * static_cast<std::size_t>(y); }
};

bool allGray(const ImageBuffer& image) {
    for (int y = 0; y < image.height(); ++y) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(image.row(y));
        for (int x = 0; x < image.width(); ++x, p += 4) {
            if (p[0] != p[1] || p[1] != p[2]) {
                return false;
            }
        }
    }
    return true;
}

bool isGrayFormat(PixelFormat format) { return format == PixelFormat::Gray16 || format == PixelFormat::Gray32F; }

// The colour space an ICC profile is for, from its header: "RGB ", "GRAY" and so on.
std::string profileSpace(const std::vector<std::uint8_t>& icc) {
    return icc.size() >= 20 ? std::string(reinterpret_cast<const char*>(icc.data()) + 16, 4) : std::string();
}

// Converts the image to what a format can hold. With grayAllowed, gray images keep one channel.
Packed pack(const ImageBuffer& image, Depth deepest, bool grayAllowed) {
    const PixelFormat format = image.format();
    Packed out;
    out.width = image.width();
    out.height = image.height();
    const bool gray = isGrayFormat(format) || (format == PixelFormat::Bgrx8 && grayAllowed && allGray(image));
    out.channels = gray && grayAllowed ? 1 : 3;
    switch (format) {
    case PixelFormat::Bgrx8:
        out.bits = 8;
        break;
    case PixelFormat::Gray16:
    case PixelFormat::Rgba16:
        out.bits = deepest == Depth::Eight ? 8 : 16;
        break;
    case PixelFormat::Gray32F:
    case PixelFormat::Rgba32F:
        out.bits = deepest == Depth::Float ? 32 : deepest == Depth::Sixteen ? 16 : 8;
        break;
    }
    out.bytes.resize(out.rowBytes() * static_cast<std::size_t>(out.height));

    const int sourceChannels = isGrayFormat(format) ? 1 : 4;
    for (int y = 0; y < out.height; ++y) {
        const std::byte* in = image.row(y);
        std::uint8_t* target = out.row(y);
        for (int x = 0; x < out.width; ++x) {
            for (int c = 0; c < out.channels; ++c) {
                // Which source channel: B, G, R order for 8-bit, the only one for gray.
                int source = isGrayFormat(format) ? 0 : c;
                if (format == PixelFormat::Bgrx8) {
                    source = out.channels == 1 ? 1 : 2 - c;
                }
                const std::size_t at = static_cast<std::size_t>(x) * sourceChannels + static_cast<std::size_t>(source);
                double value = 0;  // 0 to 1, or as stored for float
                float raw = 0;
                switch (format) {
                case PixelFormat::Bgrx8:
                    value = static_cast<double>(reinterpret_cast<const std::uint8_t*>(in)[at]) / 255;
                    break;
                case PixelFormat::Gray16:
                case PixelFormat::Rgba16:
                    value = static_cast<double>(reinterpret_cast<const std::uint16_t*>(in)[at]) / 65535;
                    break;
                case PixelFormat::Gray32F:
                case PixelFormat::Rgba32F:
                    raw = reinterpret_cast<const float*>(in)[at];
                    value = raw;
                    break;
                }
                const std::size_t index = static_cast<std::size_t>(x) * out.channels + static_cast<std::size_t>(c);
                if (out.bits == 32) {
                    reinterpret_cast<float*>(target)[index] = raw;
                } else if (out.bits == 16) {
                    reinterpret_cast<std::uint16_t*>(target)[index] =
                        static_cast<std::uint16_t>(std::clamp(value, 0.0, 1.0) * 65535 + 0.5);
                } else {
                    target[index] = static_cast<std::uint8_t>(std::clamp(value, 0.0, 1.0) * 255 + 0.5);
                }
            }
        }
    }
    return out;
}

// The EXIF to write as a TIFF structure, updated for upright pixels of this size, or nothing if
// what was given is not one.
std::vector<std::uint8_t> exifFor(const WriteMetadata& metadata, int width, int height) {
    std::span<const std::uint8_t> bytes = metadata.exif;
    if (bytes.size() >= 6 && std::memcmp(bytes.data(), "Exif\0\0", 6) == 0) {
        bytes = bytes.subspan(6);
    }
    if (bytes.size() < 8 || !((bytes[0] == 'I' && bytes[1] == 'I') || (bytes[0] == 'M' && bytes[1] == 'M'))) {
        return {};
    }
    std::vector<std::uint8_t> tiff(bytes.begin(), bytes.end());
    resetExifForPixels(tiff, width, height);
    return tiff;
}

// Whether gray pixels may be written as gray: not if an RGB profile has to go with them.
bool grayAllowedWith(const WriteMetadata& metadata) {
    return metadata.iccProfile.empty() || profileSpace(metadata.iccProfile) == "GRAY";
}

// libjpeg

struct JpegErrors {
    jpeg_error_mgr base;
    std::jmp_buf jump;
    char message[JMSG_LENGTH_MAX];
};

[[noreturn]] void onJpegError(j_common_ptr cinfo) {
    auto* errors = reinterpret_cast<JpegErrors*>(cinfo->err);
    cinfo->err->format_message(cinfo, errors->message);
    std::longjmp(errors->jump, 1);
}

void onJpegMessage(j_common_ptr) {}

// Calls into libjpeg with a jump target, keeping only trivially destructible locals there.
class JpegCompressor {
public:
    JpegCompressor() {
        m_cinfo.err = jpeg_std_error(&m_errors.base);
        m_errors.base.error_exit = onJpegError;
        m_errors.base.output_message = onJpegMessage;
    }
    ~JpegCompressor() {
        if (m_created) {
            jpeg_destroy_compress(&m_cinfo);
        }
        std::free(m_output);
    }
    JpegCompressor(const JpegCompressor&) = delete;
    JpegCompressor& operator=(const JpegCompressor&) = delete;

    const char* error() const { return m_errors.message; }

    bool compress(const Packed& pixels, const JpegWriteOptions& options, const std::vector<std::uint8_t>& app1,
                  const std::vector<std::uint8_t>& icc) {
        if (setjmp(m_errors.jump)) {
            return false;
        }
        jpeg_create_compress(&m_cinfo);
        m_created = true;
        jpeg_mem_dest(&m_cinfo, &m_output, &m_outputSize);
        m_cinfo.image_width = static_cast<JDIMENSION>(pixels.width);
        m_cinfo.image_height = static_cast<JDIMENSION>(pixels.height);
        m_cinfo.input_components = pixels.channels;
        m_cinfo.in_color_space = pixels.channels == 1 ? JCS_GRAYSCALE : JCS_RGB;
        jpeg_set_defaults(&m_cinfo);
        jpeg_set_quality(&m_cinfo, std::clamp(options.quality, 1, 100), TRUE);
        if (pixels.channels == 3) {
            m_cinfo.comp_info[0].h_samp_factor = options.chroma == ChromaSubsampling::Full ? 1 : 2;
            m_cinfo.comp_info[0].v_samp_factor = options.chroma == ChromaSubsampling::Both ? 2 : 1;
        }
        if (options.progressive) {
            jpeg_simple_progression(&m_cinfo);
        } else {
            m_cinfo.optimize_coding = TRUE;
        }
        jpeg_start_compress(&m_cinfo, TRUE);
        if (!app1.empty()) {
            jpeg_write_marker(&m_cinfo, JPEG_APP0 + 1, app1.data(), static_cast<unsigned int>(app1.size()));
        }
        if (!icc.empty()) {
            jpeg_write_icc_profile(&m_cinfo, icc.data(), static_cast<unsigned int>(icc.size()));
        }
        while (m_cinfo.next_scanline < m_cinfo.image_height) {
            auto* row = const_cast<JSAMPROW>(pixels.row(static_cast<int>(m_cinfo.next_scanline)));
            jpeg_write_scanlines(&m_cinfo, &row, 1);
        }
        jpeg_finish_compress(&m_cinfo);
        return true;
    }

    std::vector<std::uint8_t> output() const { return {m_output, m_output + m_outputSize}; }

private:
    jpeg_compress_struct m_cinfo{};
    JpegErrors m_errors{};
    bool m_created = false;
    unsigned char* m_output = nullptr;
    unsigned long m_outputSize = 0;
};

Result<std::vector<std::uint8_t>> encodeJpeg(const ImageBuffer& image, const WriteMetadata& metadata,
                                             const JpegWriteOptions& options) {
    const Packed pixels = pack(image, Depth::Eight, grayAllowedWith(metadata));
    std::vector<std::uint8_t> app1;
    if (const auto tiff = exifFor(metadata, pixels.width, pixels.height); !tiff.empty() && tiff.size() + 6 <= 65533) {
        app1 = {'E', 'x', 'i', 'f', 0, 0};
        app1.insert(app1.end(), tiff.begin(), tiff.end());
    }
    JpegCompressor compressor;
    if (!compressor.compress(pixels, options, app1, metadata.iccProfile)) {
        return std::unexpected(std::string("cannot write JPEG: ") + compressor.error());
    }
    return compressor.output();
}

// libpng

#ifdef IV_HAVE_PNG
struct PngWriter {
    std::vector<std::uint8_t> bytes;
    std::string error;
};

void onPngWrite(png_structp png, png_bytep data, png_size_t size) {
    auto* writer = static_cast<PngWriter*>(png_get_io_ptr(png));
    writer->bytes.insert(writer->bytes.end(), data, data + size);
}

void onPngFlush(png_structp) {}

[[noreturn]] void onPngError(png_structp png, png_const_charp message) {
    static_cast<PngWriter*>(png_get_error_ptr(png))->error = message;
    png_longjmp(png, 1);
}

void onPngWarning(png_structp, png_const_charp) {}

// libpng reports errors with longjmp, so this keeps only trivially destructible locals.
bool writePng(PngWriter& writer, const Packed& pixels, const PngWriteOptions& options, const std::vector<std::uint8_t>& icc,
              const std::vector<std::uint8_t>& exif) {
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, &writer, onPngError, onPngWarning);
    if (!png) {
        writer.error = "out of memory";
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info || setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        return false;
    }
    png_set_write_fn(png, &writer, onPngWrite, onPngFlush);
    png_set_compression_level(png, std::clamp(options.compressionLevel, 0, 9));
    png_set_IHDR(png, info, static_cast<png_uint_32>(pixels.width), static_cast<png_uint_32>(pixels.height), pixels.bits,
                 pixels.channels == 1 ? PNG_COLOR_TYPE_GRAY : PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    // A profile for the wrong colour space would be rejected, so only a matching one goes in.
    const bool profileFits = profileSpace(icc) == (pixels.channels == 1 ? "GRAY" : "RGB ");
    if (!icc.empty() && profileFits) {
        png_set_iCCP(png, info, "ICC Profile", PNG_COMPRESSION_TYPE_BASE, icc.data(), static_cast<png_uint_32>(icc.size()));
    } else if (icc.empty()) {
        png_set_sRGB_gAMA_and_cHRM(png, info, PNG_sRGB_INTENT_PERCEPTUAL);
    }
    if (!exif.empty()) {
        png_set_eXIf_1(png, info, static_cast<png_uint_32>(exif.size()), const_cast<png_bytep>(exif.data()));
    }
    png_write_info(png, info);
    if (pixels.bits == 16) {
        png_set_swap(png);  // PNG is big-endian; a no-op where the machine is too
    }
    for (int y = 0; y < pixels.height; ++y) {
        png_write_row(png, pixels.row(y));
    }
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    return true;
}

Result<std::vector<std::uint8_t>> encodePng(const ImageBuffer& image, const WriteMetadata& metadata,
                                            const PngWriteOptions& options) {
    const Packed pixels = pack(image, Depth::Sixteen, grayAllowedWith(metadata));
    PngWriter writer;
    if (!writePng(writer, pixels, options, metadata.iccProfile, exifFor(metadata, pixels.width, pixels.height))) {
        return std::unexpected("cannot write PNG: " + writer.error);
    }
    return std::move(writer.bytes);
}
#endif

// libtiff, writing to memory

struct MemoryFile {
    std::vector<std::uint8_t> data;
    std::uint64_t position = 0;
    std::string error;
};

tmsize_t memoryRead(thandle_t handle, void* buffer, tmsize_t size) {
    auto& file = *static_cast<MemoryFile*>(handle);
    const std::uint64_t available = file.position < file.data.size() ? file.data.size() - file.position : 0;
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(available, static_cast<std::uint64_t>(size)));
    std::memcpy(buffer, file.data.data() + file.position, count);
    file.position += count;
    return static_cast<tmsize_t>(count);
}

tmsize_t memoryWrite(thandle_t handle, void* buffer, tmsize_t size) {
    auto& file = *static_cast<MemoryFile*>(handle);
    const auto end = file.position + static_cast<std::uint64_t>(size);
    if (end > file.data.size()) {
        file.data.resize(static_cast<std::size_t>(end));
    }
    std::memcpy(file.data.data() + file.position, buffer, static_cast<std::size_t>(size));
    file.position = end;
    return size;
}

toff_t memorySeek(thandle_t handle, toff_t offset, int whence) {
    auto& file = *static_cast<MemoryFile*>(handle);
    const std::uint64_t base = whence == SEEK_CUR ? file.position : whence == SEEK_END ? file.data.size() : 0;
    file.position = base + offset;  // wraps around for a negative offset, as libtiff intends
    return file.position;
}

int memoryClose(thandle_t) { return 0; }
toff_t memorySize(thandle_t handle) { return static_cast<MemoryFile*>(handle)->data.size(); }
int memoryMap(thandle_t, void**, toff_t*) { return 0; }
void memoryUnmap(thandle_t, void*, toff_t) {}

int captureTiffError(TIFF*, void* userData, const char*, const char* format, va_list args) {
    char buffer[512];
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    static_cast<MemoryFile*>(userData)->error = buffer;
    return 1;
}

int ignoreTiffWarning(TIFF*, void*, const char*, const char*, va_list) { return 1; }

Result<std::vector<std::uint8_t>> encodeTiff(const ImageBuffer& image, const WriteMetadata& metadata,
                                             const TiffWriteOptions& options) {
    const Packed pixels = pack(image, Depth::Float, grayAllowedWith(metadata));
    MemoryFile file;
    TIFFOpenOptions* openOptions = TIFFOpenOptionsAlloc();
    TIFFOpenOptionsSetErrorHandlerExtR(openOptions, captureTiffError, &file);
    TIFFOpenOptionsSetWarningHandlerExtR(openOptions, ignoreTiffWarning, nullptr);
    TIFF* tiff = TIFFClientOpenExt("memory", "w", &file, memoryRead, memoryWrite, memorySeek, memoryClose, memorySize,
                                   memoryMap, memoryUnmap, openOptions);
    TIFFOpenOptionsFree(openOptions);
    if (!tiff) {
        return std::unexpected("cannot write TIFF: " + file.error);
    }

    const bool isFloat = pixels.bits == 32;
    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, static_cast<std::uint32_t>(pixels.width));
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, static_cast<std::uint32_t>(pixels.height));
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, static_cast<std::uint16_t>(pixels.channels));
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, static_cast<std::uint16_t>(pixels.bits));
    TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT, isFloat ? SAMPLEFORMAT_IEEEFP : SAMPLEFORMAT_UINT);
    TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, pixels.channels == 1 ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_RGB);
    TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    switch (options.compression) {
    case TiffCompression::None:
        TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
        break;
    case TiffCompression::Lzw:
    case TiffCompression::Deflate:
        TIFFSetField(tiff, TIFFTAG_COMPRESSION,
                     options.compression == TiffCompression::Lzw ? COMPRESSION_LZW : COMPRESSION_ADOBE_DEFLATE);
        TIFFSetField(tiff, TIFFTAG_PREDICTOR, isFloat ? PREDICTOR_FLOATINGPOINT : PREDICTOR_HORIZONTAL);
        break;
    }
    TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tiff, 0));
    if (!metadata.iccProfile.empty() && profileSpace(metadata.iccProfile) == (pixels.channels == 1 ? "GRAY" : "RGB ")) {
        TIFFSetField(tiff, TIFFTAG_ICCPROFILE, static_cast<std::uint32_t>(metadata.iccProfile.size()),
                     metadata.iccProfile.data());
    }
    bool ok = true;
    for (int y = 0; y < pixels.height && ok; ++y) {
        ok = TIFFWriteScanline(tiff, const_cast<std::uint8_t*>(pixels.row(y)), static_cast<std::uint32_t>(y), 0) >= 0;
    }
    ok = ok && TIFFWriteDirectory(tiff) != 0;
    TIFFClose(tiff);
    if (!ok) {
        return std::unexpected("cannot write TIFF: " + file.error);
    }
    return std::move(file.data);
}

// libwebp

#ifdef IV_HAVE_WEBP
Result<std::vector<std::uint8_t>> encodeWebp(const ImageBuffer& image, const WriteMetadata& metadata,
                                             const WebpWriteOptions& options) {
    const Packed pixels = pack(image, Depth::Eight, false);
    if (pixels.width > WEBP_MAX_DIMENSION || pixels.height > WEBP_MAX_DIMENSION) {
        return std::unexpected("WebP holds at most " + std::to_string(WEBP_MAX_DIMENSION) + " pixels a side");
    }
    WebPConfig config;
    WebPPicture picture;
    if (!WebPConfigInit(&config) || !WebPPictureInit(&picture)) {
        return std::unexpected("cannot start the WebP encoder");
    }
    config.lossless = options.lossless ? 1 : 0;
    config.quality = static_cast<float>(std::clamp(options.quality, 0, 100));
    config.method = std::clamp(options.effort, 0, 6);
    config.thread_level = 1;
    picture.use_argb = options.lossless ? 1 : 0;
    picture.width = pixels.width;
    picture.height = pixels.height;
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = &writer;
    const bool encoded = WebPPictureImportRGB(&picture, pixels.bytes.data(), static_cast<int>(pixels.rowBytes())) &&
                         WebPEncode(&config, &picture);
    const WebPEncodingError error = picture.error_code;
    WebPPictureFree(&picture);
    if (!encoded) {
        WebPMemoryWriterClear(&writer);
        return std::unexpected("cannot write WebP, error " + std::to_string(static_cast<int>(error)));
    }
    std::vector<std::uint8_t> bytes(writer.mem, writer.mem + writer.size);
    WebPMemoryWriterClear(&writer);

    const std::vector<std::uint8_t> exif = exifFor(metadata, pixels.width, pixels.height);
    const bool profile = !metadata.iccProfile.empty() && profileSpace(metadata.iccProfile) == "RGB ";
    if (!profile && exif.empty()) {
        return bytes;
    }
    // The metadata goes into chunks around the image, which the mux arranges.
    WebPMux* mux = WebPMuxNew();
    const WebPData imageData{bytes.data(), bytes.size()};
    bool ok = mux && WebPMuxSetImage(mux, &imageData, 0) == WEBP_MUX_OK;
    if (ok && profile) {
        const WebPData chunk{metadata.iccProfile.data(), metadata.iccProfile.size()};
        ok = WebPMuxSetChunk(mux, "ICCP", &chunk, 0) == WEBP_MUX_OK;
    }
    if (ok && !exif.empty()) {
        const WebPData chunk{exif.data(), exif.size()};
        ok = WebPMuxSetChunk(mux, "EXIF", &chunk, 0) == WEBP_MUX_OK;
    }
    WebPData assembled{nullptr, 0};
    ok = ok && WebPMuxAssemble(mux, &assembled) == WEBP_MUX_OK;
    std::vector<std::uint8_t> result;
    if (ok) {
        result.assign(assembled.bytes, assembled.bytes + assembled.size);
    }
    WebPDataClear(&assembled);
    WebPMuxDelete(mux);
    if (!ok) {
        return std::unexpected("cannot add metadata to WebP");
    }
    return result;
}
#endif

// libavif

#ifdef IV_HAVE_AVIF
Result<std::vector<std::uint8_t>> encodeAvif(const ImageBuffer& image, const WriteMetadata& metadata,
                                             const AvifWriteOptions& options) {
    if (!avifCodecName(AVIF_CODEC_CHOICE_AUTO, AVIF_CODEC_FLAG_CAN_ENCODE)) {
        return std::unexpected("this build has no AV1 encoder");
    }
    Packed pixels = pack(image, Depth::Sixteen, false);
    const avifPixelFormat yuv = options.chroma == ChromaSubsampling::Full        ? AVIF_PIXEL_FORMAT_YUV444
                                : options.chroma == ChromaSubsampling::Horizontal ? AVIF_PIXEL_FORMAT_YUV422
                                                                                  : AVIF_PIXEL_FORMAT_YUV420;
    const std::uint32_t depth = pixels.bits == 8 ? 8 : 10;
    avifImage* avif = avifImageCreate(static_cast<std::uint32_t>(pixels.width), static_cast<std::uint32_t>(pixels.height),
                                      depth, yuv);
    if (!avif) {
        return std::unexpected("out of memory");
    }
    avif->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;
    avif->yuvRange = AVIF_RANGE_FULL;
    std::string error;
    if (!metadata.iccProfile.empty() && profileSpace(metadata.iccProfile) == "RGB ") {
        if (avifImageSetProfileICC(avif, metadata.iccProfile.data(), metadata.iccProfile.size()) != AVIF_RESULT_OK) {
            error = "cannot add the colour profile";
        }
    } else {
        avif->colorPrimaries = AVIF_COLOR_PRIMARIES_BT709;
        avif->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
    }
    if (const auto exif = exifFor(metadata, pixels.width, pixels.height); error.empty() && !exif.empty()) {
        if (avifImageSetMetadataExif(avif, exif.data(), exif.size()) != AVIF_RESULT_OK) {
            error = "cannot add EXIF";
        }
    }

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, avif);
    rgb.format = AVIF_RGB_FORMAT_RGB;
    rgb.depth = static_cast<std::uint32_t>(pixels.bits);
    rgb.pixels = pixels.bytes.data();
    rgb.rowBytes = static_cast<std::uint32_t>(pixels.rowBytes());
    if (error.empty()) {
        if (const avifResult result = avifImageRGBToYUV(avif, &rgb); result != AVIF_RESULT_OK) {
            error = avifResultToString(result);
        }
    }

    std::vector<std::uint8_t> bytes;
    if (error.empty()) {
        avifEncoder* encoder = avifEncoderCreate();
        if (!encoder) {
            error = "out of memory";
        } else {
            encoder->quality = std::clamp(options.quality, 0, 100);
            encoder->speed = std::clamp(options.speed, 0, 10);
            encoder->maxThreads = static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
            avifRWData output = AVIF_DATA_EMPTY;
            if (const avifResult result = avifEncoderWrite(encoder, avif, &output); result != AVIF_RESULT_OK) {
                error = avifResultToString(result);
            } else {
                bytes.assign(output.data, output.data + output.size);
            }
            avifRWDataFree(&output);
            avifEncoderDestroy(encoder);
        }
    }
    avifImageDestroy(avif);
    if (!error.empty()) {
        return std::unexpected("cannot write AVIF: " + error);
    }
    return bytes;
}
#endif

// libjxl

#ifdef IV_HAVE_JXL
Result<std::vector<std::uint8_t>> encodeJxl(const ImageBuffer& image, const WriteMetadata& metadata,
                                            const JxlWriteOptions& options) {
    const Packed pixels = pack(image, Depth::Float, grayAllowedWith(metadata));
    const auto encoder = JxlEncoderMake(nullptr);
    const auto runner = JxlThreadParallelRunnerMake(nullptr, std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
    if (!encoder || !runner ||
        JxlEncoderSetParallelRunner(encoder.get(), JxlThreadParallelRunner, runner.get()) != JXL_ENC_SUCCESS) {
        return std::unexpected("cannot start the JPEG XL encoder");
    }

    JxlBasicInfo info;
    JxlEncoderInitBasicInfo(&info);
    info.xsize = static_cast<std::uint32_t>(pixels.width);
    info.ysize = static_cast<std::uint32_t>(pixels.height);
    info.bits_per_sample = static_cast<std::uint32_t>(pixels.bits);
    info.exponent_bits_per_sample = pixels.bits == 32 ? 8 : 0;
    info.num_color_channels = static_cast<std::uint32_t>(pixels.channels);
    // Lossless keeps the pixels in their own colour space instead of the encoder's own.
    info.uses_original_profile = options.lossless ? JXL_TRUE : JXL_FALSE;
    if (JxlEncoderSetBasicInfo(encoder.get(), &info) != JXL_ENC_SUCCESS) {
        return std::unexpected("cannot write JPEG XL of this size");
    }
    const bool profileFits = profileSpace(metadata.iccProfile) == (pixels.channels == 1 ? "GRAY" : "RGB ");
    if (!metadata.iccProfile.empty() && profileFits) {
        if (JxlEncoderSetICCProfile(encoder.get(), metadata.iccProfile.data(), metadata.iccProfile.size()) != JXL_ENC_SUCCESS) {
            return std::unexpected("cannot write JPEG XL: the colour profile is not usable");
        }
    } else {
        JxlColorEncoding color;
        JxlColorEncodingSetToSRGB(&color, pixels.channels == 1 ? JXL_TRUE : JXL_FALSE);
        JxlEncoderSetColorEncoding(encoder.get(), &color);
    }

    if (const auto exif = exifFor(metadata, pixels.width, pixels.height); !exif.empty()) {
        // The box starts with the offset of the TIFF header within it.
        std::vector<std::uint8_t> box(4, 0);
        box.insert(box.end(), exif.begin(), exif.end());
        if (JxlEncoderUseBoxes(encoder.get()) != JXL_ENC_SUCCESS ||
            JxlEncoderAddBox(encoder.get(), "Exif", box.data(), box.size(), JXL_FALSE) != JXL_ENC_SUCCESS) {
            return std::unexpected("cannot add EXIF to JPEG XL");
        }
        JxlEncoderCloseBoxes(encoder.get());
    }

    JxlEncoderFrameSettings* settings = JxlEncoderFrameSettingsCreate(encoder.get(), nullptr);
    JxlEncoderFrameSettingsSetOption(settings, JXL_ENC_FRAME_SETTING_EFFORT, std::clamp(options.effort, 1, 9));
    if (options.lossless) {
        JxlEncoderSetFrameLossless(settings, JXL_TRUE);
    } else {
        JxlEncoderSetFrameDistance(settings, std::clamp(options.distance, 0.1F, 25.0F));
    }
    const JxlDataType type = pixels.bits == 32 ? JXL_TYPE_FLOAT : pixels.bits == 16 ? JXL_TYPE_UINT16 : JXL_TYPE_UINT8;
    const JxlPixelFormat format{static_cast<std::uint32_t>(pixels.channels), type, JXL_NATIVE_ENDIAN, 0};
    if (JxlEncoderAddImageFrame(settings, &format, pixels.bytes.data(), pixels.bytes.size()) != JXL_ENC_SUCCESS) {
        return std::unexpected("cannot encode JPEG XL");
    }
    JxlEncoderCloseInput(encoder.get());

    std::vector<std::uint8_t> bytes(pixels.bytes.size() / 4 + 4096);
    std::size_t used = 0;
    for (;;) {
        std::uint8_t* next = bytes.data() + used;
        std::size_t available = bytes.size() - used;
        const JxlEncoderStatus status = JxlEncoderProcessOutput(encoder.get(), &next, &available);
        used = bytes.size() - available;
        if (status == JXL_ENC_SUCCESS) {
            bytes.resize(used);
            return bytes;
        }
        if (status != JXL_ENC_NEED_MORE_OUTPUT) {
            return std::unexpected("cannot encode JPEG XL");
        }
        bytes.resize(bytes.size() * 2);
    }
}
#endif

template <typename... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

}  // namespace

Result<std::vector<std::uint8_t>> encodeImage(const ImageBuffer& image, const WriteMetadata& metadata,
                                              const WriteOptions& options) {
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return std::unexpected("there is no image to write");
    }
    if (!canWrite(options)) {
        return std::unexpected("this build cannot write ." + std::string(extensionFor(options)) + " files");
    }
    try {
        return std::visit(
            Overloaded{
                [&](const JpegWriteOptions& o) { return encodeJpeg(image, metadata, o); },
                [&](const TiffWriteOptions& o) { return encodeTiff(image, metadata, o); },
#ifdef IV_HAVE_PNG
                [&](const PngWriteOptions& o) { return encodePng(image, metadata, o); },
#endif
#ifdef IV_HAVE_WEBP
                [&](const WebpWriteOptions& o) { return encodeWebp(image, metadata, o); },
#endif
#ifdef IV_HAVE_AVIF
                [&](const AvifWriteOptions& o) { return encodeAvif(image, metadata, o); },
#endif
#ifdef IV_HAVE_JXL
                [&](const JxlWriteOptions& o) { return encodeJxl(image, metadata, o); },
#endif
                [](const auto&) -> Result<std::vector<std::uint8_t>> { return std::unexpected("not built"); },
            },
            options);
    } catch (const std::bad_alloc&) {
        return std::unexpected("not enough memory to write the image");
    }
}

Result<void> writeImage(const std::filesystem::path& path, const ImageBuffer& image, const WriteMetadata& metadata,
                        const WriteOptions& options, bool replace) {
    if (!replace && std::filesystem::exists(path)) {
        return std::unexpected(path.filename().string() + " already exists");
    }
    const auto bytes = encodeImage(image, metadata, options);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return writeFileSafely(path, *bytes, replace);
}

bool canWrite(const WriteOptions& options) {
    return std::visit(Overloaded{
                          [](const JpegWriteOptions&) { return true; },
                          [](const TiffWriteOptions&) { return true; },
                          [](const PngWriteOptions&) {
#ifdef IV_HAVE_PNG
                              return true;
#else
                              return false;
#endif
                          },
                          [](const WebpWriteOptions&) {
#ifdef IV_HAVE_WEBP
                              return true;
#else
                              return false;
#endif
                          },
                          [](const AvifWriteOptions&) {
#ifdef IV_HAVE_AVIF
                              return avifCodecName(AVIF_CODEC_CHOICE_AUTO, AVIF_CODEC_FLAG_CAN_ENCODE) != nullptr;
#else
                              return false;
#endif
                          },
                          [](const JxlWriteOptions&) {
#ifdef IV_HAVE_JXL
                              return true;
#else
                              return false;
#endif
                          },
                      },
                      options);
}

std::string_view extensionFor(const WriteOptions& options) {
    return std::visit(Overloaded{
                          [](const JpegWriteOptions&) { return std::string_view("jpg"); },
                          [](const PngWriteOptions&) { return std::string_view("png"); },
                          [](const TiffWriteOptions&) { return std::string_view("tif"); },
                          [](const WebpWriteOptions&) { return std::string_view("webp"); },
                          [](const AvifWriteOptions&) { return std::string_view("avif"); },
                          [](const JxlWriteOptions&) { return std::string_view("jxl"); },
                      },
                      options);
}

std::optional<WriteOptions> optionsForExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::optional<WriteOptions> options;
    if (extension == ".jpg" || extension == ".jpeg" || extension == ".jpe" || extension == ".jfif") {
        options = JpegWriteOptions{};
    } else if (extension == ".png") {
        options = PngWriteOptions{};
    } else if (extension == ".tif" || extension == ".tiff") {
        options = TiffWriteOptions{};
    } else if (extension == ".webp") {
        options = WebpWriteOptions{};
    } else if (extension == ".avif") {
        options = AvifWriteOptions{};
    } else if (extension == ".jxl") {
        options = JxlWriteOptions{};
    }
    if (options && !canWrite(*options)) {
        return std::nullopt;
    }
    return options;
}

}  // namespace iv
