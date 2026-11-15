#pragma once

#include <optional>

namespace iv {

// Makes the bundled Inter the font of every window, at the size the system asks for, or at
// pointSize when given (the tests, so that they see one size wherever they run). What Inter
// lacks, such as Chinese file names, comes from the system's fonts. Its digits are all as wide
// (tabular figures). Call once the application
// exists, before any window.
void useProgramFont(std::optional<double> pointSize = std::nullopt);

}  // namespace iv
