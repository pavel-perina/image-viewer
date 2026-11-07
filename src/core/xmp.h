#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace iv {

// What precedes an XMP packet in a JPEG APP1 marker.
inline constexpr std::string_view kXmpJpegPrefix{"http://ns.adobe.com/xap/1.0/\0", 29};

// For pixels stored upright: sets every tiff:Orientation in an XMP packet to 1, in place and
// keeping the size, in both the attribute form tiff:Orientation="6" and the element form
// <tiff:Orientation>6</tiff:Orientation>. Nothing else in the packet is parsed or changed, so
// stored dimensions stay as they were. A packet that binds the TIFF namespace to a prefix other
// than "tiff" is left alone, as is any value that is not a single digit.
void resetXmpOrientation(std::span<std::uint8_t> packet);

// Removes the GPS properties of the EXIF schema (exif:GPSLatitude, exif:GPSLongitude and the
// rest) by overwriting each, attribute or element, with spaces, which XML allows there, so the
// packet keeps its size and stays valid. Place names typed in as text are not location data
// in this sense and stay.
void removeXmpLocation(std::span<std::uint8_t> packet);

}  // namespace iv
