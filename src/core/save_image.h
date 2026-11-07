#pragma once

#include <filesystem>
#include <stop_token>

#include "core/image_source.h"
#include "core/image_writer.h"

namespace iv {

struct SaveOptions {
    bool keepMetadata = true;     // EXIF and XMP; the colour profile is always kept
    bool removeLocation = false;  // the GPS position, from both, when metadata is kept
};

// Writes a subimage of an opened file to a new one: its full-size pixels turned upright, so
// that it looks as shown, with its colour profile and, as chosen, its EXIF and XMP. The pixels
// keep their colour space and as much of their depth as the format holds.
Result<void> saveSubimage(ImageSource& source, int subimage, const std::filesystem::path& path,
                          const WriteOptions& options, const SaveOptions& saveOptions, bool replace,
                          const std::stop_token& stop = {});

// Whether a subimage is small enough to be saved, as it is held in memory whole, more than once.
bool canSaveWhole(const SubimageInfo& subimage);

}  // namespace iv
