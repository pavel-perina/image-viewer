#include "core/geometry.h"

#include <algorithm>

namespace iv {

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
