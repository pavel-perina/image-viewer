#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "core/exif.h"
#include "core/geometry.h"
#include "core/image_buffer.h"

namespace iv {

template <typename T>
using Result = std::expected<T, std::string>;

// One resolution of a subimage. Level 0 is full size; each following level is smaller.
struct LevelInfo {
    Size size;
    Size tileSize;
    bool generated = false;  // computed from a larger level rather than stored in the file
};

enum class SubimageRole {
    Page,            // e.g. a page of a multipage TIFF
    AnimationFrame,  // e.g. a frame of a GIF, WebP or JPEG XL
};

// How a picture is stored in its file, which is not necessarily how it is decoded.
struct StoredFormat {
    std::string container;          // "JPEG", "TIFF"
    std::string compression;        // with whether it is lossy, such as "LZW, lossless"
    std::string colorModel;         // "RGB", "Grayscale", "YCbCr", "Palette", "RGB + alpha (not shown)"
    std::string chromaSubsampling;  // "4:2:0", empty if not applicable
    int bitsPerSample = 8;
    bool floatSamples = false;
    bool signedSamples = false;
    int storedLevels = 1;  // resolutions present in the file; scaled JPEG decoding does not count
    bool animated = false;  // has more frames, of which only the first is shown so far
};

// A picture inside a file. Pages and animation frames are both subimages; reduced
// resolution copies of a picture are levels of one subimage.
struct SubimageInfo {
    SubimageRole role = SubimageRole::Page;
    PixelFormat format = PixelFormat::Bgrx8;
    Orientation orientation = Orientation::Normal;
    std::vector<std::uint8_t> iccProfile;  // empty when the file has no embedded profile
    std::vector<LevelInfo> levels;
    std::chrono::milliseconds frameDelay{0};
    StoredFormat stored;
    ExifData exif;
};

struct TileCoord {
    int subimage = 0;
    int level = 0;
    int col = 0;
    int row = 0;
};

// An opened image file. Everything is expressed as tiles of pyramid levels, so a viewer
// handles a phone JPEG and a gigapixel TIFF the same way. Safe to use from several threads.
class ImageSource {
public:
    virtual ~ImageSource() = default;

    virtual std::span<const SubimageInfo> subimages() const = 0;

    // May block on I/O and decoding, so call this from a worker thread.
    virtual Result<ImageBuffer> readTile(const TileCoord& coord, std::stop_token stop) = 0;
};

// Detects the format from the file contents, not its extension.
Result<std::shared_ptr<ImageSource>> openImageSource(const std::filesystem::path& path);

// Lowercase file extensions, without the dot, of the formats that can be opened.
std::span<const std::string_view> supportedExtensions();

}  // namespace iv
