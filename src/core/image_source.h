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

#include "core/disk_gate.h"
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
    std::string colorModel;         // "RGB", "Grayscale", "YCbCr", "Palette", "RGB + alpha"
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
    // Whether its tiles have alpha, as ImageBuffer::hasAlpha says.
    bool alpha = false;
    Orientation orientation = Orientation::Normal;
    // Embedded, or made from what the file says about its colours. Empty means sRGB.
    std::vector<std::uint8_t> iccProfile;
    std::vector<LevelInfo> levels;
    std::chrono::milliseconds frameDelay{0};
    StoredFormat stored;
    ExifData exif;
    // The metadata as stored, to be written out again: EXIF as a TIFF structure without the
    // "Exif\0\0" before it, and the XMP packet. Empty when absent. A TIFF file's own EXIF
    // directory is not kept, as libtiff has no bytes of it to hand over.
    std::vector<std::uint8_t> rawExif;
    std::vector<std::uint8_t> xmp;
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

    // A quick likeness of a level, at exactly its size, to show while the level itself is being
    // made, such as a JPEG decoded at a reduced scale. It is not the level: it may be filtered
    // differently. Sources without one say so.
    virtual Result<ImageBuffer> roughLevel(int subimage, Size size, const std::stop_token& stop) {
        (void)subimage;
        (void)size;
        (void)stop;
        return std::unexpected("no rough level");
    }
};

// Detects the format from the file contents, not its extension. Formats decoded from memory are
// read whole through the DiskGate, at that priority; a TIFF reads parts as needed, ungated.
Result<std::shared_ptr<ImageSource>> openImageSource(const std::filesystem::path& path,
                                                     ReadPriority priority = ReadPriority::Foreground,
                                                     std::stop_token stop = {});

// A whole level of a subimage in one buffer, put together from its tiles.
Result<ImageBuffer> readLevel(ImageSource& source, int subimage, int level, const std::stop_token& stop);

// Lowercase file extensions, without the dot, of the formats that can be opened.
std::span<const std::string_view> supportedExtensions();

}  // namespace iv
