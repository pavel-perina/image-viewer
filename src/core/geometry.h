#pragma once

#include <array>

namespace iv {

struct Size {
    int width = 0;
    int height = 0;
    bool operator==(const Size&) const = default;
};

struct PointD {
    double x = 0;
    double y = 0;
    bool operator==(const PointD&) const = default;
};

// How far a point is from the nearest point of the segment from a to b.
double distanceToSegment(PointD p, PointD a, PointD b);

struct RectD {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
    bool operator==(const RectD&) const = default;
};

// A 3 x 3 matrix, row by row, for linear maps such as those between colour spaces. Homography
// keeps its own type for points, with the same arithmetic.
struct Matrix3 {
    std::array<double, 9> m{1, 0, 0, 0, 1, 0, 0, 0, 1};

    Matrix3 inverse() const;
    Matrix3 operator*(const Matrix3& other) const;
    std::array<double, 3> operator*(const std::array<double, 3>& v) const;
    bool operator==(const Matrix3&) const = default;
};

// The same for plain arrays, row by row; a singular matrix gives infinities.
std::array<double, 9> inverse3(const std::array<double, 9>& a);
std::array<double, 9> multiply3(const std::array<double, 9>& a, const std::array<double, 9>& b);

// x' = a*x + c*y + tx, y' = b*x + d*y + ty
struct Affine {
    double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;

    void apply(double x, double y, double& outX, double& outY) const;
    Affine inverse() const;
    // Bounding box of the transformed rectangle.
    RectD mapRect(const RectD& rect) const;
};

// Values match the EXIF orientation tag.
enum class Orientation {
    Normal = 1,
    MirrorHorizontal = 2,
    Rotate180 = 3,
    MirrorVertical = 4,
    Transpose = 5,
    Rotate90CW = 6,
    Transverse = 7,
    Rotate270CW = 8,
};

// Size of the image as shown, given the size in which it is stored.
Size displaySize(Size stored, Orientation orientation);

// Maps stored pixel coordinates to display coordinates.
Affine displayFromStored(Size stored, Orientation orientation);

}  // namespace iv
