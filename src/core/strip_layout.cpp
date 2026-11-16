#include "core/strip_layout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace iv {

namespace {

// The size a picture is brought to, as length along the line and thickness across it.
struct Placed {
    int along = 1;
    int across = 1;
};

Placed placedSize(Size picture, const StripOptions& options) {
    const bool acrossDirection = options.direction == StripDirection::Across;
    // Along the line is the side that follows the ratio; across it is the shared extent.
    const int along = std::max(1, acrossDirection ? picture.width : picture.height);
    const int across = std::max(1, acrossDirection ? picture.height : picture.width);
    const int extent = std::max(1, options.extent);
    if (options.onlyReduce && across <= extent) {
        return {along, across};
    }
    const double scale = static_cast<double>(extent) / across;
    return {std::max(1, static_cast<int>(std::lround(along * scale))), extent};
}

}  // namespace

StripLayout layoutStrip(std::span<const Size> pictures, const StripOptions& options) {
    const bool acrossDirection = options.direction == StripDirection::Across;
    const int spacing = std::max(0, options.spacing);
    const int margin = std::max(0, options.margin);
    const int shadow = std::max(0, options.shadow);
    const std::size_t perLine = options.perLine > 0 ? static_cast<std::size_t>(options.perLine) : pictures.size();

    std::vector<Placed> placed;
    placed.reserve(pictures.size());
    for (const Size& picture : pictures) {
        placed.push_back(placedSize(picture, options));
    }

    // Lines are laid out along the direction, and stacked across it. Coordinates here are
    // along and across, and turned into x and y at the end.
    StripLayout layout;
    layout.pictures.resize(pictures.size());
    int longestLine = 0;
    int lineStart = margin;  // across
    for (std::size_t first = 0; first < placed.size(); first += perLine) {
        const std::size_t end = std::min(placed.size(), first + perLine);
        int thickness = 0;
        for (std::size_t i = first; i < end; ++i) {
            thickness = std::max(thickness, placed[i].across);
        }
        int position = margin;  // along
        for (std::size_t i = first; i < end; ++i) {
            const int offset = (thickness - placed[i].across) / 2;  // centred across the line
            const int along = position;
            const int across = lineStart + offset;
            layout.pictures[i] = acrossDirection ? PixelRect{along, across, placed[i].along, placed[i].across}
                                                 : PixelRect{across, along, placed[i].across, placed[i].along};
            position += placed[i].along + shadow + (i + 1 < end ? spacing : 0);
        }
        longestLine = std::max(longestLine, position - margin);
        lineStart += thickness + shadow + (end < placed.size() ? spacing : 0);
    }
    const int along = longestLine + 2 * margin;
    const int across = placed.empty() ? 2 * margin : lineStart + margin;
    layout.canvas = acrossDirection ? Size{along, across} : Size{across, along};
    return layout;
}

}  // namespace iv
