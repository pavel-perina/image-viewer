#pragma once

#include <array>
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

// Up to three frames around one picture, outer first, then a shadow under it all. A frame of
// width 0 is left out. Colours are 8-bit and opaque.
struct FrameLook {
    struct Frame {
        int width = 0;
        Rgba8 colour{255, 255, 255, 255};
        bool operator==(const Frame&) const = default;
    };
    std::array<Frame, 3> frames;
    int shadow = 0;            // as CanvasLook has it
    bool transparent = false;  // the shadow on transparency rather than on white
    bool operator==(const FrameLook&) const = default;
};

// The size of the framed picture: the frames on every side, and with a shadow, its reach right
// and below and its faint edge (a third of it) left and above, so that none of it is cut off.
Size framedSize(Size picture, const FrameLook& look);
// Where the picture lies in it.
PixelRect framedPicture(Size picture, const FrameLook& look);
// Whether framing makes a gray picture a colour one: a frame has a colour, or the shadow is on
// transparency, which gray pictures cannot have.
bool framedInColour(const FrameLook& look);
// Whether the framed picture has alpha: only a shadow on transparency leaves any.
bool framedWithAlpha(const FrameLook& look);

// The picture in its frames, over its shadow, in the picture's own format, so that a 16-bit
// picture stays 16-bit. A gray one becomes colour as framedInColour says. A picture with
// alpha is put on white first, as a frame stands for an opaque print. The frame colours are
// values of the format's full range, as an annotation's are.
ImageBuffer framed(const ImageBuffer& picture, const FrameLook& look);

}  // namespace iv
