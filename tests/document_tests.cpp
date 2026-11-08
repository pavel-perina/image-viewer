#include <cstdlib>

#include <gtest/gtest.h>

#include "core/document.h"
#include "core/jpeg_source.h"
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

TEST(Document, ScaleFollowsResizing) {
    Document document(turnedJpeg(), 0);
    document.apply(CropEdit{{0, 0, 40, 40}});
    EXPECT_DOUBLE_EQ(document.scale(), 1);
    document.apply(ResizeEdit{{20, 20}});
    EXPECT_DOUBLE_EQ(document.scale(), 0.5);
    document.apply(ResizeEdit{{30, 10}, true});  // the middle 20 x 6.67 to 30 x 10
    EXPECT_DOUBLE_EQ(document.scale(), 0.75);
}
