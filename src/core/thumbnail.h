#pragma once

#include <filesystem>
#include <stop_token>

#include "core/color.h"
#include "core/image_buffer.h"
#include "core/image_source.h"

namespace iv {

// A copy of the image, upright, whose longer edge is at most maxEdge. Decodes the smallest
// pyramid level that is large enough, and never enlarges. Blocks, so call it from a worker.
Result<ImageBuffer> makeThumbnail(const std::filesystem::path& path, int maxEdge, std::stop_token stop,
                                  const ColorTarget& target = {});

// The image upright at the given size, as it is shown, in sRGB 8-bit (Bgrx8) with its alpha
// kept: what a strip, or anything made for a web page, is put together from. Decodes the
// smallest level that is large enough; a size larger than the image enlarges it. A deep image
// is brought to 8 bits as it is shown by default. Blocks, so call it from a worker.
Result<ImageBuffer> makeSrgbCopy(const std::filesystem::path& path, Size size, std::stop_token stop);

}  // namespace iv
