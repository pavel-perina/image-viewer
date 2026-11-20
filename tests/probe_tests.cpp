#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <tiffio.h>

#include "core/color.h"
#include "core/image_source.h"
#include "core/oklab.h"
#include "core/pixel_probe.h"
#include "core/whole_image_source.h"
#include "test_tiff.h"

using namespace iv;
using namespace iv::test;

namespace {

class ProbeFile {
public:
    explicit ProbeFile(const TiffImage& image) {
        static std::atomic<int> counter = 0;
        m_path = std::filesystem::temp_directory_path() /
                 ("iv_probe_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + "_" + std::to_string(++counter) + ".tif");
        writeTiff(m_path, {image});
        auto opened = openImageSource(m_path);
        EXPECT_TRUE(opened.has_value()) << (opened ? "" : opened.error());
        if (opened) {
            source = *opened;
        }
    }
    ~ProbeFile() {
        source.reset();  // Windows cannot delete a file that is open
        std::filesystem::remove(m_path);
    }

    std::shared_ptr<ImageSource> source;

private:
    std::filesystem::path m_path;
};

// Gray everywhere but one pixel, in a tile other than the first.
TiffImage withPixelAt(int x, int y, Rgb colour) {
    TiffImage image;
    image.width = 50;
    image.height = 40;
    image.tileSize = 16;
    image.pixel = [=](int px, int py) { return px == x && py == y ? colour : Rgb{128, 128, 128}; };
    return image;
}

PixelProbe probe(const ProbeFile& file, int x, int y) {
    auto result = probePixel(*file.source, 0, x, y, {});
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error());
    return result ? *result : PixelProbe{};
}

}  // namespace

TEST(Probe, ReadsEightBitsAsTheyAre) {
    const ProbeFile file(withPixelAt(37, 21, {64, 80, 96}));
    const PixelProbe p = probe(file, 37, 21);
    EXPECT_EQ(p.channels, 3);
    EXPECT_EQ(hexText(p.eightBit), "#405060");
    EXPECT_EQ(valuesText(p), "64, 80, 96");
    EXPECT_EQ(fractionsText(p), "0.251, 0.314, 0.376");
    EXPECT_FALSE(p.srgb) << "no profile means sRGB already";
    EXPECT_EQ(valuesText(probe(file, 36, 21)), "128, 128, 128");
}

TEST(Probe, KeepsSixteenBits) {
    TiffImage image = withPixelAt(37, 21, {64, 80, 96});
    image.bits = 16;
    const ProbeFile file(image);
    const PixelProbe p = probe(file, 37, 21);
    EXPECT_EQ(valuesText(p), "16448, 20560, 24672");
    EXPECT_EQ(fractionsText(p), "0.25098, 0.31373, 0.37647");
    EXPECT_EQ(hexText(p.eightBit), "#405060");
}

TEST(Probe, ShowsFloatsAsTheyAre) {
    TiffImage image = withPixelAt(37, 21, {64, 80, 96});
    image.bits = 32;
    image.sampleFormat = SAMPLEFORMAT_IEEEFP;
    const ProbeFile file(image);
    const PixelProbe p = probe(file, 37, 21);
    EXPECT_EQ(valuesText(p), "0.251, 0.3137, 0.3765");
    EXPECT_EQ(fractionsText(p), valuesText(p));
    EXPECT_EQ(hexText(p.eightBit), "#405060");
}

TEST(Probe, GivesGrayOneValue) {
    TiffImage image = withPixelAt(3, 3, {100, 100, 100});
    image.samples = 1;
    const ProbeFile eight(image);
    EXPECT_EQ(probe(eight, 3, 3).channels, 1);
    EXPECT_EQ(valuesText(probe(eight, 3, 3)), "100");
    image.bits = 16;
    const ProbeFile sixteen(image);
    EXPECT_EQ(valuesText(probe(sixteen, 3, 3)), "25700");
    EXPECT_EQ(hexText(probe(sixteen, 3, 3).eightBit), "#646464");
}

TEST(Probe, ConvertsToSrgbThroughTheProfile) {
    TiffImage image = withPixelAt(5, 5, {100, 150, 200});
    image.iccProfile = adobeRgbProfile();
    const ProbeFile file(image);
    const PixelProbe p = probe(file, 5, 5);
    EXPECT_EQ(hexText(p.eightBit), "#6496c8");
    ASSERT_TRUE(p.srgb);
    // Adobe RGB has a wider green, so the same numbers are a more saturated colour in sRGB.
    EXPECT_NE(*p.srgb, p.eightBit);
    EXPECT_LT((*p.srgb)[0], 100);
}

TEST(Probe, RefusesPixelsOutsideTheImage) {
    const ProbeFile file(withPixelAt(0, 0, {0, 0, 0}));
    EXPECT_FALSE(probePixel(*file.source, 0, 50, 0, {}));
    EXPECT_FALSE(probePixel(*file.source, 0, -1, 0, {}));
    EXPECT_FALSE(probePixel(*file.source, 1, 0, 0, {}));
}

namespace {

// An 8-bit picture held in memory, each pixel from a function giving R, G, B and alpha.
std::shared_ptr<ImageSource> memoryPicture(Size size, bool alpha, std::function<std::array<int, 4>(int, int)> pixel) {
    SubimageInfo info;
    info.format = PixelFormat::Bgrx8;
    info.alpha = alpha;
    info.levels.push_back({size, {256, 256}});
    info.stored.colorModel = alpha ? "RGB + alpha" : "RGB";
    return makeWholeImageSource(info, size, [=](const std::stop_token&) -> Result<ImageBuffer> {
        const ImageBuffer image = ImageBuffer::create(size, PixelFormat::Bgrx8).withAlpha(alpha);
        for (int y = 0; y < size.height; ++y) {
            auto* p = reinterpret_cast<std::uint8_t*>(image.row(y));
            for (int x = 0; x < size.width; ++x, p += 4) {
                const auto c = pixel(x, y);
                p[0] = static_cast<std::uint8_t>(c[2]);
                p[1] = static_cast<std::uint8_t>(c[1]);
                p[2] = static_cast<std::uint8_t>(c[0]);
                p[3] = static_cast<std::uint8_t>(c[3]);
            }
        }
        return image;
    });
}

double linear8(int v) { return srgbDecode(v / 255.0); }

}  // namespace

// Why: Pick gray must average what it covers, as one noisy pixel would shift the white balance.
// Input: a gray of 120 with a fixed pattern of noise around it, across tiles of 16 pixels; a
// patch of 5 and single pixels; then a rectangle of two halves, 100 and 140. The truth: the
// patch nearer the gray than the typical single pixel, and the rectangle's mean in linear light.
TEST(PatchAverage, AveragesWhatItCovers) {
    TiffImage image;
    image.width = 40;
    image.height = 40;
    image.tileSize = 16;
    image.pixel = [](int x, int y) {
        const int noise = ((x * 7 + y * 13) % 9) - 4;  // -4 to +4, evenly spread
        return Rgb{static_cast<std::uint8_t>(120 + noise), static_cast<std::uint8_t>(120 - noise), 120};
    };
    ProbeFile file(image);
    ASSERT_TRUE(file.source);
    const double gray = linear8(120);
    const auto patch = patchAverage(*file.source, 0, 14, 14, 5, 5, {});  // across four tiles
    ASSERT_TRUE(patch) << patch.error();
    double singles = 0;
    for (int i = 0; i < 25; ++i) {
        const auto one = patchAverage(*file.source, 0, 14 + i % 5, 14 + i / 5, 1, 1, {});
        singles += std::abs((*one)[0] - gray);
    }
    EXPECT_LT(std::abs((*patch)[0] - gray), singles / 25 / 3);

    const auto halves = memoryPicture({10, 4}, false, [](int x, int) {
        const int v = x < 5 ? 100 : 140;
        return std::array<int, 4>{v, v, v, 255};
    });
    const auto mean = patchAverage(*halves, 0, -3, 0, 100, 4, {});  // cut to the picture
    ASSERT_TRUE(mean) << mean.error();
    EXPECT_NEAR((*mean)[1], (linear8(100) + linear8(140)) / 2, 1e-9);
}

// Why: patches whose colour says nothing about the light must be refused, with the reason,
// rather than set a wrong white balance. Input: a transparent patch, a black one, a clipped
// one, and a gray picture; then a patch half clipped by a small part, which is kept. The truth:
// refusals naming each, and the mean of the unclipped pixels for the last.
TEST(PatchAverage, RefusesTransparentBlackAndClippedPatches) {
    const auto picture = memoryPicture({30, 10}, true, [](int x, int) {
        if (x < 10) {
            return std::array<int, 4>{128, 128, 128, 20};  // nearly transparent
        }
        if (x < 20) {
            return std::array<int, 4>{2, 3, 2, 255};
        }
        return std::array<int, 4>{255, 255, 250, 255};
    });
    const auto transparent = patchAverage(*picture, 0, 0, 0, 5, 5, {});
    ASSERT_FALSE(transparent);
    EXPECT_NE(transparent.error().find("transparent"), std::string::npos);
    const auto black = patchAverage(*picture, 0, 12, 0, 5, 5, {});
    ASSERT_FALSE(black);
    EXPECT_NE(black.error().find("dark"), std::string::npos);
    const auto clipped = patchAverage(*picture, 0, 22, 0, 5, 5, {});
    ASSERT_FALSE(clipped);
    EXPECT_NE(clipped.error().find("Clipped"), std::string::npos);

    TiffImage grayImage;
    grayImage.samples = 1;
    ProbeFile gray(grayImage);
    ASSERT_TRUE(gray.source);
    EXPECT_FALSE(patchAverage(*gray.source, 0, 0, 0, 5, 5, {}));

    // One clipped column of five: left out, the rest averaged.
    const auto some = memoryPicture({5, 5}, false, [](int x, int) {
        return x == 0 ? std::array<int, 4>{255, 255, 255, 255} : std::array<int, 4>{90, 100, 110, 255};
    });
    const auto kept = patchAverage(*some, 0, 0, 0, 5, 5, {});
    ASSERT_TRUE(kept) << kept.error();
    EXPECT_NEAR((*kept)[2], linear8(110), 1e-9);
}
