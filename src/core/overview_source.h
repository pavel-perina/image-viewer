#pragma once

#include <memory>

#include "core/image_source.h"

namespace iv {

// Adds generated, reduced resolution levels to subimages whose smallest level is still large,
// such as a flat TIFF, so that fitting one to the window does not need every full size tile
// on the GPU. They are built the first time they are needed, by reading the smallest real
// level once, and need only a fraction of its memory.
// Returns the source unchanged if none of its subimages need this.
std::shared_ptr<ImageSource> withOverviews(std::shared_ptr<ImageSource> source);

}  // namespace iv
