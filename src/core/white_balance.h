#pragma once

#include <array>

#include "core/geometry.h"

namespace iv {

// The white a picture is taken as having, relative to how it is rendered, to be corrected to
// neutral. The temperature is the colour of the light in kelvins: at 6500 the picture is taken
// as neutral already, lower reads it as lit by warmer light and so makes it bluer, higher makes
// it warmer. Tint is how far the light lay off the line of temperatures: positive makes the
// picture more magenta, correcting green light, negative greener. 6500 K and tint 0 change
// nothing.
struct WhiteBalance {
    double kelvin = 6500;
    double tint = 0;

    bool isIdentity() const { return kelvin == 6500 && tint == 0; }
    bool operator==(const WhiteBalance&) const = default;
};

inline constexpr double kMinKelvin = 2000;
inline constexpr double kMaxKelvin = 15000;
inline constexpr double kMaxTint = 100;

// A point on the Planckian locus in CIE 1960 uv, by Krystek's approximation, which holds from
// 1000 K to 15000 K.
std::array<double, 2> planckianUv(double kelvin);

// The correction as a matrix on linear RGB of a space whose RGB to XYZ matrix, referred to D65,
// is given. The source white is the space's white moved in uv by as much as the temperature and
// tint move a white from 6500 K and tint 0, so that those give exactly the identity. Each of R,
// G and B is scaled so that the source white becomes neutral, as raw converters scale a
// camera's channels; the gains are then normalised so that a neutral keeps its luminance, so
// white balance changes colour and not brightness. Scaling channels keeps every colour that has
// no negative channel without one, at any temperature, which adaptation across cone responses
// does not: far from 6500 K it turns ordinary colours negative.
Matrix3 whiteBalanceMatrix(const WhiteBalance& balance, const Matrix3& rgbToXyz);

// What makes a colour, in linear RGB of that space, neutral: the inverse of the above, exact
// unless the colour lies beyond the range.
// Values beyond the range are brought to its nearest end, and clamped says so.
struct WhiteBalanceFit {
    WhiteBalance balance;
    bool clamped = false;
};
WhiteBalanceFit whiteBalanceFor(const std::array<double, 3>& linearRgb, const Matrix3& rgbToXyz);

// A linear colour beyond 0 to 1 brought inside by moving it towards the gray of its luminance,
// the weights of which are the Y row of the space's RGB to XYZ matrix, so it keeps luminance
// and loses only saturation. The gray is clamped to the range first.
std::array<double, 3> fitByChroma(const std::array<double, 3>& rgb, const std::array<double, 3>& luminanceWeights);

}  // namespace iv
