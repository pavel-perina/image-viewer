#include "core/color.h"

#include <lcms2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <tuple>

#include "core/image_source.h"

namespace iv {

namespace {

constexpr std::size_t kMaxCachedTransforms = 16;
// Output is 8-bit, so a difference this small between source and display cannot be seen.
constexpr int kIdentityTolerance = 1;
constexpr int kIdentityGridSteps = 9;

constexpr cmsUInt32Number kGray8 = COLORSPACE_SH(PT_GRAY) | CHANNELS_SH(1) | EXTRA_SH(3) | BYTES_SH(1);
constexpr cmsUInt32Number kGray16 = COLORSPACE_SH(PT_GRAY) | CHANNELS_SH(1) | EXTRA_SH(3) | BYTES_SH(2);
constexpr cmsUInt32Number kGrayFloat = FLOAT_SH(1) | COLORSPACE_SH(PT_GRAY) | CHANNELS_SH(1) | EXTRA_SH(3) | BYTES_SH(4);

struct ProfileCloser {
    void operator()(void* profile) const { cmsCloseProfile(profile); }
};
using Profile = std::unique_ptr<void, ProfileCloser>;

// Damaged profiles are expected and already handled, so lcms need not report them.
void quietenLcms() {
    static std::once_flag once;
    std::call_once(once, [] { cmsSetLogErrorHandler([](cmsContext, cmsUInt32Number, const char*) {}); });
}

Profile openProfile(std::span<const std::uint8_t> bytes) {
    quietenLcms();
    if (bytes.empty()) {
        return nullptr;
    }
    return Profile(cmsOpenProfileFromMem(bytes.data(), static_cast<cmsUInt32Number>(bytes.size())));
}

IccBytes saveProfile(cmsHPROFILE profile) {
    cmsUInt32Number size = 0;
    if (!cmsSaveProfileToMem(profile, nullptr, &size) || size == 0) {
        return {};
    }
    IccBytes bytes(size);
    return cmsSaveProfileToMem(profile, bytes.data(), &size) ? bytes : IccBytes{};
}

cmsToneCurve* makeCurve(double gamma) {
    if (gamma > 0) {
        return cmsBuildGamma(nullptr, gamma);
    }
    const std::array<cmsFloat64Number, 5> srgb{2.4, 1 / 1.055, 0.055 / 1.055, 1 / 12.92, 0.04045};
    return cmsBuildParametricToneCurve(nullptr, 4, srgb.data());
}

bool near(double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; }

bool nearPoint(const Chromaticity& a, const Chromaticity& b) {
    constexpr double kTolerance = 0.002;
    return near(a.x, b.x, kTolerance) && near(a.y, b.y, kTolerance);
}

bool isSrgb(const RgbPrimaries& p) {
    return nearPoint(p.red, kSrgbPrimaries.red) && nearPoint(p.green, kSrgbPrimaries.green) &&
           nearPoint(p.blue, kSrgbPrimaries.blue) && nearPoint(p.white, kSrgbPrimaries.white);
}

cmsUInt32Number eightBitInput(bool gray) { return gray ? kGray8 : TYPE_BGRA_8; }

cmsUInt32Number directInput(PixelFormat format, bool gray) {
    if (format == PixelFormat::Rgba16) {
        return gray ? kGray16 : TYPE_RGBA_16;
    }
    return gray ? kGrayFloat : TYPE_RGBA_FLT;
}

cmsHTRANSFORM makeTransform(cmsHPROFILE source, cmsUInt32Number input, cmsHPROFILE display) {
    // Colorimetric keeps colours inside the display's gamut exactly, and black point
    // compensation keeps shadows from clipping. NOCACHE makes the handle safe across threads.
    return cmsCreateTransform(source, input, display, TYPE_BGRA_8, INTENT_RELATIVE_COLORIMETRIC,
                              cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE);
}

bool isIdentity(cmsHTRANSFORM transform, bool gray) {
    const auto matches = [](int in, int out) { return std::abs(in - out) <= kIdentityTolerance; };
    if (gray) {
        for (int v = 0; v < 256; ++v) {
            const std::array<std::uint8_t, 4> in{static_cast<std::uint8_t>(v), 0, 0, 255};
            std::array<std::uint8_t, 4> out{};
            cmsDoTransform(transform, in.data(), out.data(), 1);
            if (!matches(v, out[0]) || !matches(v, out[1]) || !matches(v, out[2])) {
                return false;
            }
        }
        return true;
    }
    const auto level = [](int step) { return static_cast<std::uint8_t>(step * 255 / (kIdentityGridSteps - 1)); };
    for (int b = 0; b < kIdentityGridSteps; ++b) {
        for (int g = 0; g < kIdentityGridSteps; ++g) {
            for (int r = 0; r < kIdentityGridSteps; ++r) {
                const std::array<std::uint8_t, 4> in{level(b), level(g), level(r), 255};
                std::array<std::uint8_t, 4> out{};
                cmsDoTransform(transform, in.data(), out.data(), 1);
                if (!matches(in[0], out[0]) || !matches(in[1], out[1]) || !matches(in[2], out[2])) {
                    return false;
                }
            }
        }
    }
    return true;
}

std::shared_ptr<const ColorTransform> buildTransform(const IccBytes& sourceBytes, bool grayPixels, PixelFormat format,
                                                     const IccBytes* displayBytes) {
    Profile source = openProfile(sourceBytes);
    bool gray = false;
    if (source) {
        const cmsColorSpaceSignature space = cmsGetColorSpace(source.get());
        gray = space == cmsSigGrayData && grayPixels;
        // A CMYK profile, say, says nothing about pixels that were already converted to RGB.
        if (space != cmsSigRgbData && !gray) {
            source.reset();
        }
    }
    if (!source) {
        source.reset(cmsCreate_sRGBProfile());
    }
    Profile display = displayBytes ? openProfile(*displayBytes) : nullptr;
    if (!display) {
        display.reset(cmsCreate_sRGBProfile());
    }
    if (!source || !display) {
        return nullptr;
    }

    void* const eightBit = makeTransform(source.get(), eightBitInput(gray), display.get());
    if (!eightBit) {
        return nullptr;
    }
    if (isIdentity(eightBit, gray)) {
        cmsDeleteTransform(eightBit);
        return nullptr;
    }
    void* direct = nullptr;
    if (format != PixelFormat::Bgrx8) {
        direct = makeTransform(source.get(), directInput(format, gray), display.get());
        if (!direct) {
            cmsDeleteTransform(eightBit);
            return nullptr;
        }
    }
    return std::make_shared<const ColorTransform>(eightBit, direct, format);
}

struct CacheEntry {
    IccBytes profile;
    bool gray = false;
    PixelFormat format = PixelFormat::Bgrx8;
    std::shared_ptr<const IccBytes> display;
    std::shared_ptr<const ColorTransform> transform;
};

}  // namespace

IccBytes makeRgbProfile(const std::string& name, const RgbPrimaries& p, double gamma) {
    quietenLcms();
    const cmsCIExyY white{p.white.x, p.white.y, 1.0};
    const cmsCIExyYTRIPLE primaries{{p.red.x, p.red.y, 1.0}, {p.green.x, p.green.y, 1.0}, {p.blue.x, p.blue.y, 1.0}};
    cmsToneCurve* const curve = makeCurve(gamma);
    if (!curve) {
        return {};
    }
    const std::array<cmsToneCurve*, 3> curves{curve, curve, curve};
    const Profile profile(cmsCreateRGBProfile(&white, &primaries, curves.data()));
    cmsFreeToneCurve(curve);
    if (!profile) {
        return {};
    }
    cmsMLU* const description = cmsMLUalloc(nullptr, 1);
    if (description) {
        cmsMLUsetASCII(description, "en", "US", name.c_str());
        cmsWriteTag(profile.get(), cmsSigProfileDescriptionTag, description);
        cmsMLUfree(description);
    }
    return saveProfile(profile.get());
}

IccBytes adobeRgbProfile() {
    return makeRgbProfile("Adobe RGB (1998)", kAdobeRgbPrimaries, 563.0 / 256.0);
}

IccBytes profileFromCicp(int primaries, int transfer) {
    RgbPrimaries p = kSrgbPrimaries;
    std::string name;
    switch (primaries) {
    case 1:
    case 2:  // unspecified
        break;
    case 9:
        p = {{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}, {0.3127, 0.3290}};
        name = "BT.2020";
        break;
    case 11:
        p = {{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}, {0.314, 0.351}};
        name = "DCI-P3";
        break;
    case 12:
        p = {{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}, {0.3127, 0.3290}};
        name = "Display P3";
        break;
    default:
        return {};
    }

    double gamma = 0;
    std::string curve;
    switch (transfer) {
    case 1:   // BT.709, and its equivalents below, are shown with the sRGB curve like browsers do
    case 2:   // unspecified
    case 6:
    case 13:
    case 14:
    case 15:
        break;
    case 4:
        gamma = 2.2;
        curve = "gamma 2.2";
        break;
    case 5:
        gamma = 2.8;
        curve = "gamma 2.8";
        break;
    case 8:
        gamma = 1.0;
        curve = "linear";
        break;
    default:
        return {};
    }

    if (name.empty() && curve.empty()) {
        return {};
    }
    if (name.empty()) {
        name = "sRGB primaries";
    }
    return makeRgbProfile(curve.empty() ? name : name + ", " + curve, p, gamma);
}

IccBytes profileFromPngGamma(double fileGamma, const RgbPrimaries* primaries) {
    if (fileGamma <= 0) {
        return {};
    }
    const bool srgbPrimaries = !primaries || isSrgb(*primaries);
    if (srgbPrimaries && near(fileGamma, 1 / 2.2, 0.002)) {
        return {};
    }
    return makeRgbProfile(srgbPrimaries ? "From PNG gamma" : "From PNG gamma and primaries", primaries ? *primaries : kSrgbPrimaries,
                          1 / fileGamma);
}

std::string iccDescription(std::span<const std::uint8_t> bytes) {
    const Profile profile = openProfile(bytes);
    if (!profile) {
        return {};
    }
    std::array<char, 256> text{};
    const cmsUInt32Number length = cmsGetProfileInfoASCII(profile.get(), cmsInfoDescription, "en", "US", text.data(),
                                                          static_cast<cmsUInt32Number>(text.size()));
    if (length == 0) {
        return {};
    }
    std::string out;
    for (const char c : std::string_view(text.data(), std::min<std::size_t>(length, text.size() - 1))) {
        if (c == '\0') {
            break;
        }
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F) {
            out.push_back(c);
        }
    }
    return out;
}

std::string displayProfileProblem(std::span<const std::uint8_t> bytes) {
    const Profile profile = openProfile(bytes);
    if (!profile) {
        return "This is not a valid ICC profile.";
    }
    if (cmsGetColorSpace(profile.get()) != cmsSigRgbData) {
        return "This profile is not for an RGB device.";
    }
    const Profile srgb(cmsCreate_sRGBProfile());
    cmsHTRANSFORM transform = srgb ? makeTransform(srgb.get(), TYPE_BGRA_8, profile.get()) : nullptr;
    if (!transform) {
        return "This profile cannot be used for output to a display.";
    }
    cmsDeleteTransform(transform);
    return {};
}

ColorTransform::ColorTransform(void* eightBit, void* direct, PixelFormat format)
    : m_eightBit(eightBit), m_direct(direct), m_format(format) {}

ColorTransform::~ColorTransform() {
    if (m_eightBit) {
        cmsDeleteTransform(m_eightBit);
    }
    if (m_direct) {
        cmsDeleteTransform(m_direct);
    }
}

ImageBuffer ColorTransform::apply(const ImageBuffer& source) const {
    void* const transform = source.format() == PixelFormat::Bgrx8 ? m_eightBit : source.format() == m_format ? m_direct : nullptr;
    if (!transform) {
        return source;
    }
    const ImageBuffer target = ImageBuffer::create(source.size(), PixelFormat::Bgrx8);
    const auto width = static_cast<std::size_t>(source.width());
    for (int y = 0; y < source.height(); ++y) {
        std::memset(target.row(y), 0xFF, width * 4);  // lcms leaves the fourth byte alone
        cmsDoTransform(transform, source.row(y), target.row(y), static_cast<cmsUInt32Number>(width));
    }
    return target;
}

std::shared_ptr<const ColorTransform> displayTransform(const SubimageInfo& subimage, const ColorTarget& target) {
    // 8-bit gray is spread to the 8-bit layout before it is transformed.
    const PixelFormat format = subimage.format == PixelFormat::Gray8 ? PixelFormat::Bgrx8 : subimage.format;
    if (!target.enabled || format == PixelFormat::Gray16 || format == PixelFormat::Gray32F) {
        return nullptr;
    }
    if (subimage.iccProfile.empty() && !target.displayProfile) {
        return nullptr;
    }
    const bool gray = subimage.format == PixelFormat::Gray8 || subimage.stored.colorModel.starts_with("Gray");

    static std::mutex mutex;
    static std::vector<CacheEntry> cache;
    const std::scoped_lock lock(mutex);
    for (const CacheEntry& entry : cache) {
        if (entry.gray == gray && entry.format == format && entry.display == target.displayProfile &&
            entry.profile == subimage.iccProfile) {
            return entry.transform;
        }
    }
    auto transform = buildTransform(subimage.iccProfile, gray, format, target.displayProfile.get());
    if (cache.size() >= kMaxCachedTransforms) {
        cache.erase(cache.begin());
    }
    cache.push_back({subimage.iccProfile, gray, format, target.displayProfile, transform});
    return transform;
}

}  // namespace iv
