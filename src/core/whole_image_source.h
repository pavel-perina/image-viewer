#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>

#include "core/image_source.h"

namespace iv {

// Decodes the full picture, in the format of the subimage. Called at most once successfully.
using WholeImageDecoder = std::function<Result<ImageBuffer>(const std::stop_token&)>;

// A source for formats that cannot be entered at a tile or scaled while decoding. It holds
// one level, decodes it when first needed, and serves tiles as views of the result.
std::shared_ptr<ImageSource> makeWholeImageSource(SubimageInfo info, Size size, WholeImageDecoder decoder);

// Refuses sizes that could not be allocated, rather than risk the OOM killer.
bool fitsInMemoryLimit(Size size, PixelFormat format);

}  // namespace iv
