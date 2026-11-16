#pragma once

#include <vector>

#include "core/rgba.h"

namespace iv {

// What a colour chooser needs: a colour as lightness, chroma and hue, and the colours each of
// its sliders passes through, so that every track shows what moving it would give.

// Oklab in polar form: L from 0 to 1, C from 0, H in degrees from 0 to 360.
struct Oklch {
    double L = 0;
    double C = 0;
    double H = 0;
    bool operator==(const Oklch&) const = default;
};

// The most chroma a slider offers; sRGB reaches about 0.32, in its blues and magentas.
inline constexpr double kMaxChooserChroma = 0.37;

// The colour's lightness, chroma and hue; a gray has chroma 0 and hue 0.
Oklch toOklch(Rgba8 colour);

enum class ColorChannel { Red, Green, Blue, Lightness, Chroma, Hue };

struct RampStop {
    Rgba8 colour;          // as sRGB shows it
    bool outside = false;  // sRGB cannot show it as asked, and its chroma was reduced
};

// The colours a slider of one channel passes through, from its least to its most, with the
// other channels held: those of rgb for red, green and blue, those of lch for the others, so
// that a gray keeps the hue it was given. Count stops, at least 2.
std::vector<RampStop> channelRamp(ColorChannel channel, Rgba8 rgb, const Oklch& lch, int count);

}  // namespace iv
