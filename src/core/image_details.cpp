#include "core/image_details.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <string_view>

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
        add(image, "Profile", "Embedded");
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
    return rows;
}

}  // namespace iv
