#pragma once

// The colours of the interface, worked out from a few choices rather than picked one by one.
// Every role has a fixed lightness in Oklab, so that contrast stays the same whatever hue is
// chosen; the choices only give the hue and how much colour surfaces and accents carry.
// What must not follow a theme (the gray around a picture, warnings, annotation colours) is
// not here.

#include <string_view>
#include <vector>

#include "core/rgba.h"

namespace iv {

struct PaletteChoices {
    double hue = 150;              // degrees in Oklab: 150 a willow green, 230 a blue, 40 a rust
    double accentChroma = 0.075;   // how colourful accents are: selection, marks, the default button
    double surfaceChroma = 0.014;  // how much the hue tints backgrounds and text; 0 is neutral gray
};

struct PaletteRole {
    std::string_view name;  // as Theme names it: "window", "accent", ...
    Rgba8 colour;
};

// Every role, in a fixed order.
std::vector<PaletteRole> derivePalette(const PaletteChoices& choices);

// The colour of one role, or none if there is no such role.
const PaletteRole* findRole(const std::vector<PaletteRole>& palette, std::string_view name);

// WCAG contrast between two opaque colours, from 1 to 21.
double contrastRatio(Rgba8 a, Rgba8 b);

// An Oklab colour by lightness, chroma and hue in degrees, in sRGB. Chroma the sRGB gamut cannot
// show at that lightness and hue is reduced until it can, so lightness and hue stay.
Rgba8 fromOklch(double lightness, double chroma, double hue, double alpha = 1);

}  // namespace iv
