#include "core/tiff_source.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <vector>

#include <tiffio.h>

#include "core/exif.h"
#include "core/image_details.h"

namespace iv {

namespace {

// The renderer uploads a tile as one texture, and every GPU can do this size.
constexpr std::uint32_t kMaxPhysicalTile = 4096;
constexpr int kStripTileSize = 2048;
constexpr std::size_t kCachedStripRuns = 4;
// Protects against files whose tags claim absurd sizes.
constexpr tmsize_t kMaxSingleAlloc = 1024 * 1024 * 1024;
constexpr std::uint64_t kMaxDecodedBytes = 8ull * 1024 * 1024 * 1024;

struct ErrorCapture {
    std::string message;
};

int captureError(TIFF*, void* userData, const char*, const char* format, va_list args) {
    char buffer[512];
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    static_cast<ErrorCapture*>(userData)->message = buffer;
    return 1;
}

int ignoreWarning(TIFF*, void*, const char*, const char*, va_list) { return 1; }

// Owns the libtiff handle. Errors go to this object rather than libtiff's global handlers,
// so opening one file never affects another.
class TiffHandle {
public:
    bool open(const std::filesystem::path& path) {
        TIFFOpenOptions* options = TIFFOpenOptionsAlloc();
        TIFFOpenOptionsSetErrorHandlerExtR(options, captureError, &m_errors);
        TIFFOpenOptionsSetWarningHandlerExtR(options, ignoreWarning, nullptr);
        TIFFOpenOptionsSetMaxSingleMemAlloc(options, kMaxSingleAlloc);
        // "O" loads tile offsets on demand. A gigapixel level has huge offset arrays, which
        // would otherwise be read on every switch between levels.
#ifdef _WIN32
        m_tiff = TIFFOpenWExt(path.c_str(), "rO", options);
#else
        m_tiff = TIFFOpenExt(path.c_str(), "rO", options);
#endif
        TIFFOpenOptionsFree(options);
        return m_tiff != nullptr;
    }

    ~TiffHandle() {
        if (m_tiff) {
            TIFFClose(m_tiff);
        }
    }

    TiffHandle() = default;
    TiffHandle(const TiffHandle&) = delete;
    TiffHandle& operator=(const TiffHandle&) = delete;

    TIFF* get() const { return m_tiff; }
    std::string error(const char* fallback) const { return m_errors.message.empty() ? fallback : m_errors.message; }

private:
    ErrorCapture m_errors;  // libtiff keeps a pointer to this
    TIFF* m_tiff = nullptr;
};

// One IFD, as much as is needed to read and classify it.
struct Directory {
    std::uint64_t offset = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool tiled = false;
    std::uint32_t tileWidth = 0;
    std::uint32_t tileHeight = 0;
    std::uint32_t rowsPerStrip = 0;
    std::uint16_t bits = 1;
    std::uint16_t samples = 1;
    std::uint16_t sampleFormat = SAMPLEFORMAT_UINT;
    std::uint16_t photometric = PHOTOMETRIC_MINISBLACK;
    std::uint16_t planar = PLANARCONFIG_CONTIG;
    std::uint16_t compression = COMPRESSION_NONE;
    std::uint32_t subfileType = 0;
    Orientation orientation = Orientation::Normal;
    std::vector<std::uint8_t> iccProfile;
    std::vector<std::uint64_t> subIfds;
    std::uint64_t exifOffset = 0;
    std::vector<std::uint8_t> xmp;
    StoredFormat stored;
    ExifData exif;
    bool useRgbaInterface = false;
    PixelFormat format = PixelFormat::Bgrx8;
    std::string problem;  // why this directory cannot be read, if it cannot
};

std::string compressionName(std::uint16_t compression) {
    switch (compression) {
    case COMPRESSION_NONE:         return "None";
    case COMPRESSION_LZW:          return "LZW, lossless";
    case COMPRESSION_ADOBE_DEFLATE:
    case COMPRESSION_DEFLATE:      return "Deflate, lossless";
    case COMPRESSION_ZSTD:         return "Zstandard, lossless";
    case COMPRESSION_LZMA:         return "LZMA, lossless";
    case COMPRESSION_PACKBITS:     return "PackBits, lossless";
    case COMPRESSION_CCITTRLE:
    case COMPRESSION_CCITTRLEW:    return "CCITT RLE, lossless";
    case COMPRESSION_CCITTFAX3:    return "CCITT Group 3, lossless";
    case COMPRESSION_CCITTFAX4:    return "CCITT Group 4, lossless";
    case COMPRESSION_JBIG:         return "JBIG, lossless";
    case COMPRESSION_JPEG:
    case COMPRESSION_OJPEG:        return "JPEG, lossy";
    case COMPRESSION_WEBP:         return "WebP";       // can be either
    case COMPRESSION_JXL:          return "JPEG XL";    // can be either
    case COMPRESSION_LERC:         return "LERC";       // can be either
    case COMPRESSION_SGILOG:
    case COMPRESSION_SGILOG24:     return "SGI Log";
    default:                       return "Code " + std::to_string(compression);
    }
}

std::string colorModelName(std::uint16_t photometric) {
    switch (photometric) {
    case PHOTOMETRIC_MINISBLACK: return "Grayscale";
    case PHOTOMETRIC_MINISWHITE: return "Grayscale, inverted";
    case PHOTOMETRIC_RGB:        return "RGB";
    case PHOTOMETRIC_PALETTE:    return "Palette";
    case PHOTOMETRIC_SEPARATED:  return "CMYK";
    case PHOTOMETRIC_YCBCR:      return "YCbCr";
    case PHOTOMETRIC_CIELAB:
    case PHOTOMETRIC_ICCLAB:
    case PHOTOMETRIC_ITULAB:     return "Lab";
    case PHOTOMETRIC_LOGL:       return "LogL";
    case PHOTOMETRIC_LOGLUV:     return "LogLuv";
    default:                     return {};
    }
}

StoredFormat describeStored(TIFF* tiff, const Directory& d) {
    StoredFormat stored;
    stored.container = "TIFF";
    stored.compression = compressionName(d.compression);
    stored.colorModel = colorModelName(d.photometric);
    stored.bitsPerSample = d.bits;
    stored.floatSamples = d.sampleFormat == SAMPLEFORMAT_IEEEFP;
    stored.signedSamples = d.sampleFormat == SAMPLEFORMAT_INT;

    std::uint16_t extraCount = 0;
    std::uint16_t* extraTypes = nullptr;
    if (TIFFGetField(tiff, TIFFTAG_EXTRASAMPLES, &extraCount, &extraTypes) && extraCount > 0) {
        const bool alpha = extraTypes && (extraTypes[0] == EXTRASAMPLE_ASSOCALPHA || extraTypes[0] == EXTRASAMPLE_UNASSALPHA);
        stored.colorModel += alpha ? " + alpha (not shown)" : " + extra samples (not shown)";
    }
    if (d.photometric == PHOTOMETRIC_YCBCR) {
        std::uint16_t h = 2;
        std::uint16_t v = 2;
        TIFFGetFieldDefaulted(tiff, TIFFTAG_YCBCRSUBSAMPLING, &h, &v);
        stored.chromaSubsampling = chromaSubsamplingName(h, v, 1, 1);
    }
    return stored;
}

std::string textTag(TIFF* tiff, std::uint32_t tag) {
    const char* value = nullptr;
    return TIFFGetField(tiff, tag, &value) && value ? cleanExifText(value) : std::string();
}

void readBasicCameraTags(TIFF* tiff, Directory& d) {
    d.exif.make = textTag(tiff, TIFFTAG_MAKE);
    d.exif.model = textTag(tiff, TIFFTAG_MODEL);
    d.exif.dateTaken = normalizeExifDate(textTag(tiff, TIFFTAG_DATETIME));
    toff_t offset = 0;
    if (TIFFGetField(tiff, TIFFTAG_EXIFIFD, &offset)) {
        d.exifOffset = offset;
    }
    std::uint32_t xmpLength = 0;
    const std::uint8_t* xmp = nullptr;
    if (TIFFGetField(tiff, TIFFTAG_XMLPACKET, &xmpLength, &xmp) && xmp) {
        d.xmp.assign(xmp, xmp + xmpLength);
    }
}

// libtiff returns rational tags as float or double depending on the tag and its version.
std::optional<double> rationalTag(TIFF* tiff, std::uint32_t tag) {
    const TIFFField* field = TIFFFieldWithTag(tiff, tag);
    if (!field) {
        return std::nullopt;
    }
    if (TIFFFieldSetGetSize(field) == sizeof(double)) {
        double value = 0;
        return TIFFGetField(tiff, tag, &value) ? std::optional<double>(value) : std::nullopt;
    }
    float value = 0;
    return TIFFGetField(tiff, tag, &value) ? std::optional<double>(value) : std::nullopt;
}

// Moves libtiff away from the current directory, so it cannot be part of walking the chain.
void readExifDirectory(TIFF* tiff, ExifData& exif, std::uint64_t offset) {
    if (!TIFFReadEXIFDirectory(tiff, offset)) {
        return;
    }
    exif.exposureSeconds = rationalTag(tiff, EXIFTAG_EXPOSURETIME);
    exif.fNumber = rationalTag(tiff, EXIFTAG_FNUMBER);
    exif.focalLength = rationalTag(tiff, EXIFTAG_FOCALLENGTH);
    exif.exposureBias = rationalTag(tiff, EXIFTAG_EXPOSUREBIASVALUE);
    std::uint16_t focal35 = 0;
    if (TIFFGetField(tiff, EXIFTAG_FOCALLENGTHIN35MMFILM, &focal35) && focal35 > 0) {
        exif.focalLength35mm = focal35;
    }
    std::uint16_t isoCount = 0;
    std::uint16_t* iso = nullptr;
    if (TIFFGetField(tiff, EXIFTAG_ISOSPEEDRATINGS, &isoCount, &iso) && iso && isoCount > 0 && iso[0] > 0) {
        exif.iso = iso[0];
    }
    exif.lens = textTag(tiff, EXIFTAG_LENSMODEL);
    if (exif.lens.empty()) {
        exif.lens = textTag(tiff, EXIFTAG_LENSMAKE);
    }
    const std::string original = normalizeExifDate(textTag(tiff, EXIFTAG_DATETIMEORIGINAL));
    if (!original.empty()) {
        exif.dateTaken = original;
    }
    exif.present = true;
}

// Whether samples can be read directly, keeping the file's precision.
bool canReadDirectly(const Directory& d) {
    if (d.photometric != PHOTOMETRIC_MINISBLACK && d.photometric != PHOTOMETRIC_MINISWHITE &&
        d.photometric != PHOTOMETRIC_RGB) {
        return false;
    }
    if (d.photometric == PHOTOMETRIC_RGB && d.samples < 3) {
        return false;
    }
    const bool isInt = d.sampleFormat == SAMPLEFORMAT_UINT || d.sampleFormat == SAMPLEFORMAT_INT ||
                       d.sampleFormat == SAMPLEFORMAT_VOID;
    switch (d.bits) {
    case 8:
    case 16:
        return isInt;
    case 32:
        return isInt || d.sampleFormat == SAMPLEFORMAT_IEEEFP;
    case 64:
        return d.sampleFormat == SAMPLEFORMAT_IEEEFP;
    default:
        return false;
    }
}

PixelFormat directFormat(const Directory& d) {
    const bool color = d.photometric == PHOTOMETRIC_RGB;
    if (d.bits == 8) {
        return PixelFormat::Bgrx8;
    }
    if (d.bits == 16) {
        return color ? PixelFormat::Rgba16 : PixelFormat::Gray16;
    }
    return color ? PixelFormat::Rgba32F : PixelFormat::Gray32F;
}

Directory readDirectory(TIFF* tiff) {
    Directory d;
    d.offset = TIFFCurrentDirOffset(tiff);
    TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &d.width);
    TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &d.height);
    TIFFGetFieldDefaulted(tiff, TIFFTAG_BITSPERSAMPLE, &d.bits);
    TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLESPERPIXEL, &d.samples);
    TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLEFORMAT, &d.sampleFormat);
    TIFFGetFieldDefaulted(tiff, TIFFTAG_PLANARCONFIG, &d.planar);
    TIFFGetFieldDefaulted(tiff, TIFFTAG_COMPRESSION, &d.compression);
    TIFFGetFieldDefaulted(tiff, TIFFTAG_SUBFILETYPE, &d.subfileType);
    TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &d.photometric);

    std::uint16_t orientation = ORIENTATION_TOPLEFT;
    TIFFGetFieldDefaulted(tiff, TIFFTAG_ORIENTATION, &orientation);
    if (orientation >= 1 && orientation <= 8) {
        d.orientation = static_cast<Orientation>(orientation);
    }

    std::uint32_t iccLength = 0;
    void* icc = nullptr;
    if (TIFFGetField(tiff, TIFFTAG_ICCPROFILE, &iccLength, &icc) && icc) {
        const auto* bytes = static_cast<const std::uint8_t*>(icc);
        d.iccProfile.assign(bytes, bytes + iccLength);
    }

    std::uint16_t subIfdCount = 0;
    toff_t* subIfds = nullptr;
    if (TIFFGetField(tiff, TIFFTAG_SUBIFD, &subIfdCount, &subIfds) && subIfds) {
        d.subIfds.assign(subIfds, subIfds + subIfdCount);
    }

    d.stored = describeStored(tiff, d);
    readBasicCameraTags(tiff, d);

    d.tiled = TIFFIsTiled(tiff) != 0;
    if (d.tiled) {
        TIFFGetField(tiff, TIFFTAG_TILEWIDTH, &d.tileWidth);
        TIFFGetField(tiff, TIFFTAG_TILELENGTH, &d.tileHeight);
    } else {
        TIFFGetFieldDefaulted(tiff, TIFFTAG_ROWSPERSTRIP, &d.rowsPerStrip);
        d.rowsPerStrip = std::clamp<std::uint32_t>(d.rowsPerStrip, 1, std::max<std::uint32_t>(d.height, 1));
    }

    if (d.width == 0 || d.height == 0 || d.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        d.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        d.problem = "invalid image size";
    } else if (d.tiled && (d.tileWidth == 0 || d.tileHeight == 0 || d.tileWidth > kMaxPhysicalTile ||
                           d.tileHeight > kMaxPhysicalTile)) {
        d.problem = "unsupported tile size";
    } else if (!TIFFIsCODECConfigured(d.compression)) {
        d.problem = "compression " + std::to_string(d.compression) + " is not available";
    } else if (canReadDirectly(d)) {
        d.format = directFormat(d);
    } else if (d.photometric == PHOTOMETRIC_SEPARATED && d.bits != 8) {
        // libtiff's check below accepts this, but reading then fails with a generic message.
        d.problem = std::to_string(d.bits) + "-bit CMYK is not supported";
    } else {
        char message[1024] = {};
        if (TIFFRGBAImageOK(tiff, message)) {
            d.useRgbaInterface = true;
            d.format = PixelFormat::Bgrx8;
        } else {
            d.problem = message[0] ? message : "unsupported pixel layout";
        }
    }
    return d;
}

// Writes one row of decoded samples in the output format.
template <typename In, typename Convert>
void convertRow(const std::uint8_t* input, std::size_t step, const Directory& d, int width, const ImageBuffer& out,
                int y, int firstX, Convert convert) {
    const auto read = [&](int x, int sample) {
        In value;
        std::memcpy(&value, input + (static_cast<std::size_t>(x) * d.samples + static_cast<std::size_t>(sample)) * step,
                    sizeof(In));
        return value;
    };
    const bool color = d.photometric == PHOTOMETRIC_RGB;

    switch (out.format()) {
    case PixelFormat::Bgrx8: {
        auto* row = reinterpret_cast<std::uint8_t*>(out.row(y)) + static_cast<std::size_t>(firstX) * 4;
        for (int x = 0; x < width; ++x, row += 4) {
            if (color) {
                row[0] = static_cast<std::uint8_t>(convert(read(x, 2)));
                row[1] = static_cast<std::uint8_t>(convert(read(x, 1)));
                row[2] = static_cast<std::uint8_t>(convert(read(x, 0)));
            } else {
                row[0] = row[1] = row[2] = static_cast<std::uint8_t>(convert(read(x, 0)));
            }
            row[3] = 255;
        }
        break;
    }
    case PixelFormat::Gray16: {
        auto* row = reinterpret_cast<std::uint16_t*>(out.row(y)) + firstX;
        for (int x = 0; x < width; ++x) {
            row[x] = static_cast<std::uint16_t>(convert(read(x, 0)));
        }
        break;
    }
    case PixelFormat::Rgba16: {
        auto* row = reinterpret_cast<std::uint16_t*>(out.row(y)) + static_cast<std::size_t>(firstX) * 4;
        for (int x = 0; x < width; ++x, row += 4) {
            row[0] = static_cast<std::uint16_t>(convert(read(x, 0)));
            row[1] = static_cast<std::uint16_t>(convert(read(x, 1)));
            row[2] = static_cast<std::uint16_t>(convert(read(x, 2)));
            row[3] = 65535;
        }
        break;
    }
    case PixelFormat::Gray32F: {
        auto* row = reinterpret_cast<float*>(out.row(y)) + firstX;
        for (int x = 0; x < width; ++x) {
            row[x] = static_cast<float>(convert(read(x, 0)));
        }
        break;
    }
    case PixelFormat::Rgba32F: {
        auto* row = reinterpret_cast<float*>(out.row(y)) + static_cast<std::size_t>(firstX) * 4;
        for (int x = 0; x < width; ++x, row += 4) {
            row[0] = static_cast<float>(convert(read(x, 0)));
            row[1] = static_cast<float>(convert(read(x, 1)));
            row[2] = static_cast<float>(convert(read(x, 2)));
            row[3] = 1.0F;
        }
        break;
    }
    }
}

// Decodes rows packed as the file stores them into the output buffer.
void convertRows(const std::uint8_t* input, std::size_t inputStride, const Directory& d, int width, int rows,
                 const ImageBuffer& out, int firstX, int firstY) {
    const bool invert = d.photometric == PHOTOMETRIC_MINISWHITE;
    for (int r = 0; r < rows; ++r) {
        const std::uint8_t* row = input + static_cast<std::size_t>(r) * inputStride;
        const int y = firstY + r;
        const bool isSigned = d.sampleFormat == SAMPLEFORMAT_INT;
        if (d.bits == 8) {
            convertRow<std::uint8_t>(row, 1, d, width, out, y, firstX, [&](std::uint8_t v) -> int {
                const int value = isSigned ? v ^ 0x80 : v;
                return invert ? 255 - value : value;
            });
        } else if (d.bits == 16) {
            convertRow<std::uint16_t>(row, 2, d, width, out, y, firstX, [&](std::uint16_t v) -> int {
                const int value = isSigned ? v ^ 0x8000 : v;
                return invert ? 65535 - value : value;
            });
        } else if (d.bits == 32 && d.sampleFormat == SAMPLEFORMAT_IEEEFP) {
            convertRow<float>(row, 4, d, width, out, y, firstX, [&](float v) { return invert ? -v : v; });
        } else if (d.bits == 32 && isSigned) {
            convertRow<std::int32_t>(row, 4, d, width, out, y, firstX,
                                     [&](std::int32_t v) { return invert ? -static_cast<double>(v) : static_cast<double>(v); });
        } else if (d.bits == 32) {
            convertRow<std::uint32_t>(row, 4, d, width, out, y, firstX,
                                      [&](std::uint32_t v) { return invert ? -static_cast<double>(v) : static_cast<double>(v); });
        } else {
            convertRow<double>(row, 8, d, width, out, y, firstX, [&](double v) { return invert ? -v : v; });
        }
    }
}

struct StripRun {
    int level = -1;
    int subimage = -1;
    std::uint32_t firstStrip = 0;
    std::uint32_t lastStrip = 0;
    int firstRow = 0;
    ImageBuffer rows;
};

class TiffSource final : public ImageSource {
public:
    TiffSource(std::unique_ptr<TiffHandle> handle, std::vector<SubimageInfo> subimages,
               std::vector<std::vector<Directory>> directories)
        : m_handle(std::move(handle)), m_subimages(std::move(subimages)), m_directories(std::move(directories)) {}

    std::span<const SubimageInfo> subimages() const override { return m_subimages; }

    Result<ImageBuffer> readTile(const TileCoord& coord, std::stop_token stop) override {
        if (coord.subimage < 0 || coord.subimage >= std::ssize(m_subimages)) {
            return std::unexpected("tile does not exist");
        }
        const auto& levels = m_subimages[static_cast<std::size_t>(coord.subimage)].levels;
        if (coord.level < 0 || coord.level >= std::ssize(levels)) {
            return std::unexpected("tile does not exist");
        }
        const LevelInfo& level = levels[static_cast<std::size_t>(coord.level)];
        const auto x64 = static_cast<std::int64_t>(coord.col) * level.tileSize.width;
        const auto y64 = static_cast<std::int64_t>(coord.row) * level.tileSize.height;
        if (coord.col < 0 || coord.row < 0 || x64 >= level.size.width || y64 >= level.size.height) {
            return std::unexpected("tile does not exist");
        }
        const int x = static_cast<int>(x64);
        const int y = static_cast<int>(y64);
        const int width = std::min(level.tileSize.width, level.size.width - x);
        const int height = std::min(level.tileSize.height, level.size.height - y);

        const auto& directory = m_directories[static_cast<std::size_t>(coord.subimage)][static_cast<std::size_t>(coord.level)];

        // A libtiff handle has one current directory and cannot be shared between threads.
        const std::scoped_lock lock(m_mutex);
        if (stop.stop_requested()) {
            return std::unexpected("cancelled");
        }
        if (m_currentOffset != directory.offset) {
            if (!TIFFSetSubDirectory(m_handle->get(), directory.offset)) {
                m_currentOffset = 0;
                return std::unexpected(m_handle->error("cannot read directory"));
            }
            m_currentOffset = directory.offset;
        }

        try {
            if (directory.tiled) {
                return readPhysicalTile(directory, coord.subimage, x, y, width, height);
            }
            const auto rows = stripRows(directory, coord, y, height, stop);
            if (!rows) {
                return std::unexpected(rows.error());
            }
            return (*rows)->rows.view(x, y - (*rows)->firstRow, width, height);
        } catch (const std::bad_alloc&) {
            return std::unexpected("not enough memory to read image");
        }
    }

private:
    Result<ImageBuffer> readPhysicalTile(const Directory& d, int subimage, int x, int y, int width, int height) {
        TIFF* tiff = m_handle->get();
        const PixelFormat format = m_subimages[static_cast<std::size_t>(subimage)].format;
        const ImageBuffer out = ImageBuffer::create({width, height}, format);

        if (d.useRgbaInterface) {
            std::vector<std::uint32_t> raster(static_cast<std::size_t>(d.tileWidth) * d.tileHeight);
            if (!TIFFReadRGBATileExt(tiff, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), raster.data(), 0)) {
                return std::unexpected(m_handle->error("cannot read tile"));
            }
            // The raster is bottom-up, with the image rows at the top of a partial tile.
            for (int r = 0; r < height; ++r) {
                copyRgbaRow(raster.data() + static_cast<std::size_t>(d.tileHeight - 1 - static_cast<std::uint32_t>(r)) * d.tileWidth,
                            width, out, 0, r);
            }
            return out;
        }

        const tmsize_t tileBytes = TIFFTileSize(tiff);
        if (tileBytes <= 0) {
            return std::unexpected(m_handle->error("invalid tile size"));
        }
        std::vector<std::uint8_t> raw(static_cast<std::size_t>(tileBytes));
        const std::size_t sampleBytes = d.bits / 8;

        if (d.planar == PLANARCONFIG_SEPARATE && d.samples > 1) {
            const int planes = d.photometric == PHOTOMETRIC_RGB ? 3 : 1;
            std::vector<std::uint8_t> interleaved(static_cast<std::size_t>(d.tileWidth) * d.tileHeight * d.samples * sampleBytes, 0);
            for (int plane = 0; plane < planes; ++plane) {
                const std::uint32_t tile = TIFFComputeTile(tiff, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 0,
                                                           static_cast<std::uint16_t>(plane));
                if (TIFFReadEncodedTile(tiff, tile, raw.data(), tileBytes) < 0) {
                    return std::unexpected(m_handle->error("cannot read tile"));
                }
                interleavePlane(raw.data(), interleaved.data(), static_cast<std::size_t>(d.tileWidth) * d.tileHeight, plane, d.samples, sampleBytes);
            }
            convertRows(interleaved.data(), static_cast<std::size_t>(d.tileWidth) * d.samples * sampleBytes, d, width, height, out, 0, 0);
            return out;
        }

        const std::uint32_t tile = TIFFComputeTile(tiff, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 0, 0);
        if (TIFFReadEncodedTile(tiff, tile, raw.data(), tileBytes) < 0) {
            return std::unexpected(m_handle->error("cannot read tile"));
        }
        convertRows(raw.data(), static_cast<std::size_t>(d.tileWidth) * d.samples * sampleBytes, d, width, height, out, 0, 0);
        return out;
    }

    static void interleavePlane(const std::uint8_t* plane, std::uint8_t* interleaved, std::size_t pixels, int index,
                                int samples, std::size_t sampleBytes) {
        for (std::size_t i = 0; i < pixels; ++i) {
            std::memcpy(interleaved + (i * static_cast<std::size_t>(samples) + static_cast<std::size_t>(index)) * sampleBytes,
                        plane + i * sampleBytes, sampleBytes);
        }
    }

    static void copyRgbaRow(const std::uint32_t* in, int width, const ImageBuffer& out, int firstX, int y) {
        auto* row = reinterpret_cast<std::uint8_t*>(out.row(y)) + static_cast<std::size_t>(firstX) * 4;
        for (int x = 0; x < width; ++x, row += 4) {
            row[0] = static_cast<std::uint8_t>(TIFFGetB(in[x]));
            row[1] = static_cast<std::uint8_t>(TIFFGetG(in[x]));
            row[2] = static_cast<std::uint8_t>(TIFFGetR(in[x]));
            row[3] = 255;
        }
    }

    // Strips cannot be split across tiles, so the strips under a tile are decoded together
    // and kept for the neighbouring tiles of the same rows.
    Result<const StripRun*> stripRows(const Directory& d, const TileCoord& coord, int y, int height, const std::stop_token& stop) {
        const std::uint32_t firstStrip = static_cast<std::uint32_t>(y) / d.rowsPerStrip;
        const std::uint32_t lastStrip = static_cast<std::uint32_t>(y + height - 1) / d.rowsPerStrip;

        for (const StripRun& run : m_stripRuns) {
            if (run.subimage == coord.subimage && run.level == coord.level && run.firstStrip == firstStrip &&
                run.lastStrip == lastStrip) {
                return &run;
            }
        }

        const int firstRow = static_cast<int>(firstStrip * d.rowsPerStrip);
        const int endRow = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(lastStrip + 1) * d.rowsPerStrip, d.height));
        const PixelFormat format = m_subimages[static_cast<std::size_t>(coord.subimage)].format;
        const auto bytesNeeded = static_cast<std::uint64_t>(d.width) * static_cast<std::uint64_t>(endRow - firstRow) *
                                 static_cast<std::uint64_t>(bytesPerPixel(format));
        if (bytesNeeded > kMaxDecodedBytes) {
            return std::unexpected("image strips are too large to decode");
        }

        ImageBuffer rows = ImageBuffer::create({static_cast<int>(d.width), endRow - firstRow}, format);
        for (std::uint32_t strip = firstStrip; strip <= lastStrip; ++strip) {
            if (stop.stop_requested()) {
                return std::unexpected("cancelled");
            }
            const auto decoded = readStrip(d, strip, rows, static_cast<int>(strip * d.rowsPerStrip) - firstRow);
            if (!decoded) {
                return std::unexpected(decoded.error());
            }
        }

        if (m_stripRuns.size() >= kCachedStripRuns) {
            m_stripRuns.erase(m_stripRuns.begin());
        }
        m_stripRuns.push_back({coord.level, coord.subimage, firstStrip, lastStrip, firstRow, std::move(rows)});
        return &m_stripRuns.back();
    }

    Result<bool> readStrip(const Directory& d, std::uint32_t strip, const ImageBuffer& out, int outRow) {
        TIFF* tiff = m_handle->get();
        const int stripRow = static_cast<int>(strip * d.rowsPerStrip);
        const int rows = std::min(static_cast<int>(d.rowsPerStrip), static_cast<int>(d.height) - stripRow);

        if (d.useRgbaInterface) {
            std::vector<std::uint32_t> raster(static_cast<std::size_t>(d.width) * d.rowsPerStrip);
            if (!TIFFReadRGBAStripExt(tiff, static_cast<std::uint32_t>(stripRow), raster.data(), 0)) {
                return std::unexpected(m_handle->error("cannot read strip"));
            }
            // The raster is bottom-up, and a short last strip starts at the buffer's start.
            for (int r = 0; r < rows; ++r) {
                copyRgbaRow(raster.data() + static_cast<std::size_t>(rows - 1 - r) * d.width,
                            static_cast<int>(d.width), out, 0, outRow + r);
            }
            return true;
        }

        const tmsize_t stripBytes = TIFFStripSize(tiff);
        if (stripBytes <= 0) {
            return std::unexpected(m_handle->error("invalid strip size"));
        }
        const std::size_t sampleBytes = d.bits / 8;
        std::vector<std::uint8_t> raw(static_cast<std::size_t>(stripBytes));

        if (d.planar == PLANARCONFIG_SEPARATE && d.samples > 1) {
            const std::size_t pixels = static_cast<std::size_t>(d.width) * static_cast<std::size_t>(rows);
            std::vector<std::uint8_t> interleaved(pixels * d.samples * sampleBytes, 0);
            const int planes = d.photometric == PHOTOMETRIC_RGB ? 3 : 1;
            for (int plane = 0; plane < planes; ++plane) {
                const std::uint32_t index = TIFFComputeStrip(tiff, static_cast<std::uint32_t>(stripRow), static_cast<std::uint16_t>(plane));
                if (TIFFReadEncodedStrip(tiff, index, raw.data(), stripBytes) < 0) {
                    return std::unexpected(m_handle->error("cannot read strip"));
                }
                interleavePlane(raw.data(), interleaved.data(), pixels, plane, d.samples, sampleBytes);
            }
            convertRows(interleaved.data(), static_cast<std::size_t>(d.width) * d.samples * sampleBytes, d,
                        static_cast<int>(d.width), rows, out, 0, outRow);
            return true;
        }

        const std::uint32_t index = TIFFComputeStrip(tiff, static_cast<std::uint32_t>(stripRow), 0);
        if (TIFFReadEncodedStrip(tiff, index, raw.data(), stripBytes) < 0) {
            return std::unexpected(m_handle->error("cannot read strip"));
        }
        convertRows(raw.data(), static_cast<std::size_t>(d.width) * d.samples * sampleBytes, d, static_cast<int>(d.width),
                    rows, out, 0, outRow);
        return true;
    }

    std::unique_ptr<TiffHandle> m_handle;
    std::vector<SubimageInfo> m_subimages;
    std::vector<std::vector<Directory>> m_directories;
    std::mutex m_mutex;
    std::uint64_t m_currentOffset = 0;
    std::vector<StripRun> m_stripRuns;
};

Size tileSizeFor(const Directory& d) {
    if (d.tiled) {
        return {static_cast<int>(d.tileWidth), static_cast<int>(d.tileHeight)};
    }
    // Whole strips where possible, so one tile decodes as few strips as it can. Strips taller
    // than a tile have to be cut, because a tile must fit one texture.
    const std::uint32_t height = d.rowsPerStrip <= kStripTileSize
                                     ? d.rowsPerStrip * ((kStripTileSize + d.rowsPerStrip - 1) / d.rowsPerStrip)
                                     : kStripTileSize;
    return {kStripTileSize, static_cast<int>(height)};
}

// Turns a page and the reduced images that belong to it into one subimage.
void addPage(std::vector<Directory> levels, std::vector<SubimageInfo>& subimages,
             std::vector<std::vector<Directory>>& directories, std::string& firstProblem) {
    Directory main = levels.front();
    if (!main.problem.empty()) {
        if (firstProblem.empty()) {
            firstProblem = main.problem;
        }
        return;
    }

    // Files are not required to list reduced images in order, or to be consistent.
    std::ranges::stable_sort(levels, [](const Directory& a, const Directory& b) { return a.width > b.width; });
    std::vector<Directory> usable;
    for (Directory& level : levels) {
        const bool sameContent = level.problem.empty() && level.format == main.format;
        const bool smaller = usable.empty() || (level.width < usable.back().width && level.height < usable.back().height);
        if (sameContent && smaller && level.width <= main.width && level.height <= main.height) {
            usable.push_back(std::move(level));
        }
    }

    SubimageInfo info;
    info.role = SubimageRole::Page;
    info.format = main.format;
    info.orientation = main.orientation;
    info.iccProfile = main.iccProfile;
    info.stored = main.stored;
    info.exif = main.exif;
    info.exif.orientation = main.orientation;
    info.xmp = main.xmp;
    info.stored.storedLevels = static_cast<int>(usable.size());
    for (const Directory& level : usable) {
        info.levels.push_back({{static_cast<int>(level.width), static_cast<int>(level.height)}, tileSizeFor(level), false});
    }
    subimages.push_back(std::move(info));
    directories.push_back(std::move(usable));
}

}  // namespace

bool isTiff(std::span<const std::uint8_t> h) {
    if (h.size() < 4) {
        return false;
    }
    const bool little = h[0] == 'I' && h[1] == 'I' && (h[2] == 42 || h[2] == 43) && h[3] == 0;
    const bool big = h[0] == 'M' && h[1] == 'M' && h[2] == 0 && (h[3] == 42 || h[3] == 43);
    return little || big;
}

Result<std::shared_ptr<ImageSource>> openTiffSource(const std::filesystem::path& path) {
    auto handle = std::make_unique<TiffHandle>();
    if (!handle->open(path)) {
        return std::unexpected(handle->error("cannot open TIFF file"));
    }
    TIFF* tiff = handle->get();

    // Walk the main chain first. Following SubIFDs moves away from it.
    std::vector<std::vector<Directory>> pages;
    do {
        Directory directory = readDirectory(tiff);
        const bool isMask = (directory.subfileType & FILETYPE_MASK) != 0;
        const bool isReduced = (directory.subfileType & FILETYPE_REDUCEDIMAGE) != 0;
        if (isMask) {
            continue;
        }
        if (isReduced && !pages.empty()) {
            pages.back().push_back(std::move(directory));
        } else {
            pages.push_back({std::move(directory)});
        }
    } while (TIFFReadDirectory(tiff));

    for (auto& page : pages) {
        const std::vector<std::uint64_t> subIfds = page.front().subIfds;
        for (const std::uint64_t offset : subIfds) {
            if (TIFFSetSubDirectory(tiff, offset)) {
                Directory directory = readDirectory(tiff);
                if ((directory.subfileType & FILETYPE_REDUCEDIMAGE) != 0) {
                    page.push_back(std::move(directory));
                }
            }
        }
    }

    for (auto& page : pages) {
        Directory& main = page.front();
        if (main.exifOffset != 0) {
            readExifDirectory(tiff, main.exif, main.exifOffset);
        }
    }

    std::vector<SubimageInfo> subimages;
    std::vector<std::vector<Directory>> directories;
    std::string firstProblem;
    for (auto& page : pages) {
        addPage(std::move(page), subimages, directories, firstProblem);
    }
    if (subimages.empty()) {
        return std::unexpected(firstProblem.empty() ? std::string("no readable images in file") : firstProblem);
    }
    return std::make_shared<TiffSource>(std::move(handle), std::move(subimages), std::move(directories));
}

}  // namespace iv
