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

}  // namespace iv
