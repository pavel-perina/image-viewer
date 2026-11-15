#include "core/save_image.h"

#include <cstdint>
#include <new>

#include "core/exif.h"
#include "core/image_ops.h"
#include "core/xmp.h"

namespace iv {

namespace {

// The decoded image, its turned copy and the writer's packed copy are held at once.
constexpr std::uint64_t kMaxSavedBytes = 2ull * 1024 * 1024 * 1024;

}  // namespace

bool canSaveWhole(const SubimageInfo& subimage) {
    if (subimage.levels.empty()) {
        return false;
    }
    const Size size = subimage.levels.front().size;
    return static_cast<std::uint64_t>(size.width) * static_cast<std::uint64_t>(size.height) *
               static_cast<std::uint64_t>(bytesPerPixel(subimage.format)) <=
           kMaxSavedBytes;
}

Result<void> saveSubimage(ImageSource& source, int subimage, const std::filesystem::path& path,
                          const WriteOptions& options, const SaveOptions& saveOptions, bool replace,
                          const std::stop_token& stop) {
    const auto subimages = source.subimages();
    if (subimage < 0 || subimage >= static_cast<int>(subimages.size())) {
        return std::unexpected("there is no such page");
    }
    const SubimageInfo& info = subimages[static_cast<std::size_t>(subimage)];
    if (!canSaveWhole(info)) {
        return std::unexpected("the image is too large to be saved whole");
    }
    if (!canWrite(options)) {
        return std::unexpected("this build cannot write " + std::string(extensionFor(options)) + " files");
    }

    WriteMetadata metadata;
    metadata.iccProfile = info.iccProfile;
    if (saveOptions.keepMetadata) {
        metadata.exif = info.rawExif;
        metadata.xmp = info.xmp;
        if (saveOptions.removeLocation) {
            removeExifLocation(metadata.exif);
            removeXmpLocation(metadata.xmp);
        }
    }

    try {
        const auto pixels = readLevel(source, subimage, 0, stop);
        if (!pixels) {
            return std::unexpected(pixels.error());
        }
        if (stop.stop_requested()) {
            return std::unexpected("cancelled");
        }
        // Flattened until the writers keep alpha.
        return writeImage(path, applyOrientation(flattened(*pixels), info.orientation), metadata, options, replace);
    } catch (const std::bad_alloc&) {
        return std::unexpected("not enough memory to save the image");
    }
}

}  // namespace iv
