#pragma once

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
