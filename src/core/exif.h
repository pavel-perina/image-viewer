#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/geometry.h"

namespace iv {

// The EXIF fields worth showing, which come from a JPEG APP1 block or from TIFF tags.
struct ExifData {
    bool present = false;  // whether a valid block was found at all
    Orientation orientation = Orientation::Normal;
    std::string make;
    std::string model;
    std::string lens;
    std::string dateTaken;  // "YYYY-MM-DD HH:MM:SS", empty if unknown
    std::optional<double> exposureSeconds;
    std::optional<double> fNumber;
    std::optional<double> focalLength;     // millimetres
    std::optional<double> focalLength35mm;  // millimetres, the equivalent on 35mm film
    std::optional<double> exposureBias;     // EV
    std::optional<int> iso;
    bool adobeRgb = false;  // a camera set to Adobe RGB, which says so here instead of a profile
};

// Reads a TIFF structure holding EXIF, with or without the "Exif\0\0" that precedes it in a
// JPEG APP1 marker. The data is untrusted; whatever is malformed is left out.
ExifData parseExif(std::span<const std::uint8_t> app1);

// Turns "2024:05:01 12:30:00" into "2024-05-01 12:30:00". Cameras write blanks or zeros for
// an unset clock, and those and anything else malformed give an empty string.
std::string normalizeExifDate(std::string_view date);

// Removes what cameras pad text fields with: trailing NULs and spaces, control characters.
std::string cleanExifText(std::string_view text);

}  // namespace iv
