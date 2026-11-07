#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/image_source.h"

namespace iv {

// One line of text about an image, ready to display.
struct DetailRow {
    std::string section;
    std::string label;
    std::string value;
};

std::vector<DetailRow> describeImage(const SubimageInfo& subimage);

// One compact line of each, for where there is no room for the rows above: "400x300 0.12MP 24bit
// PNG", and "1/60s f/4 ISO-500 EV -0.33", which is empty without any of these.
std::string imageSummary(const SubimageInfo& subimage);
std::string cameraSummary(const ExifData& exif);

// "780 B", "12 kB", "1.5 MB"; decimal units, as file managers show them.
std::string shortDataSize(std::uint64_t bytes);

// "4:2:0" from the sampling factors of the luma and chroma components.
std::string chromaSubsamplingName(int lumaH, int lumaV, int chromaH, int chromaV);

}  // namespace iv
