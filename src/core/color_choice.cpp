#include "core/color_choice.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/oklab.h"
#include "core/palette.h"

namespace iv {

Oklch toOklch(Rgba8 colour) {
    const Oklab lab = linearToOklab(srgbDecode(colour.r / 255.0), srgbDecode(colour.g / 255.0), srgbDecode(colour.b / 255.0));
    const double chroma = std::hypot(lab.a, lab.b);
    if (chroma < 1e-4) {
        return {lab.L, 0, 0};
    }
    double hue = std::atan2(lab.b, lab.a) * 180 / std::numbers::pi;
    if (hue < 0) {
        hue += 360;
    }
    return {lab.L, chroma, hue};
}

std::vector<RampStop> channelRamp(ColorChannel channel, Rgba8 rgb, const Oklch& lch, int count) {
    count = std::max(2, count);
    std::vector<RampStop> ramp;
    ramp.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / (count - 1);
        const auto level = static_cast<std::uint8_t>(std::lround(t * 255));
        Rgba8 shown = rgb;
        shown.a = 255;
        Oklch asked = lch;
        switch (channel) {
        case ColorChannel::Red: shown.r = level; break;
        case ColorChannel::Green: shown.g = level; break;
        case ColorChannel::Blue: shown.b = level; break;
        case ColorChannel::Lightness: asked.L = t; break;
        case ColorChannel::Chroma: asked.C = t * kMaxChooserChroma; break;
        case ColorChannel::Hue: asked.H = t * 360; break;
        }
        if (channel == ColorChannel::Red || channel == ColorChannel::Green || channel == ColorChannel::Blue) {
            ramp.push_back({shown, false});
        } else {
            ramp.push_back({fromOklch(asked.L, asked.C, asked.H), !showsInSrgb(asked.L, asked.C, asked.H)});
        }
    }
    return ramp;
}

}  // namespace iv
