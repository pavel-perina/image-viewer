#pragma once

#include <optional>
#include <span>

#include "core/crop.h"
#include "core/image_buffer.h"
#include "core/rgba.h"

namespace iv {

// Putting pictures on a canvas of their own, each over its shadow: what a strip, a contact
// sheet or a frame around one picture is made of.

struct CanvasLook {
    // What the canvas is filled with; none leaves it transparent, and the result has alpha.
    std::optional<Rgba8> background = Rgba8{255, 255, 255, 255};
    // How far a black shadow reaches right of and below each picture, in pixels; 0 for none.
    // It is what the picture covers, moved by a third of that and blurred with a sigma of two
    // ninths, so it reaches no further (the offset and three sigmas); a faint edge of it shows
    // above and left of the picture, as with a soft light.
    int shadow = 0;
    double shadowStrength = 0.75;  // how dark it is under the picture, from 0 to 1
};

struct PlacedPicture {
    ImageBuffer pixels;  // Bgrx8; with alpha, only what it covers casts a shadow
    int x = 0;
    int y = 0;
};

// A new Bgrx8 canvas of the given size with the pictures on it, in order, each over its own
// and the others' shadows. What reaches beyond the canvas is cut off.
ImageBuffer composeCanvas(Size size, std::span<const PlacedPicture> pictures, const CanvasLook& look);

}  // namespace iv
