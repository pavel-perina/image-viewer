#include <atomic>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <tiffio.h>

#include "core/color.h"
#include "core/image_source.h"
#include "core/pixel_probe.h"
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
    ~ProbeFile() { std::filesystem::remove(m_path); }

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
