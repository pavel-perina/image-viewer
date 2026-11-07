#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "core/image_buffer.h"
#include "core/image_source.h"

namespace iv {

enum class ChromaSubsampling {
    Full,        // 4:4:4, for graphics and text
    Horizontal,  // 4:2:2
    Both,        // 4:2:0, usual for photos
};

struct JpegWriteOptions {
    int quality = 90;  // 1-100
    ChromaSubsampling chroma = ChromaSubsampling::Both;
    bool progressive = true;
};

struct PngWriteOptions {
    int compressionLevel = 6;  // 0-9, only speed and size, never quality
};

enum class TiffCompression { None, Lzw, Deflate };

struct TiffWriteOptions {
    TiffCompression compression = TiffCompression::Deflate;
};

struct WebpWriteOptions {
    bool lossless = false;
    int quality = 85;  // 0-100; with lossless, how hard it tries to make the file small
    int effort = 4;    // 0-6, slower is smaller
};

struct AvifWriteOptions {
    int quality = 70;  // 0-100, lossy only
    int speed = 6;     // 0-10, faster is larger
    ChromaSubsampling chroma = ChromaSubsampling::Both;
};

struct JxlWriteOptions {
    bool lossless = false;
    float distance = 1.0F;  // 0.1-25, lossy only; 1 is visually lossless
    int effort = 7;         // 1-9, slower is smaller
};

// Which format to write follows from the options given.
using WriteOptions =
    std::variant<JpegWriteOptions, PngWriteOptions, TiffWriteOptions, WebpWriteOptions, AvifWriteOptions, JxlWriteOptions>;

struct WriteMetadata {
    std::vector<std::uint8_t> iccProfile;  // empty for sRGB
    // A TIFF structure holding EXIF, with or without the "Exif\0\0" a JPEG puts before it. The
    // pixels are written as given, so the orientation is reset to 1 and the dimensions set.
    std::vector<std::uint8_t> exif;
    // An XMP packet, written as it is apart from its orientation, which is reset to 1.
    std::vector<std::uint8_t> xmp;
};

// Encodes the image in the format the options choose. Each format keeps as much as it can hold:
// 16-bit and float images stay so in TIFF, 16-bit in PNG and JPEG XL, 10-bit in AVIF, and are
// reduced to 8 bits for JPEG and WebP. Float values are taken as 0 to 1 where they must be
// reduced. 8-bit images whose pixels are all gray are written as grayscale where the format
// allows. The unused fourth channel is never written.
Result<std::vector<std::uint8_t>> encodeImage(const ImageBuffer& image, const WriteMetadata& metadata,
                                              const WriteOptions& options);

// Encodes and writes the file through a temporary one, replacing an existing file only with
// replace set.
Result<void> writeImage(const std::filesystem::path& path, const ImageBuffer& image, const WriteMetadata& metadata,
                        const WriteOptions& options, bool replace);

// Whether this build can write the format these options choose.
bool canWrite(const WriteOptions& options);

// The usual extension, without the dot, such as "jpg".
std::string_view extensionFor(const WriteOptions& options);

// Default options for the format a file name's extension names, or nothing if none can be written.
std::optional<WriteOptions> optionsForExtension(const std::filesystem::path& path);

}  // namespace iv
