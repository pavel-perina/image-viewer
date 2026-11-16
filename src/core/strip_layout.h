#pragma once

#include <span>
#include <vector>

#include "core/crop.h"
#include "core/geometry.h"

namespace iv {

// Where pictures go when they are put side by side in one: in a row or a column, or wrapped
// into several, as a contact sheet is. Worked out from their sizes alone, before anything is
// decoded, so the size of the result can be shown first.

enum class StripDirection { Across, Down };

struct StripOptions {
    StripDirection direction = StripDirection::Across;
    // The height every picture is brought to across, or the width down. The other side follows
    // each picture's ratio.
    int extent = 200;
    // A picture already smaller keeps its size, centred in its row or column.
    bool onlyReduce = false;
    int spacing = 0;  // between pictures, and between rows or columns
    int margin = 0;   // around the whole
    // How far a shadow reaches beyond the right and bottom of a picture, kept free for it.
    int shadow = 0;
    // Pictures in a row (across) or column (down) before the next one starts; 0 for one only.
    int perLine = 0;
    bool operator==(const StripOptions&) const = default;
};

struct StripLayout {
    Size canvas;
    // Each picture's place on the canvas, at its new size, in the order given.
    std::vector<PixelRect> pictures;
};

// The pictures' sizes are as they are shown, upright. A size of nothing counts as one pixel.
StripLayout layoutStrip(std::span<const Size> pictures, const StripOptions& options);

}  // namespace iv
