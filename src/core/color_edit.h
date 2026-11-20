#pragma once

#include <array>
#include <memory>
#include <stop_token>
#include <string_view>
#include <vector>

#include "core/color.h"
#include "core/image_buffer.h"
#include "core/image_source.h"
#include "core/white_balance.h"

namespace iv {

// A curve from 0 to 1 onto 0 to 1 and beyond, as evenly spaced samples joined by straight lines.
// No samples is the identity, so an untouched curve costs nothing and changes nothing.
struct Curve {
    std::vector<float> samples;

    bool isIdentity() const { return samples.empty(); }
    // Inputs outside 0 to 1 take the end values.
    float operator()(float x) const;
    bool operator==(const Curve&) const = default;
};

// Levels: black and white points from 0 to 1 of the range, and a gamma for the midtones, above 1
// lightening them, then the output range they map to, so a raised output black turns black gray.
// The identity gives an empty curve.
Curve levelsCurve(double black, double white, double gamma, double outBlack = 0, double outWhite = 1);

// For Oklab lightness: a gamma for the midtones as in levels, contrast scaling the distance from
// the middle of the scale, and brightness added to every value, so it moves blacks and highlights
// alike. The identity gives an empty curve.
Curve lightnessCurve(double brightness, double contrast, double gamma);

// A 3D lookup table from RGB as stored to RGB, of any size, with red changing fastest, as .cube
// files hold it. Points between those of the grid are mixed from the four corners of the
// tetrahedron around them, which keeps grays on the gray diagonal gray.
struct ColorCube {
    int size = 0;
    std::array<float, 3> domainMin{0, 0, 0};
    std::array<float, 3> domainMax{1, 1, 1};
    std::vector<float> rgb;  // size^3 entries of R, G, B

    std::array<float, 3> operator()(float r, float g, float b) const;
    bool operator==(const ColorCube&) const = default;
};

// One that changes nothing, for tests and as a start.
ColorCube identityCube(int size);

// A .cube file: TITLE, DOMAIN_MIN, DOMAIN_MAX, and LUT_3D_SIZE or LUT_1D_SIZE. A 1D table, a curve
// per channel, is turned into a 3D one of 33 points, so there is one kind to apply.
Result<ColorCube> parseCube(std::string_view text);

// Colour and tone changes of a picture, as one edit. The stages run in a fixed order, each off
// unless set: white balance in linear light of the picture's own RGB; a curve on the samples as
// stored, the same for R, G and B; then in Oklab, a curve on lightness and a gain on chroma;
// then a cube on the result as stored. The Oklab stages work on planes of a band of rows at a
// time, which is where changes that look at neighbouring pixels, such as local contrast, are
// meant to go.
// Gray pictures take the curves only: chroma and the cube would make colour out of them.
struct ColorEdit {
    WhiteBalance whiteBalance;
    // The picture's RGB to XYZ, which white balance works in: from its profile, else sRGB. A
    // value, not a reference to the picture, so the edit can be repeated on another.
    Matrix3 workingSpace = rgbToXyz(kSrgbPrimaries);
    Curve tone;
    Curve lightness;
    double chroma = 1;
    // Colours the Oklab stages take beyond the range of an integer format lose chroma at the same
    // lightness and hue until they fit, rather than being clipped channel by channel, which turns
    // their hue and flattens them. Floats keep what lies beyond the range either way.
    bool gamutMap = true;
    std::shared_ptr<const ColorCube> cube;

    bool isIdentity() const { return whiteBalance.isIdentity() && tone.isIdentity() && lightness.isIdentity() && chroma == 1 && !cube; }
    bool usesOklab() const { return !lightness.isIdentity() || chroma != 1; }
    bool operator==(const ColorEdit& other) const;
};

// The picture with the edit applied, in the same format, with alpha as it was. Colours that leave the range of an
// integer format are fitted as gamutMap says, then clipped in linear light. A stop request gives up between rows and returns an empty buffer.
ImageBuffer applyColor(const ImageBuffer& source, const ColorEdit& edit, const std::stop_token& stop = {});

}  // namespace iv
