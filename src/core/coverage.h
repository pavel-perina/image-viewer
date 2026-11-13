#pragma once

#include <vector>

namespace iv {

// How much of each pixel something drawn covers, from 0 to 1: the alpha of a shape drawn
// alone. A dark edge around the shape and its shadow are made from this, the same way for
// every kind of shape, rather than by growing each kind's geometry.
struct Coverage {
    int width = 0;
    int height = 0;
    std::vector<float> values;  // row after row

    Coverage() = default;
    Coverage(int width, int height) : width(width), height(height), values(static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {}

    const float& at(int x, int y) const { return values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]; }
    float& at(int x, int y) { return values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]; }
};

// The shape grown by `radius` pixels all round, which need not be whole: a pixel is covered
// by how far its middle is from the shape's edge, and a partly covered pixel has its edge
// inside it. The grown edge goes from covered to not over `soft` pixels: 1 is the edge of
// something drawn smoothly, more is softer. What reaches beyond the plane is cut off.
Coverage spread(const Coverage& shape, double radius, double soft = 1);

// Moved by a distance, which need not be whole pixels. What leaves the plane is lost.
Coverage shifted(const Coverage& shape, double dx, double dy);

// Blurred with a Gaussian of the given sigma, in pixels. Beyond the plane counts as empty.
Coverage blurred(const Coverage& shape, double sigma);

}  // namespace iv
