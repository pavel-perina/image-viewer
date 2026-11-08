#include "core/perspective.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <utility>

namespace iv {

namespace {

constexpr double kDegree = std::numbers::pi / 180;
constexpr double k35mmDiagonal = 43.2666;  // millimetres, of 36 x 24
constexpr double kMaxAngle = 60;           // degrees, beyond which a picture would be torn apart

Homography matrix(double a, double b, double c, double d, double e, double f, double g, double h, double i) {
    return Homography{{a, b, c, d, e, f, g, h, i}};
}

// Turns the camera by yaw, then pitch, then roll.
std::array<double, 9> rotation(const Tilt& tilt) {
    const double r = tilt.roll * kDegree;
    const double p = tilt.pitch * kDegree;
    const double y = tilt.yaw * kDegree;
    const Homography rz = matrix(std::cos(r), -std::sin(r), 0, std::sin(r), std::cos(r), 0, 0, 0, 1);
    const Homography rx = matrix(1, 0, 0, 0, std::cos(p), -std::sin(p), 0, std::sin(p), std::cos(p));
    const Homography ry = matrix(std::cos(y), 0, std::sin(y), 0, 1, 0, -std::sin(y), 0, std::cos(y));
    return (rz * rx * ry).m;
}

PointD operator-(PointD a, PointD b) { return {a.x - b.x, a.y - b.y}; }

// A x = b for three unknowns; false when the system is singular.
bool solve3(std::array<double, 9> a, std::array<double, 3> b, std::array<double, 3>& x) {
    for (int col = 0; col < 3; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 3; ++row) {
            if (std::abs(a[row * 3 + col]) > std::abs(a[pivot * 3 + col])) {
                pivot = row;
            }
        }
        if (std::abs(a[pivot * 3 + col]) < 1e-12) {
            return false;
        }
        for (int k = 0; k < 3; ++k) {
            std::swap(a[col * 3 + k], a[pivot * 3 + k]);
        }
        std::swap(b[col], b[pivot]);
        for (int row = 0; row < 3; ++row) {
            if (row == col) {
                continue;
            }
            const double factor = a[row * 3 + col] / a[col * 3 + col];
            for (int k = 0; k < 3; ++k) {
                a[row * 3 + k] -= factor * a[col * 3 + k];
            }
            b[row] -= factor * b[col];
        }
    }
    for (int i = 0; i < 3; ++i) {
        x[i] = b[i] / a[i * 3 + i];
    }
    return true;
}

// Each edge of a convex quadrilateral as o . p <= limit, with o pointing out.
struct HalfPlane {
    double ox, oy, limit;
};

std::array<HalfPlane, 4> halfPlanes(const std::array<PointD, 4>& quad) {
    PointD centre{};
    for (const PointD& p : quad) {
        centre.x += p.x / 4;
        centre.y += p.y / 4;
    }
    std::array<HalfPlane, 4> planes{};
    for (std::size_t i = 0; i < 4; ++i) {
        const PointD p = quad[i];
        const PointD d = quad[(i + 1) % 4] - p;
        double ox = d.y;
        double oy = -d.x;
        if (ox * (centre.x - p.x) + oy * (centre.y - p.y) > 0) {
            ox = -ox;
            oy = -oy;
        }
        planes[i] = {ox, oy, ox * p.x + oy * p.y};
    }
    return planes;
}

// The largest half height h of a rectangle of the ratio, over its centre: maximize h with
// o . c + (|ox| ratio + |oy|) h <= limit for each edge, a linear program of three unknowns
// whose best lies where three of the four edges hold exactly.
RectD largestOfRatio(const std::array<HalfPlane, 4>& planes, double ratio) {
    RectD best{};
    double bestHalf = -1;
    for (int skip = 0; skip < 4; ++skip) {
        std::array<double, 9> a{};
        std::array<double, 3> b{};
        int row = 0;
        for (int i = 0; i < 4; ++i) {
            if (i == skip) {
                continue;
            }
            const HalfPlane& e = planes[static_cast<std::size_t>(i)];
            a[row * 3] = e.ox;
            a[row * 3 + 1] = e.oy;
            a[row * 3 + 2] = std::abs(e.ox) * ratio + std::abs(e.oy);
            b[static_cast<std::size_t>(row)] = e.limit;
            ++row;
        }
        std::array<double, 3> x{};
        if (!solve3(a, b, x) || x[2] <= bestHalf) {
            continue;
        }
        const HalfPlane& other = planes[static_cast<std::size_t>(skip)];
        const double used = other.ox * x[0] + other.oy * x[1] + (std::abs(other.ox) * ratio + std::abs(other.oy)) * x[2];
        if (used > other.limit + 1e-6 * (std::abs(other.limit) + 1)) {
            continue;
        }
        bestHalf = x[2];
        best = {x[0] - ratio * x[2], x[1] - x[2], 2 * ratio * x[2], 2 * x[2]};
    }
    return best;
}

double area(const RectD& rect) { return rect.width * rect.height; }

}  // namespace

PointD Homography::apply(PointD p) const {
    const double w = m[6] * p.x + m[7] * p.y + m[8];
    return {(m[0] * p.x + m[1] * p.y + m[2]) / w, (m[3] * p.x + m[4] * p.y + m[5]) / w};
}

Homography Homography::inverse() const {
    const auto& a = m;
    Homography r;
    r.m = {a[4] * a[8] - a[5] * a[7], a[2] * a[7] - a[1] * a[8], a[1] * a[5] - a[2] * a[4],
           a[5] * a[6] - a[3] * a[8], a[0] * a[8] - a[2] * a[6], a[2] * a[3] - a[0] * a[5],
           a[3] * a[7] - a[4] * a[6], a[1] * a[6] - a[0] * a[7], a[0] * a[4] - a[1] * a[3]};
    const double det = a[0] * r.m[0] + a[1] * r.m[3] + a[2] * r.m[6];
    for (double& v : r.m) {
        v /= det;
    }
    return r;
}

Homography Homography::operator*(const Homography& other) const {
    Homography r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double sum = 0;
            for (int k = 0; k < 3; ++k) {
                sum += m[static_cast<std::size_t>(i * 3 + k)] * other.m[static_cast<std::size_t>(k * 3 + j)];
            }
            r.m[static_cast<std::size_t>(i * 3 + j)] = sum;
        }
    }
    return r;
}

double focalPixels(Size image, double focal35mm) {
    return focal35mm / k35mmDiagonal * std::hypot(image.width, image.height);
}

Homography tiltTransform(Size image, double focalPixels, Tilt tilt) {
    const double f = focalPixels;
    const auto r = rotation(tilt);
    // The ray through each point, turned, and where it meets the picture again.
    const Homography centred = matrix(1, 0, -image.width / 2.0, 0, 1, -image.height / 2.0, 0, 0, 1);
    const Homography turned = matrix(r[0], r[1], r[2] * f, r[3], r[4], r[5] * f, r[6] / f, r[7] / f, r[8]) * centred;
    // Back to the centre, at the scale the centre had.
    const PointD middle{image.width / 2.0, image.height / 2.0};
    const PointD c = turned.apply(middle);
    const PointD dx = turned.apply({middle.x + 1, middle.y}) - turned.apply({middle.x - 1, middle.y});
    const PointD dy = turned.apply({middle.x, middle.y + 1}) - turned.apply({middle.x, middle.y - 1});
    const double det = std::abs(dx.x * dy.y - dx.y * dy.x) / 4;
    const double s = det > 0 ? 1 / std::sqrt(det) : 1;
    return matrix(s, 0, -s * c.x, 0, s, -s * c.y, 0, 0, 1) * turned;
}

RectD largestRectangle(const std::array<PointD, 4>& quad, double ratio) {
    const auto planes = halfPlanes(quad);
    if (ratio > 0) {
        return largestOfRatio(planes, ratio);
    }
    // Over ratios from 1:10 to 10:1, coarsely and then around the best.
    double bestLog = 0;
    RectD best = largestOfRatio(planes, 1);
    for (int i = -100; i <= 100; ++i) {
        const double logRatio = i * std::log(10.0) / 100;
        const RectD r = largestOfRatio(planes, std::exp(logRatio));
        if (area(r) > area(best)) {
            best = r;
            bestLog = logRatio;
        }
    }
    double low = bestLog - std::log(10.0) / 100;
    double high = bestLog + std::log(10.0) / 100;
    for (int i = 0; i < 40; ++i) {
        const double m1 = low + (high - low) / 3;
        const double m2 = high - (high - low) / 3;
        if (area(largestOfRatio(planes, std::exp(m1))) < area(largestOfRatio(planes, std::exp(m2)))) {
            low = m1;
        } else {
            high = m2;
        }
    }
    const RectD refined = largestOfRatio(planes, std::exp((low + high) / 2));
    return area(refined) > area(best) ? refined : best;
}

TiltPlan planTilt(Size image, double focalPixels, Tilt tilt, bool keepRatio) {
    TiltPlan plan;
    plan.transform = tiltTransform(image, focalPixels, tilt);
    const double w = image.width;
    const double h = image.height;
    if (tilt.isZero()) {
        plan.kept = {-w / 2, -h / 2, w, h};
        plan.size = image;
        return plan;
    }
    const std::array<PointD, 4> quad{plan.transform.apply({0, 0}), plan.transform.apply({w, 0}),
                                     plan.transform.apply({w, h}), plan.transform.apply({0, h})};
    RectD kept = largestRectangle(quad, keepRatio ? w / h : 0);
    // Whole pixels, keeping the centre.
    plan.size = {std::max(1, static_cast<int>(std::floor(kept.width + 1e-6))),
                 std::max(1, static_cast<int>(std::floor(kept.height + 1e-6)))};
    kept.x += (kept.width - plan.size.width) / 2;
    kept.y += (kept.height - plan.size.height) / 2;
    kept.width = plan.size.width;
    kept.height = plan.size.height;
    plan.kept = kept;
    return plan;
}

bool GuideLine::isVertical() const { return std::abs(b.y - a.y) > std::abs(b.x - a.x); }

double GuideLine::length() const { return std::hypot(b.x - a.x, b.y - a.y); }

namespace {

// How far a line leans from what it is to become, as the sine of the angle, after the turn.
double lean(const GuideLine& line, const Homography& transform) {
    const PointD d = transform.apply(line.b) - transform.apply(line.a);
    const double length = std::hypot(d.x, d.y);
    if (length == 0) {
        return 0;
    }
    return line.isVertical() ? d.x / length : d.y / length;
}

// Least squares over the free angles, by Levenberg and Marquardt with numeric derivatives.
Tilt fitOnce(std::span<const GuideLine> lines, const std::vector<bool>& used, Size image, double focalPixels,
             const std::vector<double Tilt::*>& free, Tilt start) {
    const auto residuals = [&](const Tilt& tilt) {
        const Homography transform = tiltTransform(image, focalPixels, tilt);
        std::vector<double> r;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (used[i]) {
                r.push_back(std::sqrt(lines[i].length()) * lean(lines[i], transform));
            }
        }
        return r;
    };
    const auto cost = [](const std::vector<double>& r) {
        double sum = 0;
        for (const double v : r) {
            sum += v * v;
        }
        return sum;
    };
    const std::size_t n = free.size();
    Tilt tilt = start;
    std::vector<double> r = residuals(tilt);
    double current = cost(r);
    double lambda = 1e-3;
    for (int iteration = 0; iteration < 100; ++iteration) {
        // The Jacobian, column by column.
        constexpr double kStep = 1e-4;
        std::vector<std::vector<double>> jacobian(n);
        for (std::size_t k = 0; k < n; ++k) {
            Tilt plus = tilt;
            Tilt minus = tilt;
            plus.*free[k] += kStep;
            minus.*free[k] -= kStep;
            const auto rp = residuals(plus);
            const auto rm = residuals(minus);
            jacobian[k].resize(r.size());
            for (std::size_t i = 0; i < r.size(); ++i) {
                jacobian[k][i] = (rp[i] - rm[i]) / (2 * kStep);
            }
        }
        std::array<double, 9> normal{};
        std::array<double, 3> gradient{};
        for (std::size_t a = 0; a < n; ++a) {
            for (std::size_t b = 0; b < n; ++b) {
                double sum = 0;
                for (std::size_t i = 0; i < r.size(); ++i) {
                    sum += jacobian[a][i] * jacobian[b][i];
                }
                normal[a * 3 + b] = sum;
            }
            double sum = 0;
            for (std::size_t i = 0; i < r.size(); ++i) {
                sum += jacobian[a][i] * r[i];
            }
            gradient[a] = -sum;
        }
        bool improved = false;
        double size = 0;
        for (int attempt = 0; attempt < 10 && !improved; ++attempt) {
            std::array<double, 9> damped{};
            for (std::size_t a = 0; a < 3; ++a) {
                damped[a * 3 + a] = 1;  // the unused unknowns stay at zero
            }
            for (std::size_t a = 0; a < n; ++a) {
                for (std::size_t b = 0; b < n; ++b) {
                    damped[a * 3 + b] = normal[a * 3 + b];
                }
                damped[a * 3 + a] += lambda * normal[a * 3 + a] + 1e-12;
            }
            std::array<double, 3> b{};
            std::copy_n(gradient.begin(), n, b.begin());
            std::array<double, 3> step{};
            if (!solve3(damped, b, step)) {
                lambda *= 10;
                continue;
            }
            Tilt next = tilt;
            size = 0;
            for (std::size_t k = 0; k < n; ++k) {
                next.*free[k] = std::clamp(next.*free[k] + step[k], -kMaxAngle, kMaxAngle);
                size = std::max(size, std::abs(step[k]));
            }
            const auto nextResiduals = residuals(next);
            const double nextCost = cost(nextResiduals);
            if (nextCost <= current) {
                tilt = next;
                r = nextResiduals;
                current = nextCost;
                lambda = std::max(lambda / 3, 1e-9);
                improved = true;
            } else {
                lambda *= 4;
            }
        }
        if (!improved || size < 1e-7) {
            break;
        }
    }
    return tilt;
}

}  // namespace

TiltFit fitTilt(std::span<const GuideLine> lines, Size image, double focalPixels, TiltMode mode, bool robust, Tilt start) {
    TiltFit fit{start, std::vector<bool>(lines.size(), true)};
    if (lines.empty()) {
        return fit;
    }
    for (int round = 0; round < (robust ? 5 : 1); ++round) {
        int verticals = 0;
        int horizontals = 0;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (fit.used[i]) {
                (lines[i].isVertical() ? verticals : horizontals)++;
            }
        }
        std::vector<double Tilt::*> free;
        free.reserve(3);
        free.push_back(&Tilt::roll);
        Tilt from = start;
        if (mode != TiltMode::Rotation && verticals >= 2) {
            free.push_back(&Tilt::pitch);
        }
        if (mode == TiltMode::Full && horizontals >= 2) {
            free.push_back(&Tilt::yaw);
        }
        fit.tilt = fitOnce(lines, fit.used, image, focalPixels, free, from);
        if (!robust) {
            break;
        }
        // Lines that lean much more than the typical one after the fit are taken as not meant,
        // unless the user drew them. Two of a kind stay if two were used, as leaving out one of
        // two horizontals frees yaw no more, so that the other then leans as well and goes too.
        const Homography transform = tiltTransform(image, focalPixels, fit.tilt);
        std::vector<double> angles(lines.size());
        std::vector<double> usedAngles;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            angles[i] = std::asin(std::min(1.0, std::abs(lean(lines[i], transform)))) / kDegree;
            if (fit.used[i]) {
                usedAngles.push_back(angles[i]);
            }
        }
        std::ranges::nth_element(usedAngles, usedAngles.begin() + static_cast<std::ptrdiff_t>(usedAngles.size() / 2));
        const double limit = std::max(0.3, 3 * usedAngles[usedAngles.size() / 2]);
        std::vector<bool> used(lines.size());
        for (std::size_t i = 0; i < lines.size(); ++i) {
            used[i] = lines[i].meant || angles[i] <= limit;
        }
        for (const bool vertical : {true, false}) {
            std::vector<std::size_t> kind;
            int kept = 0;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (fit.used[i] && lines[i].isVertical() == vertical) {
                    kind.push_back(i);
                    kept += used[i] ? 1 : 0;
                }
            }
            if (kind.size() >= 2 && kept < 2) {
                std::ranges::sort(kind, {}, [&](std::size_t i) { return angles[i]; });
                used[kind[0]] = true;
                used[kind[1]] = true;
            }
        }
        if (std::ranges::none_of(used, [](bool u) { return u; }) || used == fit.used) {
            break;
        }
        fit.used = std::move(used);
    }
    return fit;
}

std::vector<GuideLine> detectLines(const ImageBuffer& bgrx, int maxLines) {
    const int w = bgrx.width();
    const int h = bgrx.height();
    std::vector<GuideLine> found;
    if (bgrx.format() != PixelFormat::Bgrx8 || w < 8 || h < 8) {
        return found;
    }
    const auto at = [w](int x, int y) { return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x); };
    const auto column = [w](std::uint32_t i) { return static_cast<double>(static_cast<int>(i % static_cast<std::uint32_t>(w))); };
    const auto line = [w](std::uint32_t i) { return static_cast<double>(static_cast<int>(i / static_cast<std::uint32_t>(w))); };

    // Gray, smoothed with 1 2 1 each way to calm noise and JPEG blocks.
    std::vector<float> gray(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(bgrx.row(y));
        for (int x = 0; x < w; ++x, p += 4) {
            gray[at(x, y)] = 0.114F * p[0] + 0.587F * p[1] + 0.299F * p[2];
        }
    }
    std::vector<float> across(gray.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int l = std::max(x - 1, 0);
            const int r = std::min(x + 1, w - 1);
            across[at(x, y)] = (gray[at(l, y)] + 2 * gray[at(x, y)] + gray[at(r, y)]) / 4;
        }
    }
    for (int y = 0; y < h; ++y) {
        const int u = std::max(y - 1, 0);
        const int d = std::min(y + 1, h - 1);
        for (int x = 0; x < w; ++x) {
            gray[at(x, y)] = (across[at(x, u)] + 2 * across[at(x, y)] + across[at(x, d)]) / 4;
        }
    }

    // The gradient, and the pixels strong enough to be on an edge, strongest first.
    constexpr float kThreshold = 5;
    std::vector<float> magnitude(gray.size(), 0);
    std::vector<float> angle(gray.size(), 0);
    std::vector<std::pair<float, std::uint32_t>> strong;
    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            const float gx = (gray[at(x + 1, y)] - gray[at(x - 1, y)]) / 2;
            const float gy = (gray[at(x, y + 1)] - gray[at(x, y - 1)]) / 2;
            const float m = std::hypot(gx, gy);
            magnitude[at(x, y)] = m;
            angle[at(x, y)] = std::atan2(gy, gx);
            if (m > kThreshold) {
                strong.emplace_back(m, static_cast<std::uint32_t>(at(x, y)));
            }
        }
    }
    std::ranges::sort(strong, std::greater{});

    // Regions of neighbours whose gradient points the same way, each a candidate line.
    constexpr float kTolerance = static_cast<float>(22.5 * kDegree);
    const double minLength = std::max(16.0, 0.05 * std::max(w, h));
    std::vector<std::uint8_t> taken(gray.size(), 0);
    std::vector<std::uint32_t> region;
    for (const auto& [strength, seed] : strong) {
        if (taken[seed]) {
            continue;
        }
        region.clear();
        region.push_back(seed);
        taken[seed] = 1;
        double sumCos = std::cos(angle[seed]);
        double sumSin = std::sin(angle[seed]);
        float regionAngle = angle[seed];
        for (std::size_t next = 0; next < region.size(); ++next) {
            const int px = static_cast<int>(region[next] % static_cast<std::uint32_t>(w));
            const int py = static_cast<int>(region[next] / static_cast<std::uint32_t>(w));
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = px + dx;
                    const int ny = py + dy;
                    if (nx < 1 || ny < 1 || nx >= w - 1 || ny >= h - 1) {
                        continue;
                    }
                    const std::size_t i = at(nx, ny);
                    if (taken[i] || magnitude[i] <= kThreshold) {
                        continue;
                    }
                    float difference = std::abs(angle[i] - regionAngle);
                    if (difference > std::numbers::pi_v<float>) {
                        difference = 2 * std::numbers::pi_v<float> - difference;
                    }
                    if (difference > kTolerance) {
                        continue;
                    }
                    taken[i] = 1;
                    region.push_back(static_cast<std::uint32_t>(i));
                    sumCos += std::cos(angle[i]);
                    sumSin += std::sin(angle[i]);
                    regionAngle = static_cast<float>(std::atan2(sumSin, sumCos));
                }
            }
        }
        if (static_cast<double>(region.size()) < minLength) {
            continue;
        }
        // The line through the region and how long and thin it is.
        double total = 0;
        double cx = 0;
        double cy = 0;
        for (const std::uint32_t i : region) {
            const double m = magnitude[i];
            total += m;
            cx += m * column(i);
            cy += m * line(i);
        }
        cx /= total;
        cy /= total;
        double sxx = 0;
        double syy = 0;
        double sxy = 0;
        for (const std::uint32_t i : region) {
            const double m = magnitude[i];
            const double dx = column(i) - cx;
            const double dy = line(i) - cy;
            sxx += m * dx * dx;
            syy += m * dy * dy;
            sxy += m * dx * dy;
        }
        const double theta = 0.5 * std::atan2(2 * sxy, sxx - syy);
        const double ux = std::cos(theta);
        const double uy = std::sin(theta);
        double tMin = std::numeric_limits<double>::max();
        double tMax = std::numeric_limits<double>::lowest();
        double sMin = tMin;
        double sMax = tMax;
        for (const std::uint32_t i : region) {
            const double dx = column(i) - cx;
            const double dy = line(i) - cy;
            const double t = dx * ux + dy * uy;
            const double s = -dx * uy + dy * ux;
            tMin = std::min(tMin, t);
            tMax = std::max(tMax, t);
            sMin = std::min(sMin, s);
            sMax = std::max(sMax, s);
        }
        const double length = tMax - tMin;
        const double width = sMax - sMin;
        if (length < minLength || width > 3.5 + 0.02 * length) {
            continue;
        }
        // Pixel centres lie half a pixel in.
        found.push_back({{cx + ux * tMin + 0.5, cy + uy * tMin + 0.5}, {cx + ux * tMax + 0.5, cy + uy * tMax + 0.5}});
    }
    std::ranges::sort(found, [](const GuideLine& a, const GuideLine& b) { return a.length() > b.length(); });
    if (found.size() > static_cast<std::size_t>(maxLines)) {
        found.resize(static_cast<std::size_t>(maxLines));
    }
    return found;
}

}  // namespace iv
