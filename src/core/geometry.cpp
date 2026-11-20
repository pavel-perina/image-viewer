#include "core/geometry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace iv {

double distanceToSegment(PointD p, PointD a, PointD b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lengthSquared = dx * dx + dy * dy;
    const double t = lengthSquared > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared, 0.0, 1.0) : 0.0;
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

void Affine::apply(double x, double y, double& outX, double& outY) const {
    outX = a * x + c * y + tx;
    outY = b * x + d * y + ty;
}

Affine Affine::inverse() const {
    const double det = a * d - b * c;
    Affine r;
    r.a = d / det;
    r.b = -b / det;
    r.c = -c / det;
    r.d = a / det;
    r.tx = -(r.a * tx + r.c * ty);
    r.ty = -(r.b * tx + r.d * ty);
    return r;
}

RectD Affine::mapRect(const RectD& rect) const {
    double x0, y0, x1, y1;
    apply(rect.x, rect.y, x0, y0);
    apply(rect.x + rect.width, rect.y + rect.height, x1, y1);
    // Only axis-aligned transforms are used, so two opposite corners suffice.
    return {std::min(x0, x1), std::min(y0, y1), std::max(x0, x1) - std::min(x0, x1),
            std::max(y0, y1) - std::min(y0, y1)};
}

std::array<double, 9> inverse3(const std::array<double, 9>& a) {
    std::array<double, 9> r{a[4] * a[8] - a[5] * a[7], a[2] * a[7] - a[1] * a[8], a[1] * a[5] - a[2] * a[4],
                            a[5] * a[6] - a[3] * a[8], a[0] * a[8] - a[2] * a[6], a[2] * a[3] - a[0] * a[5],
                            a[3] * a[7] - a[4] * a[6], a[1] * a[6] - a[0] * a[7], a[0] * a[4] - a[1] * a[3]};
    const double det = a[0] * r[0] + a[1] * r[3] + a[2] * r[6];
    for (double& v : r) {
        v /= det;
    }
    return r;
}

std::array<double, 9> multiply3(const std::array<double, 9>& a, const std::array<double, 9>& b) {
    std::array<double, 9> r{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double sum = 0;
            for (std::size_t k = 0; k < 3; ++k) {
                sum += a[i * 3 + k] * b[k * 3 + j];
            }
            r[i * 3 + j] = sum;
        }
    }
    return r;
}

Matrix3 Matrix3::inverse() const { return {inverse3(m)}; }

Matrix3 Matrix3::operator*(const Matrix3& other) const { return {multiply3(m, other.m)}; }

std::array<double, 3> Matrix3::operator*(const std::array<double, 3>& v) const {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

Size displaySize(Size stored, Orientation orientation) {
    switch (orientation) {
    case Orientation::Transpose:
    case Orientation::Rotate90CW:
    case Orientation::Transverse:
    case Orientation::Rotate270CW:
        return {stored.height, stored.width};
    default:
        return stored;
    }
}

Affine displayFromStored(Size stored, Orientation orientation) {
    const double w = stored.width;
    const double h = stored.height;
    switch (orientation) {
    case Orientation::Normal:           return {1, 0, 0, 1, 0, 0};
    case Orientation::MirrorHorizontal: return {-1, 0, 0, 1, w, 0};
    case Orientation::Rotate180:        return {-1, 0, 0, -1, w, h};
    case Orientation::MirrorVertical:   return {1, 0, 0, -1, 0, h};
    case Orientation::Transpose:        return {0, 1, 1, 0, 0, 0};
    case Orientation::Rotate90CW:       return {0, 1, -1, 0, h, 0};
    case Orientation::Transverse:       return {0, -1, -1, 0, h, w};
    case Orientation::Rotate270CW:      return {0, -1, 1, 0, 0, w};
    }
    return {};
}

}  // namespace iv
