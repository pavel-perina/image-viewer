#pragma once

#include <cmath>

namespace iv {

// The sRGB curve both ways, for samples from 0 to 1, mirrored below 0 for floats.
inline double srgbDecode(double c) {
    const double a = std::abs(c);
    return std::copysign(a <= 0.04045 ? a / 12.92 : std::pow((a + 0.055) / 1.055, 2.4), c);
}

inline double srgbEncode(double c) {
    const double a = std::abs(c);
    return std::copysign(a <= 0.0031308 ? a * 12.92 : 1.055 * std::pow(a, 1 / 2.4) - 0.055, c);
}

// Oklab, from linear sRGB: L lightness from 0 to 1, a green to red, b blue to yellow. Gray has
// a and b at zero, and both matrices keep white, so equal R, G and B give L as their cube root.
struct Oklab {
    double L = 0;
    double a = 0;
    double b = 0;
};

inline Oklab linearToOklab(double r, double g, double b) {
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

struct LinearRgb {
    double r = 0;
    double g = 0;
    double b = 0;
};

inline LinearRgb oklabToLinear(const Oklab& lab) {
    const double l = lab.L + 0.3963377774 * lab.a + 0.2158037573 * lab.b;
    const double m = lab.L - 0.1055613458 * lab.a - 0.0638541728 * lab.b;
    const double s = lab.L - 0.0894841775 * lab.a - 1.2914855480 * lab.b;
    const double l3 = l * l * l;
    const double m3 = m * m * m;
    const double s3 = s * s * s;
    return {4.0767416621 * l3 - 3.3077115913 * m3 + 0.2309699292 * s3,
            -1.2684380046 * l3 + 2.6097574011 * m3 - 0.3413193965 * s3,
            -0.0041960863 * l3 - 0.7034186147 * m3 + 1.7076147010 * s3};
}

}  // namespace iv
