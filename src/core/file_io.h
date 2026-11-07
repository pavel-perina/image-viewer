#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "core/image_source.h"

namespace iv {

Result<std::vector<std::uint8_t>> readFile(const std::filesystem::path& path);

// Writes a new file, or replaces one, through a temporary file in the same folder, so that the
// target is never left half written. A replaced file keeps its permissions. With replace off,
// fails if the target exists.
Result<void> writeFileSafely(const std::filesystem::path& path, std::span<const std::uint8_t> bytes, bool replace);

}  // namespace iv
