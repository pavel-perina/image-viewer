#pragma once

#include <array>
#include <span>
#include <vector>

#include "core/crop.h"
#include "core/geometry.h"
#include "core/image_buffer.h"

namespace iv {

// How the camera would have to have been turned for the picture to come out straight, in
// degrees: roll around the lens axis, positive turning the picture clockwise; pitch tilting it
// up or down, which makes verticals parallel; yaw turning it left or right, which does the same
// for horizontals.
struct Tilt {
    double roll = 0;
    double pitch = 0;
    double yaw = 0;
    bool operator==(const Tilt&) const = default;
    bool isZero() const { return roll == 0 && pitch == 0 && yaw == 0; }
};

// A 3 x 3 projective transform of points, row by row.
struct Homography {
    std::array<double, 9> m{1, 0, 0, 0, 1, 0, 0, 0, 1};

    PointD apply(PointD p) const;
    Homography inverse() const;
    Homography operator*(const Homography& other) const;
};

// The focal length in pixels of a picture of that size, from its equivalent on 35mm film, whose
// diagonal is 43.27 mm.
double focalPixels(Size image, double focal35mm);

// From pixel coordinates of a picture to those of the picture the turned camera would have
// taken, centred on 0, 0, at the same scale as the original at its centre.
Homography tiltTransform(Size image, double focalPixels, Tilt tilt);

// What straightening a picture makes: its transform, and the part kept. With crop, that is the
// largest rectangle with no empty corners, of the picture's own ratio or of any; without, the
// whole turned picture, the empty corners included, and keepRatio does not apply. A turn so
// strong that part of the picture would lie beyond the horizon has no whole picture: its size
// is then 0 x 0.
struct TiltPlan {
    Homography transform;  // as tiltTransform
    RectD kept;            // in the transform's coordinates
    Size size;             // of the result, in whole pixels
};
TiltPlan planTilt(Size image, double focalPixels, Tilt tilt, bool keepRatio, bool crop = true);

// The largest rectangle of a ratio, width to height, inside a convex quadrilateral given in
// order around it. A ratio of 0 takes the largest of any ratio.
RectD largestRectangle(const std::array<PointD, 4>& quad, double ratio);

// A line the user drew or that was found, in pixels of the picture. It is to become vertical if
// it is closer to vertical than to horizontal, and horizontal otherwise. A line the user drew
// or moved is meant, so a robust fit never leaves it out.
struct GuideLine {
    PointD a;
    PointD b;
    bool meant = false;
    bool isVertical() const;
    double length() const;
};

// Which angles the lines may set.
enum class TiltMode { Rotation, RotationAndVerticals, Full };

// The angles that make the lines vertical or horizontal as they lean, starting from those given.
// Pitch is set only by two vertical lines or more, and yaw only by two horizontal ones, and only
// where the mode allows; the rest stay as given. With robust set, lines that disagree with
// the others are left out, unless drawn, and used tells which were kept. Two of a kind stay
// if two were used, as they are what sets that angle.
struct TiltFit {
    Tilt tilt;
    std::vector<bool> used;
};
TiltFit fitTilt(std::span<const GuideLine> lines, Size image, double focalPixels, TiltMode mode, bool robust, Tilt start = {});

// Long straight edges in an 8-bit picture, best first, at most maxLines. Meant for a reduced
// picture of about a thousand pixels across, which keeps it quick and noise low.
std::vector<GuideLine> detectLines(const ImageBuffer& bgrx, int maxLines = 60);

}  // namespace iv
