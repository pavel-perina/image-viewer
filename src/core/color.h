#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/image_buffer.h"

namespace iv {

struct SubimageInfo;

using IccBytes = std::vector<std::uint8_t>;

struct Chromaticity {
    double x = 0;
    double y = 0;
};

struct RgbPrimaries {
    Chromaticity red, green, blue, white;
};

inline constexpr RgbPrimaries kSrgbPrimaries{{0.64, 0.33}, {0.30, 0.60}, {0.15, 0.06}, {0.3127, 0.3290}};
inline constexpr RgbPrimaries kAdobeRgbPrimaries{{0.64, 0.33}, {0.21, 0.71}, {0.15, 0.06}, {0.3127, 0.3290}};

// A gamma of 0 selects the sRGB curve. Empty if the values cannot make a profile.
IccBytes makeRgbProfile(const std::string& name, const RgbPrimaries& primaries, double gamma);

IccBytes adobeRgbProfile();

// A profile for the code points of H.273, which AVIF and PNG use instead of a profile. Empty
// when they mean sRGB, or for what a profile cannot express such as HDR transfer curves.
IccBytes profileFromCicp(int primaries, int transfer);

// A profile for the gAMA and cHRM values of a PNG; primaries may be null. Empty when they
// amount to sRGB, which is what encoders usually mean by a gamma of 1/2.2.
IccBytes profileFromPngGamma(double fileGamma, const RgbPrimaries* primaries);

// The name a profile gives itself. Empty if it cannot be read or has none.
std::string iccDescription(std::span<const std::uint8_t> profile);

// Why a profile cannot describe a display. Empty if it can.
std::string displayProfileProblem(std::span<const std::uint8_t> profile);

// What images are shown on.
struct ColorTarget {
    bool enabled = true;
    std::shared_ptr<const IccBytes> displayProfile;  // null for sRGB
};

// Converts the pixels of a subimage for a display. Safe to use from several threads.
class ColorTransform {
public:
    ColorTransform(void* eightBit, void* direct, void* wide, PixelFormat format);
    ~ColorTransform();
    ColorTransform(const ColorTransform&) = delete;
    ColorTransform& operator=(const ColorTransform&) = delete;

    // Takes either the subimage's own format, or Bgrx8 with its colours in the same encoding.
    // Returns Bgrx8.
    ImageBuffer apply(const ImageBuffer& source) const;
    // Takes Rgba16 with the colours in the subimage's encoding, as toDisplayWide makes it, and
    // returns Rgba16 for the display. The same buffer back for 8-bit subimages, which have none.
    ImageBuffer applyWide(const ImageBuffer& source) const;

private:
    void* m_eightBit;
    void* m_direct;
    void* m_wide;
    PixelFormat m_format;
};

// Null when nothing needs converting, which is the usual case and costs nothing: colour
// management is off, the pixels are not colours as such, or source and display match. Files
// without a profile, or with one that is damaged or unusable for their pixels, count as sRGB.
// Results are cached, since a folder tends to use one or two profiles throughout.
std::shared_ptr<const ColorTransform> displayTransform(const SubimageInfo& subimage, const ColorTarget& target);

}  // namespace iv
