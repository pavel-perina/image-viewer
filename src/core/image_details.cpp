#include "core/image_details.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <string_view>

#include "core/color.h"

namespace iv {

namespace {

std::string lowered(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Cameras often repeat the maker in the model: "Canon" and "Canon EOS 5D", or
// "NIKON CORPORATION" and "NIKON D850".
std::string cameraName(const ExifData& exif) {
    const std::string_view make = exif.make;
    const std::string firstWord = lowered(make.substr(0, make.find(' ')));
    if (exif.model.empty()) {
        return exif.make;
    }
    if (make.empty() || lowered(exif.model).starts_with(firstWord)) {
        return exif.model;
    }
    return exif.make + " " + exif.model;
}

std::string exposureText(double seconds) {
    if (seconds <= 0) {
        return {};
    }
    if (seconds < 1) {
        return std::format("1/{} s", static_cast<long>(std::lround(1 / seconds)));
    }
    return std::format("{:g} s", std::round(seconds * 10) / 10);
}

std::string depthText(const StoredFormat& stored) {
    if (stored.floatSamples) {
        return std::format("{}-bit float", stored.bitsPerSample);
    }
    return std::format("{}-bit{}", stored.bitsPerSample, stored.signedSamples ? " signed" : "");
}

const char* meteringText(int mode) {
    switch (mode) {
    case 1: return "Average";
    case 2: return "Center-weighted";
    case 3: return "Spot";
    case 4: return "Multi-spot";
    case 5: return "Pattern";
    case 6: return "Partial";
    default: return "";
    }
}

const char* whiteBalanceText(int mode) {
    switch (mode) {
    case 0: return "Auto";
    case 1: return "Manual";
    default: return "";
    }
}

// All the samples of a pixel: 24 for 8-bit RGB. A palette has one index per pixel.
int bitsPerPixel(const StoredFormat& stored) {
    const std::string_view model = stored.colorModel;
    int samples = 1;
    if (model.starts_with("RGB") || model.starts_with("YCbCr") || model.starts_with("Lab")) {
        samples = 3;
    } else if (model.starts_with("CMYK") || model.starts_with("YCCK")) {
        samples = 4;
    }
    if (model.contains("alpha")) {
        ++samples;
    }
    return samples * stored.bitsPerSample;
}

std::string megapixelsText(Size size) {
    const double megapixels = static_cast<double>(size.width) * size.height / 1e6;
    return megapixels >= 1 ? std::format("{:.1f} MP", megapixels) : std::format("{:.2f} MP", megapixels);
}

}  // namespace

std::string chromaSubsamplingName(int lumaH, int lumaV, int chromaH, int chromaV) {
    if (chromaH <= 0 || chromaV <= 0) {
        return {};
    }
    const int h = lumaH / chromaH;
    const int v = lumaV / chromaV;
    if (h == 1 && v == 1) { return "4:4:4"; }
    if (h == 2 && v == 1) { return "4:2:2"; }
    if (h == 2 && v == 2) { return "4:2:0"; }
    if (h == 1 && v == 2) { return "4:4:0"; }
    if (h == 4 && v == 1) { return "4:1:1"; }
    if (h == 4 && v == 2) { return "4:1:0"; }
    return std::format("{}x{}", lumaH, lumaV);
}

std::vector<DetailRow> describeImage(const SubimageInfo& subimage) {
    std::vector<DetailRow> rows;
    const auto add = [&](const char* section, const char* label, std::string value) {
        if (!value.empty()) {
            rows.push_back({section, label, std::move(value)});
        }
    };

    const StoredFormat& stored = subimage.stored;
    const char* const image = "Image";
    if (!subimage.levels.empty()) {
        const Size size = displaySize(subimage.levels.front().size, subimage.orientation);
        add(image, "Dimensions", std::format("{} x {}", size.width, size.height));
        add(image, "Megapixels", megapixelsText(size));
    }
    add(image, "Format", stored.container);
    add(image, "Bit depth", depthText(stored));
    add(image, "Color", stored.colorModel);
    add(image, "Compression", stored.compression);
    add(image, "Chroma", stored.chromaSubsampling);
    if (stored.storedLevels > 1) {
        add(image, "Pyramid", std::format("{} levels", stored.storedLevels));
    }
    if (stored.animated) {
        add(image, "Animation", "First frame shown");
    }
    if (!subimage.iccProfile.empty()) {
        const std::string name = iccDescription(subimage.iccProfile);
        add(image, "Profile", name.empty() ? "Embedded" : name);
    }

    const ExifData& exif = subimage.exif;
    const char* const camera = "Camera";
    add(camera, "Taken", exif.dateTaken);
    add(camera, "Camera", cameraName(exif));
    add(camera, "Lens", exif.lens);
    if (exif.focalLength && *exif.focalLength > 0) {
        add(camera, "Focal length",
            exif.focalLength35mm ? std::format("{:g} mm ({:g} mm in 35mm)", std::round(*exif.focalLength * 10) / 10, *exif.focalLength35mm)
                                 : std::format("{:g} mm", std::round(*exif.focalLength * 10) / 10));
    } else if (exif.focalLength35mm) {
        add(camera, "Focal length", std::format("{:g} mm in 35mm", *exif.focalLength35mm));
    }
    if (exif.exposureSeconds) {
        add(camera, "Exposure", exposureText(*exif.exposureSeconds));
    }
    if (exif.fNumber && *exif.fNumber > 0) {
        add(camera, "Aperture", std::format("f/{:g}", std::round(*exif.fNumber * 10) / 10));
    }
    if (exif.iso) {
        add(camera, "ISO", std::to_string(*exif.iso));
    }
    if (exif.exposureBias) {
        const double bias = std::round(*exif.exposureBias * 10) / 10;
        add(camera, "Exposure bias", bias == 0 ? std::string("0 EV") : std::format("{:+g} EV", bias));
    }
    if (exif.meteringMode) {
        add(camera, "Metering", meteringText(*exif.meteringMode));
    }
    if (exif.whiteBalance) {
        add(camera, "White balance", whiteBalanceText(*exif.whiteBalance));
    }
    return rows;
}

std::string imageSummary(const SubimageInfo& subimage) {
    const StoredFormat& stored = subimage.stored;
    std::string text;
    if (!subimage.levels.empty()) {
        const Size size = displaySize(subimage.levels.front().size, subimage.orientation);
        const double megapixels = static_cast<double>(size.width) * size.height / 1e6;
        text = megapixels >= 1 ? std::format("{}x{} {:.1f}MP ", size.width, size.height, megapixels)
                               : std::format("{}x{} {:.2f}MP ", size.width, size.height, megapixels);
    }
    text += std::format("{}bit{}", bitsPerPixel(stored), stored.floatSamples ? " float" : "");
    if (!stored.container.empty()) {
        text += " " + stored.container;
    }
    return text;
}

std::string cameraSummary(const ExifData& exif) {
    std::string text;
    const auto add = [&](const std::string& part) {
        if (!part.empty()) {
            text += (text.empty() ? "" : " ") + part;
        }
    };
    if (exif.exposureSeconds && *exif.exposureSeconds > 0) {
        std::string exposure = exposureText(*exif.exposureSeconds);
        std::erase(exposure, ' ');
        add(exposure);
    }
    if (exif.fNumber && *exif.fNumber > 0) {
        add(std::format("f/{:g}", std::round(*exif.fNumber * 10) / 10));
    }
    if (exif.iso) {
        add(std::format("ISO-{}", *exif.iso));
    }
    if (exif.exposureBias) {
        const double bias = std::round(*exif.exposureBias * 100) / 100;
        if (bias != 0) {
            add(std::format("EV {:+g}", bias));
        }
    }
    return text;
}

std::string shortDataSize(std::uint64_t bytes) {
    if (bytes < 1000) {
        return std::format("{} B", bytes);
    }
    if (bytes < 999'500) {
        return std::format("{} kB", std::lround(static_cast<double>(bytes) / 1e3));
    }
    const double megabytes = static_cast<double>(bytes) / 1e6;
    if (megabytes < 999.95) {
        return std::format("{:.1f} MB", megabytes);
    }
    return std::format("{:.1f} GB", megabytes / 1e3);
}

}  // namespace iv
