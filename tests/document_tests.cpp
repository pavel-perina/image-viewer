#include <cstdlib>

#include <gtest/gtest.h>

#include "core/document.h"
#include "core/jpeg_source.h"
#include "core/whole_image_source.h"
#include "test_jpeg.h"

using namespace iv;
using namespace iv::test;

namespace {

// Stored 80 x 40, left half red and right half blue, turned a quarter clockwise by EXIF: upright
// it is 40 x 80 with red on top.
std::shared_ptr<ImageSource> turnedJpeg() {
    JpegOptions options;
    options.width = 80;
    options.height = 40;
    options.orientation = 6;
    options.pixel = [](int x, int) { return x < 40 ? Rgb{230, 20, 20} : Rgb{20, 20, 230}; };
    auto source = openJpegSource(encodeJpeg(options));
    EXPECT_TRUE(source.has_value());
    return source ? *source : nullptr;
}

ImageBuffer pixelsOf(const Document& document) {
    auto pixels = readLevel(*document.source(), 0, 0, {});
    EXPECT_TRUE(pixels.has_value()) << (pixels ? "" : pixels.error());
    return pixels ? *pixels : ImageBuffer{};
}

// Red or blue, by which channel wins; B, G, R are the bytes of Bgrx8.
bool isRed(const ImageBuffer& image, int x, int y) {
    const auto* pixel = reinterpret_cast<const std::uint8_t*>(image.row(y)) + 4 * x;
    return pixel[2] > 150 && pixel[0] < 100;
}

bool isBlue(const ImageBuffer& image, int x, int y) {
    const auto* pixel = reinterpret_cast<const std::uint8_t*>(image.row(y)) + 4 * x;
    return pixel[0] > 150 && pixel[2] < 100;
}

}  // namespace

TEST(Document, WithoutEditsIsTheUprightPicture) {
    const Document document(turnedJpeg(), 0);
    EXPECT_FALSE(document.modified());
    EXPECT_EQ(document.size(), (Size{40, 80}));

    const auto source = document.source();
    const SubimageInfo& info = source->subimages()[0];
    EXPECT_EQ(info.orientation, Orientation::Normal);
    EXPECT_EQ(info.exif.orientation, Orientation::Normal);
    EXPECT_EQ(info.levels.front().size, (Size{40, 80}));

    const ImageBuffer pixels = pixelsOf(document);
    ASSERT_EQ(pixels.size(), (Size{40, 80}));
    EXPECT_TRUE(isRed(pixels, 20, 10));
    EXPECT_TRUE(isBlue(pixels, 20, 70));
}

TEST(Document, CropKeepsTheRectangleOfTheUprightPicture) {
    Document document(turnedJpeg(), 0);
    document.apply(CropEdit{{5, 45, 30, 30}});
    EXPECT_TRUE(document.modified());
    EXPECT_EQ(document.size(), (Size{30, 30}));

    const ImageBuffer pixels = pixelsOf(document);
    ASSERT_EQ(pixels.size(), (Size{30, 30}));
    EXPECT_TRUE(isBlue(pixels, 0, 0));
    EXPECT_TRUE(isBlue(pixels, 29, 29));
}

TEST(Document, CropsApplyOneAfterAnother) {
    Document document(turnedJpeg(), 0);
    document.apply(CropEdit{{0, 20, 40, 40}});  // red above blue, 20 of each
    document.apply(CropEdit{{10, 25, 10, 10}});  // inside the blue part
    EXPECT_EQ(document.size(), (Size{10, 10}));
    const ImageBuffer pixels = pixelsOf(document);
    EXPECT_TRUE(isBlue(pixels, 0, 0));
    EXPECT_TRUE(isBlue(pixels, 9, 9));
}

TEST(Document, UndoAndRedoMoveThroughTheEdits) {
    Document document(turnedJpeg(), 0);
    EXPECT_FALSE(document.canUndo());
    document.apply(CropEdit{{0, 0, 40, 40}});
    document.apply(CropEdit{{0, 0, 20, 20}});
    EXPECT_EQ(document.size(), (Size{20, 20}));

    document.undo();
    EXPECT_EQ(document.size(), (Size{40, 40}));
    EXPECT_TRUE(document.canRedo());
    document.undo();
    EXPECT_EQ(document.size(), (Size{40, 80}));
    EXPECT_FALSE(document.modified());
    EXPECT_FALSE(document.canUndo());

    document.redo();
    EXPECT_EQ(document.size(), (Size{40, 40}));
    EXPECT_EQ(pixelsOf(document).size(), (Size{40, 40}));

    // A new edit drops what was undone.
    document.apply(CropEdit{{0, 0, 10, 10}});
    EXPECT_FALSE(document.canRedo());
    EXPECT_EQ(document.edits().size(), 2u);
}

TEST(Document, KnowsWhetherItDiffersFromWhatWasSaved) {
    Document document(turnedJpeg(), 0);
    document.apply(CropEdit{{0, 0, 40, 40}});
    document.markSaved();
    EXPECT_FALSE(document.modified());
    document.undo();
    EXPECT_TRUE(document.modified());
    document.redo();
    EXPECT_FALSE(document.modified());

    // Undoing past the save and editing something else leaves no way back to it.
    document.undo();
    document.apply(CropEdit{{0, 0, 20, 20}});
    EXPECT_TRUE(document.modified());
    document.undo();
    EXPECT_TRUE(document.modified());
}

TEST(Document, SourcesHandedOutKeepTheirEdits) {
    Document document(turnedJpeg(), 0);
    document.apply(CropEdit{{0, 0, 40, 40}});
    const auto before = document.source();
    document.undo();
    EXPECT_EQ(before->subimages()[0].levels.front().size, (Size{40, 40}));
    auto pixels = readLevel(*before, 0, 0, {});
    ASSERT_TRUE(pixels.has_value());
    EXPECT_EQ(pixels->size(), (Size{40, 40}));
}

TEST(Document, ResizeScalesToExactlyTheSize) {
    Document document(turnedJpeg(), 0);
    document.apply(ResizeEdit{{20, 40}});
    EXPECT_EQ(document.size(), (Size{20, 40}));
    const ImageBuffer pixels = pixelsOf(document);
    ASSERT_EQ(pixels.size(), (Size{20, 40}));
    EXPECT_TRUE(isRed(pixels, 10, 5));
    EXPECT_TRUE(isBlue(pixels, 10, 35));
    document.undo();
    EXPECT_EQ(document.size(), (Size{40, 80}));
}

TEST(Document, ResizeToAnotherRatioStretchesOrCropsTheMiddle) {
    // Red 40 rows above blue 20, to a strip of 40 x 12.
    Document stretched(turnedJpeg(), 0);
    stretched.apply(CropEdit{{0, 0, 40, 60}});
    stretched.apply(ResizeEdit{{40, 12}});
    const ImageBuffer squeezed = pixelsOf(stretched);
    ASSERT_EQ(squeezed.size(), (Size{40, 12}));
    EXPECT_TRUE(isRed(squeezed, 20, 1));
    EXPECT_TRUE(isBlue(squeezed, 20, 11)) << "all of it, squeezed";

    Document filled(turnedJpeg(), 0);
    filled.apply(CropEdit{{0, 0, 40, 60}});
    filled.apply(ResizeEdit{{40, 12}, true});
    const ImageBuffer middle = pixelsOf(filled);
    ASSERT_EQ(middle.size(), (Size{40, 12}));
    EXPECT_TRUE(isRed(middle, 20, 1));
    EXPECT_TRUE(isRed(middle, 20, 11)) << "only rows 24 to 36, which are red";
}

TEST(Document, ResizeHasToStaySavable) {
    const Document document(turnedJpeg(), 0);
    EXPECT_TRUE(document.canResizeTo({80, 160}));
    EXPECT_FALSE(document.canResizeTo({0, 10}));
    EXPECT_FALSE(document.canResizeTo({100000, 100000}));
}

TEST(Document, TiltTurnsAndKeepsTheLargestRectangle) {
    Document document(turnedJpeg(), 0);
    const TiltEdit edit{{8, 0, 0}, focalPixels({40, 80}, 28), true};
    document.apply(edit);
    const Size expected = planTilt({40, 80}, edit.focalPixels, edit.tilt, true).size;
    EXPECT_EQ(document.size(), expected);
    EXPECT_LT(expected.height, 80);
    const ImageBuffer pixels = pixelsOf(document);
    ASSERT_EQ(pixels.size(), expected);
    EXPECT_TRUE(isRed(pixels, expected.width / 2, 4));
    EXPECT_TRUE(isBlue(pixels, expected.width / 2, expected.height - 5));
}

TEST(Document, GrayKeepsTheSizeAndUndoBringsTheColourBack) {
    Document document(turnedJpeg(), 0);
    document.apply(GrayEdit{GrayMethod::Lightness});
    EXPECT_EQ(document.size(), (Size{40, 80}));
    const ImageBuffer pixels = pixelsOf(document);
    ASSERT_EQ(pixels.size(), (Size{40, 80}));
    const auto* p = reinterpret_cast<const std::uint8_t*>(pixels.row(4)) + 20 * 4;
    EXPECT_EQ(p[0], p[1]);
    EXPECT_EQ(p[1], p[2]);
    document.undo();
    EXPECT_TRUE(isRed(pixelsOf(document), 20, 4));
}

TEST(Document, ColourEditsApplyAndUndo) {
    Document document(turnedJpeg(), 0);
    ColorEdit edit;
    edit.chroma = 0;
    document.apply(edit);
    EXPECT_EQ(document.size(), (Size{40, 80}));
    const ImageBuffer pixels = pixelsOf(document);
    const auto* p = reinterpret_cast<const std::uint8_t*>(pixels.row(4)) + 20 * 4;
    EXPECT_NEAR(p[0], p[2], 1);
    document.undo();
    EXPECT_TRUE(isRed(pixelsOf(document), 20, 4));
}

TEST(Document, ScaleFollowsResizing) {
    Document document(turnedJpeg(), 0);
    document.apply(CropEdit{{0, 0, 40, 40}});
    EXPECT_DOUBLE_EQ(document.scale(), 1);
    document.apply(ResizeEdit{{20, 20}});
    EXPECT_DOUBLE_EQ(document.scale(), 0.5);
    document.apply(ResizeEdit{{30, 10}, true});  // the middle 20 x 6.67 to 30 x 10
    EXPECT_DOUBLE_EQ(document.scale(), 0.75);
}

// Why: applied annotations are one edit like any other: they change the pixels where the
// layer is, keep the size, and undo. Here a 10 x 10 opaque green layer is put at 5, 5 of the
// picture that is red on top, and a rectangle over the border of red and blue is pixelated
// into one block, which mixes the two.
TEST(Document, DrawingPutsALayerOnAndUndoes) {
    Document document(turnedJpeg(), 0);
    DrawEdit edit;
    edit.drawing = ImageBuffer::create({10, 10}, PixelFormat::Bgrx8);
    for (int y = 0; y < 10; ++y) {
        auto* out = reinterpret_cast<std::uint8_t*>(edit.drawing.row(y));
        for (int x = 0; x < 10; ++x, out += 4) {
            out[0] = 0, out[1] = 255, out[2] = 0, out[3] = 255;
        }
    }
    edit.x = 5;
    edit.y = 5;
    edit.range = {0, 255};
    edit.pixelated = {{{0, 30, 40, 20}, 100}};
    document.apply(edit);
    EXPECT_TRUE(document.modified());
    EXPECT_EQ(document.size(), (Size{40, 80}));

    const ImageBuffer pixels = pixelsOf(document);
    const auto* green = reinterpret_cast<const std::uint8_t*>(pixels.row(10)) + 4 * 10;
    EXPECT_EQ(green[1], 255);
    EXPECT_EQ(green[2], 0);
    EXPECT_TRUE(isRed(pixels, 30, 10)) << "beside the layer";
    EXPECT_FALSE(isRed(pixels, 20, 32));
    EXPECT_FALSE(isBlue(pixels, 20, 48)) << "red and blue averaged";
    EXPECT_TRUE(isBlue(pixels, 20, 60));

    document.undo();
    EXPECT_TRUE(isRed(pixelsOf(document), 10, 10));
}

// Why: a red arrow on a gray picture should stay red, so drawing in colour turns the picture
// into a colour one. The source then has to say so too: its format, its colour model, and no
// gray colour profile, which a colour picture cannot be read with. The gray source here is
// made by hand: 4 x 4 pixels of 50 with a stand-in profile that only claims to be gray.
TEST(Document, DrawingInColourTurnsAGrayPictureIntoAColourOne) {
    SubimageInfo info;
    info.format = PixelFormat::Gray8;
    info.levels.push_back({{4, 4}, {4, 4}});
    info.stored.colorModel = "Grayscale";
    info.iccProfile.assign(128, 0);
    std::copy_n("GRAY", 4, info.iccProfile.begin() + 16);
    const auto gray = makeWholeImageSource(info, {4, 4}, [](const std::stop_token&) -> Result<ImageBuffer> {
        const ImageBuffer pixels = ImageBuffer::create({4, 4}, PixelFormat::Gray8);
        for (int y = 0; y < 4; ++y) {
            std::fill_n(reinterpret_cast<std::uint8_t*>(pixels.row(y)), 4, std::uint8_t{50});
        }
        return pixels;
    });
    Document document(gray, 0);
    DrawEdit edit;
    edit.drawing = ImageBuffer::create({1, 1}, PixelFormat::Bgrx8);
    const std::uint8_t red[4] = {0, 0, 255, 255};
    std::copy_n(red, 4, reinterpret_cast<std::uint8_t*>(edit.drawing.row(0)));
    edit.range = {0, 255};

    // Not in colour: the picture stays gray and takes the luma of red, 76.
    document.apply(edit);
    EXPECT_EQ(document.source()->subimages()[0].format, PixelFormat::Gray8);
    EXPECT_EQ(reinterpret_cast<const std::uint8_t*>(pixelsOf(document).row(0))[0], 76);
    EXPECT_FALSE(document.source()->subimages()[0].iccProfile.empty());
    document.undo();

    edit.colour = true;
    document.apply(edit);
    const auto source = document.source();
    const SubimageInfo& after = source->subimages()[0];
    EXPECT_EQ(after.format, PixelFormat::Bgrx8);
    EXPECT_EQ(after.stored.colorModel, "RGB");
    EXPECT_TRUE(after.iccProfile.empty());
    const ImageBuffer pixels = pixelsOf(document);
    ASSERT_EQ(pixels.format(), PixelFormat::Bgrx8);
    const auto* first = reinterpret_cast<const std::uint8_t*>(pixels.row(0));
    EXPECT_EQ(first[2], 255);
    EXPECT_EQ(first[0], 0);
    EXPECT_EQ(first[4], 50) << "the next pixel, gray as before";
    EXPECT_EQ(first[6], 50);

    document.undo();
    EXPECT_EQ(document.source()->subimages()[0].format, PixelFormat::Gray8);
}

namespace {

// A made-up source of one colour or gray, opaque, whose fourth channel holds 0 so that taking
// it for alpha would show.
std::shared_ptr<ImageSource> plainSource(Size size, PixelFormat format, std::uint8_t value) {
    SubimageInfo info;
    info.format = format;
    return makeWholeImageSource(info, size, [size, format, value](const std::stop_token&) -> Result<ImageBuffer> {
        const ImageBuffer image = ImageBuffer::create(size, format);
        for (int y = 0; y < size.height; ++y) {
            auto* p = reinterpret_cast<std::uint8_t*>(image.row(y));
            if (format == PixelFormat::Gray8) {
                std::fill_n(p, size.width, value);
            } else {
                for (int x = 0; x < size.width; ++x, p += 4) {
                    p[0] = value, p[1] = 20, p[2] = 10, p[3] = 0;
                }
            }
        }
        return image;
    });
}

}  // namespace

// Why: straightening without its crop keeps the whole turned picture, so nothing of it is lost,
// and the corners it uncovers are transparent rather than a made-up colour. Input: an opaque
// picture of one colour, 40 x 20, without alpha, turned by 90 and by 30 degrees, and a gray one.
// The truth: turned 90 degrees it is 20 x 40, as a quarter turn gives; turned 30 degrees it has
// alpha, a corner is transparent, the middle opaque and of the colour it had; a gray picture
// becomes a colour one with alpha, of the same gray; with the crop, as before, there is no alpha.
TEST(Document, TiltWithoutCropKeepsTheWholePictureWithClearCorners) {
    const auto colour = plainSource({40, 20}, PixelFormat::Bgrx8, 200);
    {
        Document document(colour, 0);
        document.apply(TiltEdit{{.roll = 90}, 100, true, false});
        EXPECT_EQ(document.size(), (Size{20, 40}));
    }
    Document document(colour, 0);
    document.apply(TiltEdit{{.roll = 30}, 100, true, false});
    const auto edited = document.source();
    EXPECT_TRUE(edited->subimages()[0].alpha);
    const ImageBuffer turned = pixelsOf(document);
    ASSERT_TRUE(turned.hasAlpha());
    EXPECT_EQ(turned.size(), document.size());
    EXPECT_GT(turned.width(), 40) << "the whole turned picture is wider than the picture";
    const auto* corner = reinterpret_cast<const std::uint8_t*>(turned.row(0));
    EXPECT_EQ(corner[3], 0);
    const auto* middle = reinterpret_cast<const std::uint8_t*>(turned.row(turned.height() / 2)) + turned.width() / 2 * 4;
    EXPECT_EQ(middle[3], 255);
    EXPECT_NEAR(middle[0], 200, 1);
    EXPECT_NEAR(middle[2], 10, 1);

    Document gray(plainSource({40, 20}, PixelFormat::Gray8, 90), 0);
    gray.apply(TiltEdit{{.roll = 30}, 100, true, false});
    EXPECT_EQ(gray.source()->subimages()[0].format, PixelFormat::Bgrx8);
    const ImageBuffer grayTurned = pixelsOf(gray);
    ASSERT_EQ(grayTurned.format(), PixelFormat::Bgrx8);
    ASSERT_TRUE(grayTurned.hasAlpha());
    const auto* grayMiddle = reinterpret_cast<const std::uint8_t*>(grayTurned.row(grayTurned.height() / 2)) + grayTurned.width() / 2 * 4;
    EXPECT_NEAR(grayMiddle[1], 90, 1);
    EXPECT_EQ(grayMiddle[3], 255);

    Document cropped(colour, 0);
    cropped.apply(TiltEdit{{.roll = 30}, 100, true});
    EXPECT_FALSE(pixelsOf(cropped).hasAlpha());
    EXPECT_LT(cropped.size().width, 40);
}

// Why: without the crop, the whole turned picture grows as a strong perspective brings an edge
// towards the horizon, and past it there is no whole picture; its corners would project onto
// the far side, which a bounding box takes for a small picture. Input: 400 x 300 at a 28 mm
// focal length, pitched 40, 64 and 70 degrees. The truth, as measured once and checked by eye
// against the geometry: 40 is somewhat larger than the picture, 64 several times larger, and
// at 70 the top edge lies beyond the horizon (at about 65 degrees for this lens), so 0 x 0.
TEST(Document, AWholeTurnedPictureGrowsTowardsTheHorizonAndHasNoneBeyondIt) {
    const Size image{400, 300};
    const double focal = focalPixels(image, 28);
    const Size mild = planTilt(image, focal, {0, 40, 0}, true, false).size;
    const Size strong = planTilt(image, focal, {0, 64, 0}, true, false).size;
    EXPECT_GT(mild.width, 400);
    EXPECT_GT(strong.width, 4 * mild.width);
    EXPECT_EQ(planTilt(image, focal, {0, 70, 0}, true, false).size, (Size{0, 0}));
}
