#include "ui_test_support.h"

#include "core/image_source.h"
#include "ui/edited_image.h"
#include "ui/strip_builder.h"

using iv::StripBuilder;

namespace {

// A JPEG of one colour, of a size as stored, with an EXIF orientation if not 0.
QString writePicture(const QTemporaryDir& dir, const QString& name, int width, int height, Rgb colour, int orientation = 0) {
    const QString path = dir.filePath(name);
    JpegOptions options;
    options.width = width;
    options.height = height;
    options.orientation = orientation;
    options.pixel = [colour](int, int) { return colour; };
    writeJpeg(path, options);
    return path;
}

bool sizesRead(const StripBuilder& builder) {
    return waitUntil([&] { return !builder.reading(); });
}

// The strip's pixels, made as the save job would make them.
iv::ImageBuffer stripPixels(const StripBuilder& builder) {
    const auto source = builder.strip().value<std::shared_ptr<iv::ImageSource>>();
    if (!source) {
        ADD_FAILURE() << "no strip: " << builder.problem().toStdString();
        return {};
    }
    auto pixels = iv::readLevel(*source, 0, 0, {});
    if (!pixels) {
        ADD_FAILURE() << pixels.error();
        return {};
    }
    return *pixels;
}

std::array<int, 4> rgbaAt(const iv::ImageBuffer& image, int x, int y) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(image.row(y)) + x * 4;
    return {p[2], p[1], p[0], p[3]};
}

void expectNearRgb(const std::array<int, 4>& actual, Rgb expected, const char* where) {
    EXPECT_NEAR(actual[0], expected.r, 6) << where;
    EXPECT_NEAR(actual[1], expected.g, 6) << where;
    EXPECT_NEAR(actual[2], expected.b, 6) << where;
}

constexpr Rgb kRed{220, 30, 30};
constexpr Rgb kBlue{30, 30, 220};

}  // namespace

// Why: the dialog shows the size of the strip before anything is decoded, from the pictures'
// sizes as shown. Input: a 400 x 200 picture and one stored 200 x 100 but turned upright to
// 100 x 200, across at a height of 100, then with spacing 10. The truth: widths 200 and 50,
// so 250 x 100, then 260 x 100; with no problem.
TEST(StripBuilder, TheResultSizeFollowsThePicturesAsShownAndTheOptions) {
    QTemporaryDir dir;
    StripBuilder builder;
    builder.setExtent(100);
    builder.setPaths({writePicture(dir, "wide.jpg", 400, 200, kRed), writePicture(dir, "turned.jpg", 200, 100, kBlue, 6)});
    ASSERT_TRUE(sizesRead(builder));
    EXPECT_EQ(builder.resultSize(), QSize(250, 100));
    EXPECT_TRUE(builder.problem().isEmpty()) << builder.problem().toStdString();
    builder.setSpacing(10);
    EXPECT_EQ(builder.resultSize(), QSize(260, 100));
}

// Why: pictures are put in order by dragging, and taken out, in the dialog's list. Input:
// three pictures a, b, c; a moved to the end, then back to the start; then two removed. The
// truth: b, c, a; a, b, c; and with one picture left, the strip cannot be made.
TEST(StripBuilder, PicturesAreReorderedAndRemoved) {
    QTemporaryDir dir;
    const QString a = writePicture(dir, "a.jpg", 40, 40, kRed);
    const QString b = writePicture(dir, "b.jpg", 40, 40, kRed);
    const QString c = writePicture(dir, "c.jpg", 40, 40, kRed);
    StripBuilder builder;
    builder.setPaths({a, b, c});
    ASSERT_TRUE(sizesRead(builder));
    builder.move(0, 2);
    EXPECT_EQ(builder.paths(), (QStringList{b, c, a}));
    builder.move(2, 0);
    EXPECT_EQ(builder.paths(), (QStringList{a, b, c}));
    EXPECT_EQ(builder.data(builder.index(1), StripBuilder::NameRole).toString(), QStringLiteral("b.jpg"));
    builder.remove(0);
    builder.remove(0);
    EXPECT_EQ(builder.count(), 1);
    EXPECT_FALSE(builder.problem().isEmpty());
    EXPECT_FALSE(builder.strip().isValid());
}

// Why: a file that cannot be read cannot go in the strip, and the dialog says which it is;
// and a sane limit keeps a huge selection from being taken whole. Input: a text file named
// .jpg beside a picture; then 60 paths. The truth: the problem names the file; 50 are kept.
TEST(StripBuilder, AnUnreadableFileIsNamedAndTheCountIsLimited) {
    QTemporaryDir dir;
    const QString bad = dir.filePath(QStringLiteral("notes.jpg"));
    QFile file(bad);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("not a picture");
    file.close();
    StripBuilder builder;
    builder.setPaths({writePicture(dir, "good.jpg", 40, 40, kRed), bad});
    ASSERT_TRUE(sizesRead(builder));
    EXPECT_TRUE(builder.problem().contains(QStringLiteral("notes.jpg"))) << builder.problem().toStdString();

    QStringList many;
    for (int i = 0; i < 60; ++i) {
        many << dir.filePath(QStringLiteral("missing%1.jpg").arg(i));
    }
    builder.setPaths(many);
    EXPECT_EQ(builder.count(), StripBuilder::kMaxPictures);
}

// Why: what is saved is the pictures in order, where the layout puts them, on the background.
// Input: a red and a blue 100 x 100 picture across at a height of 50, spacing 4, on white;
// then on transparency. The truth: 104 x 50; red in the first, white in the gap, blue in the
// second; on transparency the gap is clear and the pictures opaque.
TEST(StripBuilder, TheStripIsThePicturesInOrderOnTheBackground) {
    QTemporaryDir dir;
    StripBuilder builder;
    builder.setExtent(50);
    builder.setSpacing(4);
    builder.setPaths({writePicture(dir, "red.jpg", 100, 100, kRed), writePicture(dir, "blue.jpg", 100, 100, kBlue)});
    ASSERT_TRUE(sizesRead(builder));
    const iv::ImageBuffer white = stripPixels(builder);
    ASSERT_FALSE(white.isNull());
    EXPECT_EQ(white.size(), (iv::Size{104, 50}));
    EXPECT_FALSE(white.hasAlpha());
    expectNearRgb(rgbaAt(white, 25, 25), kRed, "first");
    expectNearRgb(rgbaAt(white, 52, 25), Rgb{255, 255, 255}, "gap");
    expectNearRgb(rgbaAt(white, 79, 25), kBlue, "second");

    builder.setTransparent(true);
    const iv::ImageBuffer clear = stripPixels(builder);
    ASSERT_FALSE(clear.isNull());
    EXPECT_TRUE(clear.hasAlpha());
    EXPECT_EQ(rgbaAt(clear, 52, 25)[3], 0);
    EXPECT_EQ(rgbaAt(clear, 25, 25)[3], 255);
}

// Why: the strip is saved through Save As, which takes it as an edited image, and is named by
// the time unless the user changes it. Input: the strip of two pictures saved as PNG by
// saveImageAs, as the save job does. The truth: the file is written and opens at the strip's
// size; the suggested name is a time stamp with .webp.
TEST(StripBuilder, IsSavedThroughSaveAsUnderANameFromTheTime) {
    QTemporaryDir dir;
    StripBuilder builder;
    builder.setExtent(50);
    builder.setPaths({writePicture(dir, "red.jpg", 100, 100, kRed), writePicture(dir, "blue.jpg", 100, 50, kBlue)});
    ASSERT_TRUE(sizesRead(builder));
    const QString target = dir.filePath(QStringLiteral("strip.png"));
    const auto edited = builder.strip().value<std::shared_ptr<iv::ImageSource>>();
    ASSERT_TRUE(edited);
    const iv::FileJobResult result = iv::saveImageAs(QStringLiteral("strip"), 0, target, {}, {}, edited);
    EXPECT_TRUE(result.problems.isEmpty()) << result.problems.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(QImage(target).size(), QSize(150, 50));
    EXPECT_TRUE(QRegularExpression(QStringLiteral("^\\d{4}-\\d\\d-\\d\\d_\\d{6}\\.webp$")).match(builder.suggestedName()).hasMatch())
        << builder.suggestedName().toStdString();
}
