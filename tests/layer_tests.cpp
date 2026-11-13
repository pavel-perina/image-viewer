// Putting a drawn layer on a picture and pixelating, in core/image_layer.h: what applying
// annotations does to the pixels. Pictures here are a few pixels of one known value, and the
// expected values follow from the blend formulas, worked out in the comments.

#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "core/image_layer.h"

using namespace iv;

namespace {

template <typename Sample>
ImageBuffer filled(Size size, PixelFormat format, std::initializer_list<Sample> pixel) {
    const ImageBuffer image = ImageBuffer::create(size, format);
    for (int y = 0; y < size.height; ++y) {
        auto* out = reinterpret_cast<Sample*>(image.row(y));
        for (int x = 0; x < size.width; ++x) {
            out = std::copy(pixel.begin(), pixel.end(), out);
        }
    }
    return image;
}

// A layer of one colour, given as it is stored: blue, green, red multiplied by alpha, alpha.
ImageBuffer layerOf(Size size, std::uint8_t b, std::uint8_t g, std::uint8_t r, std::uint8_t a) {
    return filled<std::uint8_t>(size, PixelFormat::Bgrx8, {b, g, r, a});
}

template <typename Sample>
Sample at(const ImageBuffer& image, int x, int y, int channel, int channels) {
    return reinterpret_cast<const Sample*>(image.row(y))[x * channels + channel];
}

}  // namespace

// Why: an opaque layer replaces what is under it, a clear one leaves it, and the picture
// given is not changed, since edits are replayed from it on undo. A layer partly outside
// the picture is cut off rather than written beyond it.
TEST(Layer, CoversWhereItIsOpaqueAndLeavesTheOriginalAlone) {
    const ImageBuffer image = filled<std::uint8_t>({4, 4}, PixelFormat::Bgrx8, {10, 20, 30, 255});
    // Red, at 2, 2 and reaching two pixels beyond the corner.
    const ImageBuffer result = blendLayer(image, layerOf({4, 4}, 0, 0, 255, 255), 2, 2, LayerBlend::Over, fullRange(image.format()));
    EXPECT_EQ(at<std::uint8_t>(result, 3, 3, 2, 4), 255);
    EXPECT_EQ(at<std::uint8_t>(result, 3, 3, 1, 4), 0);
    EXPECT_EQ(at<std::uint8_t>(result, 1, 1, 2, 4), 30) << "outside the layer";
    EXPECT_EQ(at<std::uint8_t>(image, 3, 3, 2, 4), 30) << "the original";

    const ImageBuffer clear = blendLayer(image, layerOf({4, 4}, 0, 0, 0, 0), 0, 0, LayerBlend::Over, fullRange(image.format()));
    EXPECT_EQ(std::memcmp(clear.row(2), image.row(2), 16), 0);
    const ImageBuffer beyond = blendLayer(image, layerOf({2, 2}, 0, 0, 255, 255), -1, -1, LayerBlend::Over, fullRange(image.format()));
    EXPECT_EQ(at<std::uint8_t>(beyond, 0, 0, 2, 4), 255);
    EXPECT_EQ(at<std::uint8_t>(beyond, 1, 1, 2, 4), 30);
}

// Why: antialiased edges and shadows are partly transparent. Half-opaque white (stored as
// 128, 128, 128, 128) over 100 gives 100 * (1 - 128/255) + 128 = 177.8.
TEST(Layer, MixesWhereItIsPartlyTransparent) {
    const ImageBuffer image = filled<std::uint8_t>({2, 2}, PixelFormat::Bgrx8, {100, 100, 100, 255});
    const ImageBuffer result = blendLayer(image, layerOf({2, 2}, 128, 128, 128, 128), 0, 0, LayerBlend::Over, fullRange(image.format()));
    EXPECT_EQ(at<std::uint8_t>(result, 0, 0, 0, 4), 178);
}

// Why: a 16-bit picture has to stay 16-bit. Opaque red on Rgba16, whose samples are in the
// order red, green, blue unlike Bgrx8, gives 65535, 0, 0.
TEST(Layer, KeepsSixteenBitsAndTheirChannelOrder) {
    const ImageBuffer image = filled<std::uint16_t>({2, 2}, PixelFormat::Rgba16, {1000, 2000, 3000, 65535});
    const ImageBuffer result = blendLayer(image, layerOf({1, 1}, 0, 0, 255, 255), 0, 0, LayerBlend::Over, fullRange(image.format()));
    ASSERT_EQ(result.format(), PixelFormat::Rgba16);
    EXPECT_EQ(at<std::uint16_t>(result, 0, 0, 0, 4), 65535);
    EXPECT_EQ(at<std::uint16_t>(result, 0, 0, 1, 4), 0);
    EXPECT_EQ(at<std::uint16_t>(result, 0, 0, 2, 4), 0);
    EXPECT_EQ(at<std::uint16_t>(result, 1, 1, 0, 4), 1000);
}

// Why: a 12-bit picture in a 16-bit file is shown stretched, from 0 to 4095 say. White drawn
// at 65535 would be far above everything, and every colour would look white. So the layer's
// black and white become the range's ends: white is 4095, and mid gray 128/255 of it, 2055.6.
TEST(Layer, DrawsWithinTheRangeThePictureIsShownIn) {
    const ImageBuffer image = filled<std::uint16_t>({2, 1}, PixelFormat::Gray16, {1000});
    const DisplayRange range{0, 4095};
    EXPECT_EQ(at<std::uint16_t>(blendLayer(image, layerOf({1, 1}, 255, 255, 255, 255), 0, 0, LayerBlend::Over, range), 0, 0, 0, 1), 4095);
    EXPECT_EQ(at<std::uint16_t>(blendLayer(image, layerOf({1, 1}, 128, 128, 128, 255), 0, 0, LayerBlend::Over, range), 0, 0, 0, 1), 2056);
    // With a low end above zero, black is the low end: 500.
    EXPECT_EQ(at<std::uint16_t>(blendLayer(image, layerOf({1, 1}, 0, 0, 0, 255), 0, 0, LayerBlend::Over, {500, 4095}), 0, 0, 0, 1), 500);
}

// Why: a highlighter must not hide what it marks. Opaque yellow (blue 0) multiplied onto
// white leaves yellow, onto black leaves black, and onto mid gray 100 keeps red and green
// and removes blue.
TEST(Layer, AMarkerDarkensRatherThanCovers) {
    const DisplayRange range{0, 255};
    const ImageBuffer yellow = layerOf({1, 1}, 0, 255, 255, 255);
    const ImageBuffer onWhite = blendLayer(filled<std::uint8_t>({1, 1}, PixelFormat::Bgrx8, {255, 255, 255, 255}), yellow, 0, 0, LayerBlend::Multiply, range);
    EXPECT_EQ(at<std::uint8_t>(onWhite, 0, 0, 0, 4), 0);
    EXPECT_EQ(at<std::uint8_t>(onWhite, 0, 0, 1, 4), 255);
    EXPECT_EQ(at<std::uint8_t>(onWhite, 0, 0, 2, 4), 255);
    const ImageBuffer onBlack = blendLayer(filled<std::uint8_t>({1, 1}, PixelFormat::Bgrx8, {0, 0, 0, 255}), yellow, 0, 0, LayerBlend::Multiply, range);
    EXPECT_EQ(at<std::uint8_t>(onBlack, 0, 0, 2, 4), 0);
    const ImageBuffer onGray = blendLayer(filled<std::uint8_t>({1, 1}, PixelFormat::Bgrx8, {100, 100, 100, 255}), yellow, 0, 0, LayerBlend::Multiply, range);
    EXPECT_EQ(at<std::uint8_t>(onGray, 0, 0, 0, 4), 0);
    EXPECT_EQ(at<std::uint8_t>(onGray, 0, 0, 2, 4), 100);
}

// Why: a gray picture has one channel and cannot hold a colour. Put on it as it is, red
// becomes its luma, 0.299 * 255 = 76.2; turned to colour first, the picture keeps the red.
TEST(Layer, AGrayPictureTakesTheLumaUnlessTurnedToColourFirst) {
    const ImageBuffer gray = filled<std::uint8_t>({2, 1}, PixelFormat::Gray8, {50});
    const ImageBuffer red = layerOf({1, 1}, 0, 0, 255, 255);
    EXPECT_TRUE(hasColour(red));
    EXPECT_FALSE(hasColour(layerOf({1, 1}, 128, 128, 128, 200)));
    EXPECT_EQ(at<std::uint8_t>(blendLayer(gray, red, 0, 0, LayerBlend::Over, {0, 255}), 0, 0, 0, 1), 76);

    const ImageBuffer colour = toColour(gray);
    ASSERT_EQ(colour.format(), PixelFormat::Bgrx8);
    EXPECT_EQ(at<std::uint8_t>(colour, 1, 0, 0, 4), 50);
    EXPECT_EQ(at<std::uint8_t>(colour, 1, 0, 2, 4), 50);
    const ImageBuffer result = blendLayer(colour, red, 0, 0, LayerBlend::Over, {0, 255});
    EXPECT_EQ(at<std::uint8_t>(result, 0, 0, 2, 4), 255);
    EXPECT_EQ(at<std::uint8_t>(result, 0, 0, 0, 4), 0);

    EXPECT_EQ(toColour(filled<std::uint16_t>({1, 1}, PixelFormat::Gray16, {700})).format(), PixelFormat::Rgba16);
    EXPECT_EQ(toColour(filled<float>({1, 1}, PixelFormat::Gray32F, {0.5F})).format(), PixelFormat::Rgba32F);
    EXPECT_EQ(toColour(colour).data(), colour.data()) << "already colour";
}

// Why: pixelating hides detail by replacing each block with its average. A 4 x 2 picture
// whose left half is 0 and right half 100, pixelated in blocks of 2 from x = 1 over three
// columns: the block at columns 1-2 averages 0 and 100 to 50, the block at column 3 is cut
// off by the rectangle and stays 100, and column 0 is outside and stays 0.
TEST(Layer, PixelatingAveragesBlocksCountedFromTheRectangle) {
    const ImageBuffer image = filled<std::uint8_t>({4, 2}, PixelFormat::Gray8, {0});
    for (int y = 0; y < 2; ++y) {
        reinterpret_cast<std::uint8_t*>(image.row(y))[2] = 100;
        reinterpret_cast<std::uint8_t*>(image.row(y))[3] = 100;
    }
    const ImageBuffer result = pixelate(image, {1, 0, 3, 2}, 2);
    EXPECT_EQ(at<std::uint8_t>(result, 0, 0, 0, 1), 0);
    EXPECT_EQ(at<std::uint8_t>(result, 1, 0, 0, 1), 50);
    EXPECT_EQ(at<std::uint8_t>(result, 2, 1, 0, 1), 50);
    EXPECT_EQ(at<std::uint8_t>(result, 3, 0, 0, 1), 100);
    EXPECT_EQ(at<std::uint8_t>(image, 1, 0, 0, 1), 0) << "the original";

    // A rectangle reaching outside the picture is cut to it.
    const ImageBuffer whole = pixelate(image, {-5, -5, 50, 50}, 100);
    EXPECT_EQ(at<std::uint8_t>(whole, 0, 0, 0, 1), 50);
    EXPECT_EQ(at<std::uint8_t>(whole, 3, 1, 0, 1), 50);
}
