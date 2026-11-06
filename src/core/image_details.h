#pragma once

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

// "4:2:0" from the sampling factors of the luma and chroma components.
std::string chromaSubsamplingName(int lumaH, int lumaV, int chromaH, int chromaV);

}  // namespace iv
