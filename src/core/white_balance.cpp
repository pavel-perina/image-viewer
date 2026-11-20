#include "core/white_balance.h"

#include <algorithm>
#include <cmath>

namespace iv {

namespace {

using Uv = std::array<double, 2>;

// Tint 100 is this far from the locus in uv.
constexpr double kTintScale = 0.02 / kMaxTint;

// The smallest share of its largest channel a source white's channel is taken as having, so
// that a white at the edge of the space's gamut gives a large gain rather than an endless one.
constexpr double kLeastChannel = 0.02;

Uv uvOfXyz(const std::array<double, 3>& xyz) {
    const double d = xyz[0] + 15 * xyz[1] + 3 * xyz[2];
    return {4 * xyz[0] / d, 6 * xyz[1] / d};
}

// XYZ with Y = 1 of a uv point.
std::array<double, 3> xyzOfUv(const Uv& uv) {
    // From u = 4X / D and v = 6Y / D with Y = 1: D = 6 / v.
    const double d = 6 / uv[1];
    const double x = uv[0] * d / 4;
    return {x, 1, (d - x - 15) / 3};
}

// The unit normal of the locus at a temperature, pointing to green, above the line.
Uv greenNormal(double kelvin) {
    const double step = kelvin * 1e-4;
    const Uv a = planckianUv(kelvin - step);
    const Uv b = planckianUv(kelvin + step);
    const double du = b[0] - a[0];
    const double dv = b[1] - a[1];
    const double length = std::hypot(du, dv);
    Uv n{-dv / length, du / length};
    if (n[1] < 0) {
        n = {-n[0], -n[1]};
    }
    return n;
}

// Where the temperature and tint put a white, relative to 6500 K and tint 0. A positive tint
// makes the picture magenta, so it takes the light as green: above the line.
Uv offsetOf(const WhiteBalance& balance) {
    const Uv on = planckianUv(balance.kelvin);
    const Uv reference = planckianUv(6500);
    const Uv n = greenNormal(balance.kelvin);
    const double tint = balance.tint * kTintScale;
    return {on[0] - reference[0] + tint * n[0], on[1] - reference[1] + tint * n[1]};
}

Uv whiteUv(const Matrix3& rgbToXyz) { return uvOfXyz(rgbToXyz * std::array<double, 3>{1, 1, 1}); }

}  // namespace

Uv planckianUv(double kelvin) {
    const double t = kelvin;
    const double t2 = t * t;
    return {(0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t2) / (1 + 8.42420235e-4 * t + 7.08145163e-7 * t2),
            (0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t2) / (1 - 2.89741816e-5 * t + 1.61456053e-7 * t2)};
}

Matrix3 whiteBalanceMatrix(const WhiteBalance& balance, const Matrix3& rgbToXyz) {
    if (balance.isIdentity()) {
        return {};
    }
    const Uv white = whiteUv(rgbToXyz);
    const Uv offset = offsetOf(balance);
    std::array<double, 3> source = rgbToXyz.inverse() * xyzOfUv({white[0] + offset[0], white[1] + offset[1]});
    const double largest = std::max({source[0], source[1], source[2]});
    std::array<double, 3> gain{};
    for (std::size_t c = 0; c < 3; ++c) {
        gain[c] = 1 / std::max(source[c], largest * kLeastChannel);
    }
    const double luminance = rgbToXyz.m[3] * gain[0] + rgbToXyz.m[4] * gain[1] + rgbToXyz.m[5] * gain[2];
    return {{gain[0] / luminance, 0, 0, 0, gain[1] / luminance, 0, 0, 0, gain[2] / luminance}};
}

WhiteBalanceFit whiteBalanceFor(const std::array<double, 3>& linearRgb, const Matrix3& rgbToXyz) {
    const Uv white = whiteUv(rgbToXyz);
    const Uv picked = uvOfXyz(rgbToXyz * linearRgb);
    const Uv reference = planckianUv(6500);
    // The point the white must reach, in the locus's own terms.
    const Uv target{reference[0] + picked[0] - white[0], reference[1] + picked[1] - white[1]};
    // The temperature whose point on the locus is nearest, found in mireds, where the locus is
    // stepped about evenly: a coarse search, then halving around the best.
    const auto distance = [&](double mired) {
        const Uv p = planckianUv(1e6 / mired);
        return std::hypot(target[0] - p[0], target[1] - p[1]);
    };
    constexpr double kLowMired = 1e6 / kMaxKelvin;
    constexpr double kHighMired = 1e6 / kMinKelvin;
    constexpr int kSteps = 200;
    constexpr double kStep = (kHighMired - kLowMired) / kSteps;
    double best = kLowMired;
    for (int i = 0; i <= kSteps; ++i) {
        const double mired = kLowMired + i * kStep;
        if (distance(mired) < distance(best)) {
            best = mired;
        }
    }
    double low = std::max(kLowMired, best - kStep);
    double high = std::min(kHighMired, best + kStep);
    for (int i = 0; i < 60; ++i) {
        const double a = low + (high - low) / 3;
        const double b = high - (high - low) / 3;
        if (distance(a) < distance(b)) {
            high = b;
        } else {
            low = a;
        }
    }
    // The search keeps to the range, so a white beyond it lands at an end, which is then exact.
    constexpr double kEdge = 1e-3;
    const double mired = (low + high) / 2;
    const bool atEnd = mired - kLowMired < kEdge || kHighMired - mired < kEdge;
    const double kelvin = mired - kLowMired < kEdge ? kMaxKelvin : kHighMired - mired < kEdge ? kMinKelvin : 1e6 / mired;
    const Uv on = planckianUv(kelvin);
    const Uv n = greenNormal(kelvin);
    const double tint = ((target[0] - on[0]) * n[0] + (target[1] - on[1]) * n[1]) / kTintScale;

    WhiteBalanceFit fit;
    fit.balance.kelvin = kelvin;
    fit.balance.tint = std::clamp(tint, -kMaxTint, kMaxTint);
    fit.clamped = atEnd || std::abs(tint) > kMaxTint;
    return fit;
}

std::array<double, 3> fitByChroma(const std::array<double, 3>& rgb, const std::array<double, 3>& luminanceWeights) {
    const double gray =
        std::clamp(rgb[0] * luminanceWeights[0] + rgb[1] * luminanceWeights[1] + rgb[2] * luminanceWeights[2], 0.0, 1.0);
    double keep = 1;
    for (const double c : rgb) {
        if (c > 1) {
            keep = std::min(keep, (1 - gray) / (c - gray));
        } else if (c < 0) {
            keep = std::min(keep, gray / (gray - c));
        }
    }
    if (keep >= 1) {
        return rgb;
    }
    keep = std::max(keep, 0.0);
    return {gray + (rgb[0] - gray) * keep, gray + (rgb[1] - gray) * keep, gray + (rgb[2] - gray) * keep};
}

}  // namespace iv
