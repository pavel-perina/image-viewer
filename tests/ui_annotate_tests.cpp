// Edit > Annotate in the large view: drawing shapes on the picture with the mouse, changing
// them before they are applied, and what applying leaves in the pixels. The picture is 400 by
// 300, shown at its own size, light (about 220) around a dark (about 30) rectangle in the
// middle; it is a JPEG, so its values are near those and not exact. Pixels are read back
// through the probe, which gives the edited picture as it would be saved.

#include <array>

#include "ui_test_support.h"

#include "ui/annotate_overlay.h"
#include "ui/overlay_style.h"
#include "ui/shape_painter.h"

using namespace iv;
using namespace iv::test;

namespace {

using Tool = AnnotateOverlay::Tool;

struct AnnotatePage : ToolPage {
    AnnotateOverlay* overlay = nullptr;

    AnnotatePage() : ToolPage("startAnnotate", false) {
        overlay = qobject_cast<AnnotateOverlay*>(page->findChild<QQuickItem*>(QStringLiteral("annotateOverlay")));
        EXPECT_NE(overlay, nullptr);
        EXPECT_TRUE(page->property("annotating").toBool());
    }

    // A tool with a style of its own for the test, whatever tests before it left remembered.
    void choose(Tool tool, const QColor& colour, double width) {
        overlay->setTool(tool);
        overlay->setColour(colour);
        overlay->setLineWidth(width);
        overlay->setFilled(false);
        overlay->setOutline(false);
        overlay->setShadow(false);
    }

    void apply() {
        QMetaObject::invokeMethod(page, "applyAnnotate");
        EXPECT_TRUE(settled(image));
    }
};

int channel(const QString& hex, int index) {
    return hex.mid(1 + 2 * index, 2).toInt(nullptr, 16);
}

}  // namespace

// Why: the whole path once. An arrow dragged across the light part is one shape, selected;
// applying burns it in as an edit, in the colour chosen, and undo takes it out again. The
// arrow is 6 wide along y = 20, so the pixel at 40, 20 is in the middle of its shaft.
TEST(Annotate, AnArrowDrawnIsBurnedInAndUndoes) {
    AnnotatePage f;
    f.choose(Tool::Arrow, QColor(0x10, 0xa0, 0xf0), 6);
    const QString before = f.hexAt(40, 20);

    f.dragOnImage({20, 20}, {120, 20});
    ASSERT_EQ(f.overlay->count(), 1);
    EXPECT_EQ(f.overlay->selected(), 0);
    EXPECT_EQ(f.overlay->shapes().front().kind, ShapeKind::Arrow);
    EXPECT_FALSE(f.image->modified()) << "nothing is in the picture until applied";

    f.apply();
    EXPECT_FALSE(f.page->property("annotating").toBool());
    EXPECT_EQ(f.overlay->count(), 0);
    EXPECT_TRUE(f.image->modified());
    EXPECT_EQ(f.hexAt(40, 20), QStringLiteral("#10a0f0"));
    EXPECT_EQ(f.hexAt(40, 40), f.hexAt(41, 40)) << "beside the arrow";

    f.image->undo();
    ASSERT_TRUE(settled(f.image));
    EXPECT_EQ(f.hexAt(40, 20), before);
}

// Why: shapes can be corrected before they are applied. A click on a shape selects it and a
// click on nothing deselects; with Select a drag moves it; Delete removes the one selected;
// and leaving without applying changes nothing in the picture.
TEST(Annotate, ShapesAreSelectedMovedAndRemovedBeforeApplying) {
    AnnotatePage f;
    f.choose(Tool::Line, Qt::red, 4);
    f.dragOnImage({20, 20}, {120, 20});
    f.dragOnImage({20, 60}, {120, 60});
    ASSERT_EQ(f.overlay->count(), 2);
    EXPECT_EQ(f.overlay->selected(), 1);

    QTest::mouseClick(&f.view, Qt::LeftButton, Qt::NoModifier, f.at(200, 250));
    EXPECT_EQ(f.overlay->selected(), -1);
    QTest::mouseClick(&f.view, Qt::LeftButton, Qt::NoModifier, f.at(70, 20));
    EXPECT_EQ(f.overlay->selected(), 0);

    // Dragging an end moves that end alone.
    f.dragOnImage({120, 20}, {150, 40});
    EXPECT_EQ(f.overlay->shapes()[0].points, (std::vector<PointD>{{20, 20}, {150, 40}}));

    // With Select, dragging the second line by its middle moves all of it, 10 right and 5 down.
    f.overlay->setTool(Tool::Select);
    f.dragOnImage({70, 60}, {80, 65});
    EXPECT_EQ(f.overlay->selected(), 1);
    EXPECT_EQ(f.overlay->shapes()[1].points, (std::vector<PointD>{{30, 65}, {130, 65}}));
    ASSERT_EQ(f.overlay->count(), 2) << "a drag with Select draws nothing";

    QTest::keyClick(&f.view, Qt::Key_Delete);
    ASSERT_EQ(f.overlay->count(), 1);
    EXPECT_EQ(f.overlay->selected(), -1);

    QMetaObject::invokeMethod(f.page, "cancelAnnotate");
    EXPECT_EQ(f.overlay->count(), 0);
    EXPECT_FALSE(f.image->modified());
}

// Why: a rectangle between pixels has blurred edges. Its corners keep to whole pixels however
// the mouse lands, and its line is drawn inside them: with a line 3 wide from 20, 20 to 80, 60
// the pixels at 20 and 22 are fully the colour, and 19 and 23 are untouched.
TEST(Annotate, ARectangleKeepsToWholePixelsAndItsEdgesAreSharp) {
    AnnotatePage f;
    f.choose(Tool::Rectangle, QColor(0x10, 0xa0, 0xf0), 3);
    const QString ground = f.hexAt(19, 40);
    const QString inside = f.hexAt(23, 40);

    f.dragOnImage({20.4, 19.7}, {79.6, 60.3});
    ASSERT_EQ(f.overlay->count(), 1);
    EXPECT_EQ(f.overlay->shapes().front().points, (std::vector<PointD>{{20, 20}, {80, 60}}));

    f.apply();
    EXPECT_EQ(f.hexAt(19, 40), ground);
    EXPECT_EQ(f.hexAt(20, 40), QStringLiteral("#10a0f0"));
    EXPECT_EQ(f.hexAt(22, 40), QStringLiteral("#10a0f0"));
    EXPECT_EQ(f.hexAt(23, 40), inside);
    EXPECT_EQ(f.hexAt(79, 40), QStringLiteral("#10a0f0")) << "the right edge, inside the corner at 80";
    EXPECT_EQ(f.hexAt(80, 40), f.hexAt(81, 40));
}

// Why: a marker must mark without hiding, and a pixelated box must hide. On the light ground
// a yellow marker leaves red and green and takes the blue away; a box over the border of dark
// and light, pixelated as one block, becomes one colour between the two.
TEST(Annotate, AMarkerDarkensAndABoxPixelates) {
    AnnotatePage f;
    f.choose(Tool::Marker, QColor(255, 255, 0), 1);
    f.dragOnImage({10, 10}, {60, 30});
    f.choose(Tool::Pixelate, Qt::black, 500);
    // Across the top of the picture, well above the dark rectangle and into it.
    f.dragOnImage({190, 10}, {210, 110});
    ASSERT_EQ(f.overlay->count(), 2);
    const QString light = f.hexAt(30, 20);
    ASSERT_GT(channel(light, 2), 180);

    f.apply();
    const QString marked = f.hexAt(30, 20);
    EXPECT_NEAR(channel(marked, 0), channel(light, 0), 1);
    EXPECT_NEAR(channel(marked, 1), channel(light, 1), 1);
    EXPECT_EQ(channel(marked, 2), 0);

    const QString top = f.hexAt(200, 12);
    EXPECT_EQ(top, f.hexAt(200, 108)) << "one block";
    EXPECT_GT(channel(top, 0), 60);
    EXPECT_LT(channel(top, 0), 190);
}

// Why: what a marker or a pixelated box gives depends on the picture under it, so while
// drawing they are shown from the real pixels. The preview of the part of the picture has to
// be what applying then leaves there. Compared at one pixel inside each.
TEST(Annotate, AMarkerAndABoxAreShownAsTheyWillBe) {
    AnnotatePage f;
    f.choose(Tool::Marker, QColor(255, 255, 0), 1);
    f.dragOnImage({10, 10}, {60, 30});
    f.choose(Tool::Pixelate, Qt::black, 500);
    f.dragOnImage({190, 10}, {210, 110});
    ASSERT_EQ(f.overlay->count(), 2);

    QRect markerPlace;
    QRect boxPlace;
    // The picture is read in the background when the tool starts.
    ASSERT_TRUE(waitUntil([&] { return !f.image->drawPreview(f.overlay->shapes()[0], &markerPlace).isNull(); }));
    const QImage marker = f.image->drawPreview(f.overlay->shapes()[0], &markerPlace);
    const QImage box = f.image->drawPreview(f.overlay->shapes()[1], &boxPlace);
    ASSERT_FALSE(marker.isNull());
    ASSERT_FALSE(box.isNull());
    const QColor markerShown = marker.pixelColor(QPoint(30, 20) - markerPlace.topLeft());
    const QColor boxShown = box.pixelColor(QPoint(200, 50) - boxPlace.topLeft());

    f.apply();
    EXPECT_EQ(markerShown.name(), f.hexAt(30, 20));
    EXPECT_EQ(boxShown.name(), f.hexAt(200, 50));
}

// Why: each tool keeps the style it was last used with, also for the next picture and the
// next run, and separately from the others: the marker stays yellow while arrows stay red.
// A second page stands in for another run, as both read the same stored settings.
TEST(Annotate, EachToolRemembersItsOwnStyle) {
    {
        AnnotatePage f;
        f.choose(Tool::Arrow, QColor(0x12, 0x34, 0x56), 9);
        f.overlay->setShadow(true);
        f.choose(Tool::Marker, QColor(0xff, 0xee, 0x00), 1);
    }
    AnnotatePage f;
    f.overlay->setTool(Tool::Arrow);
    EXPECT_EQ(f.overlay->colour(), QColor(0x12, 0x34, 0x56));
    EXPECT_EQ(f.overlay->lineWidth(), 9);
    EXPECT_TRUE(f.overlay->shadow());
    f.overlay->setTool(Tool::Marker);
    EXPECT_EQ(f.overlay->colour(), QColor(0xff, 0xee, 0x00));
    EXPECT_FALSE(f.overlay->shadow()) << "the arrow's shadow is not the marker's";

    // The style shown follows the shape selected, and changing it changes that shape.
    f.overlay->setTool(Tool::Arrow);
    f.dragOnImage({20, 20}, {120, 20});
    ASSERT_EQ(f.overlay->selected(), 0);
    f.overlay->setColour(Qt::green);
    EXPECT_EQ(f.overlay->shapes().front().style.colour, (ShapeColour{0, 255, 0, 255}));
    QMetaObject::invokeMethod(f.page, "cancelAnnotate");
}

// Why: Esc first lets go of the shape selected, and only then leaves the tool, so that a
// slip of the key does not throw away what was drawn. Through the window's own Esc.
TEST(Annotate, EscapeDeselectsBeforeItLeaves) {
    AnnotatePage f;
    f.choose(Tool::Line, Qt::red, 4);
    f.dragOnImage({20, 20}, {120, 20});
    ASSERT_EQ(f.overlay->selected(), 0);
    QMetaObject::invokeMethod(f.page, "escapeAnnotate");
    EXPECT_EQ(f.overlay->selected(), -1);
    EXPECT_EQ(f.overlay->count(), 1);
    EXPECT_TRUE(f.page->property("annotating").toBool());
    QMetaObject::invokeMethod(f.page, "escapeAnnotate");
    EXPECT_FALSE(f.page->property("annotating").toBool());
    EXPECT_EQ(f.overlay->count(), 0);
}

// Why: text is a box whose words come from the panel. Dragging makes the box, on whole
// pixels, and asks for the words; a click with the Text tool on nothing places a box of a
// size to start in. Words set while it is selected are its text, and are burned in with the
// rest: after applying, the picture is edited and some pixel in the box has the text's colour.
// The letters depend on the typeface, so the test looks for the colour along a row through
// the middle of a line of large capital letters rather than at fixed pixels.
TEST(Annotate, TextIsABoxWhoseWordsAreSetAndBurnedIn) {
    AnnotatePage f;
    f.choose(Tool::Text, QColor(0x10, 0xa0, 0xf0), 1);
    f.overlay->setTextSize(40);
    QSignalSpy wanted(f.overlay, &AnnotateOverlay::textWanted);

    f.dragOnImage({10.3, 10.2}, {150.4, 60.1});
    ASSERT_EQ(f.overlay->count(), 1);
    EXPECT_EQ(f.overlay->shapes()[0].kind, ShapeKind::Text);
    EXPECT_EQ(f.overlay->shapes()[0].points, (std::vector<PointD>{{10, 10}, {150, 60}}));
    EXPECT_EQ(wanted.count(), 1);
    f.overlay->setText(QStringLiteral("HHHH"));
    EXPECT_EQ(f.overlay->shapes()[0].text, "HHHH");

    // A click elsewhere places a second box, ten letters wide; nothing is typed into it.
    QTest::mouseClick(&f.view, Qt::LeftButton, Qt::NoModifier, f.at(20, 250));
    ASSERT_EQ(f.overlay->count(), 2);
    EXPECT_EQ(f.overlay->selected(), 1);
    EXPECT_EQ(f.overlay->shapes()[1].points, (std::vector<PointD>{{20, 250}, {420, 310}}));
    EXPECT_EQ(wanted.count(), 2);
    EXPECT_TRUE(f.overlay->text().isEmpty()) << "the text shown is that of the box selected";

    // As one edit: the layer holds the letters and nothing for the empty box.
    const DrawEdit edit = renderShapes(f.overlay->shapes(), {400, 300}, {0, 255});
    ASSERT_FALSE(edit.drawing.isNull());
    EXPECT_LT(edit.y + edit.drawing.height(), 200) << "the empty box at 250 draws nothing";
    int coloured = 0;
    const int row = 30 - edit.y;  // through the middle of the letters, which stand from 10 to about 50
    const auto* pixel = reinterpret_cast<const std::uint8_t*>(edit.drawing.row(row));
    for (int x = 0; x < edit.drawing.width(); ++x, pixel += 4) {
        coloured += pixel[3] == 255 && pixel[2] == 0x10 && pixel[1] == 0xa0 && pixel[0] == 0xf0;
    }
    EXPECT_GT(coloured, 8) << "the stems of four letters";

    f.apply();
    EXPECT_TRUE(f.image->modified());
}

// Why: text has to read on any picture, so it can have a dark edge. The edge is around the
// letters, not in place of them: the layer still has the text's colour, and also dark pixels
// that it does not have without the edge.
TEST(Annotate, TextCanHaveADarkEdge) {
    Shape text{ShapeKind::Text, {{10, 10}, {300, 80}}, {}, "HHHH"};
    text.style.colour = {255, 255, 255, 255};
    text.style.textSize = 40;
    const auto count = [](const DrawEdit& edit, bool dark) {
        int found = 0;
        for (int y = 0; y < edit.drawing.height(); ++y) {
            const auto* pixel = reinterpret_cast<const std::uint8_t*>(edit.drawing.row(y));
            for (int x = 0; x < edit.drawing.width(); ++x, pixel += 4) {
                found += dark ? pixel[3] > 150 && pixel[2] < 40 : pixel[3] == 255 && pixel[2] == 255;
            }
        }
        return found;
    };
    const DrawEdit plain = renderShapes(std::span(&text, 1), {400, 300}, {0, 255});
    text.style.outline = true;
    const DrawEdit edged = renderShapes(std::span(&text, 1), {400, 300}, {0, 255});
    ASSERT_FALSE(plain.drawing.isNull());
    ASSERT_FALSE(edged.drawing.isNull());
    EXPECT_GT(count(plain, false), 100);
    EXPECT_EQ(count(plain, true), 0);
    EXPECT_GT(count(edged, true), 100);
    EXPECT_GT(count(edged, false), count(plain, false) * 0.9) << "the letters are as before";
}

// Why: the typeface and its weight are part of a text's style, chosen from those installed.
// Checked without relying on any one typeface being there: the family shown is one of the
// list, setting another changes the box selected, and bold letters cover more pixels than
// regular ones of the same words and size.
TEST(Annotate, TextTakesATypefaceAndBold) {
    AnnotatePage f;
    f.choose(Tool::Text, Qt::white, 1);
    f.overlay->setTextSize(40);
    f.overlay->setBold(false);
    const QStringList families = f.overlay->fontFamilies();
    ASSERT_FALSE(families.isEmpty());
    EXPECT_TRUE(families.contains(f.overlay->fontFamily()));

    f.dragOnImage({10, 10}, {390, 70});
    ASSERT_EQ(f.overlay->selected(), 0);
    f.overlay->setText(QStringLiteral("HHHH"));
    const QString other = families.front() != f.overlay->fontFamily() ? families.front() : families.back();
    f.overlay->setFontFamily(other);
    EXPECT_EQ(f.overlay->shapes()[0].style.fontFamily, other.toStdString());

    const auto covered = [&] {
        const DrawEdit edit = renderShapes(f.overlay->shapes(), {400, 300}, {0, 255});
        int found = 0;
        for (int y = 0; y < edit.drawing.height(); ++y) {
            const auto* pixel = reinterpret_cast<const std::uint8_t*>(edit.drawing.row(y));
            for (int x = 0; x < edit.drawing.width(); ++x, pixel += 4) {
                found += pixel[3] > 128;
            }
        }
        return found;
    };
    const int regular = covered();
    f.overlay->setBold(true);
    EXPECT_TRUE(f.overlay->shapes()[0].style.bold);
    if (QFontInfo(QFont(other)).family() == other) {
        EXPECT_GT(covered(), regular) << other.toStdString();
    }
    QMetaObject::invokeMethod(f.page, "cancelAnnotate");
}

// Why: enlarged, shapes must show as the pixels they will become, not smoothed to the
// screen. With the picture enlarged several times, a rectangle's edge on screen falls where
// an image pixel ends: the screen pixel just inside the edge is fully the rectangle's colour
// and the one just outside is the picture's, as it is well away from the rectangle. Drawn
// scaled to the screen instead, the edge would land between screen pixels and blend the two.
// Read from the window, so it needs a platform that can grab it.
TEST(Annotate, EnlargedShapesShowAsThePixelsTheyWillBe) {
    ToolPage f("startAnnotate");
    auto* overlay = qobject_cast<AnnotateOverlay*>(f.page->findChild<QQuickItem*>(QStringLiteral("annotateOverlay")));
    ASSERT_NE(overlay, nullptr);
    overlay->setTool(AnnotateOverlay::Tool::Rectangle);
    overlay->setColour(QColor(0x10, 0xa0, 0xf0));
    overlay->setLineWidth(1);
    overlay->setFilled(true);
    overlay->setOutline(false);
    overlay->setShadow(false);
    ASSERT_NE(f.drag(Qt::LeftButton, QPoint(100, 60)), QPointF(1, 1));
    ASSERT_EQ(overlay->count(), 1);
    QMetaObject::invokeMethod(f.page, "escapeAnnotate");  // without the frame of the one selected
    const QImage shown = grabIfShown(f.view);
    if (shown.isNull()) {
        GTEST_SKIP() << "nothing to grab on this platform";
    }

    // Along a row through the rectangle's middle, across its left edge.
    const RectD box = bounds(overlay->shapes()[0]);
    const QPointF edge = f.image->mapToScene(f.image->imageToItem(QPointF(box.x, box.y + box.height / 2)));
    const int y = static_cast<int>(edge.y());
    const int x = static_cast<int>(std::round(edge.x()));
    const QColor fill(0x10, 0xa0, 0xf0);
    EXPECT_EQ(shown.pixelColor(x + 1, y), fill);
    EXPECT_EQ(shown.pixelColor(x, y), fill) << "the first screen pixel of the image pixel inside";
    EXPECT_EQ(shown.pixelColor(x - 1, y), shown.pixelColor(x - 3, y)) << "outside, the picture alone";
    EXPECT_NE(shown.pixelColor(x - 1, y), fill);
    QMetaObject::invokeMethod(f.page, "cancelAnnotate");
}

// Why: an ellipse is drawn like a rectangle but fills only itself. Filled, in a box from
// 20, 20 to 120, 80 on the light part, its middle at 70, 50 takes the colour when applied
// and the box's corner at 23, 23, outside the ellipse, stays as the picture was.
TEST(Annotate, AnEllipseFillsItselfAndNotItsBox) {
    AnnotatePage f;
    f.choose(Tool::Ellipse, QColor(0x10, 0xa0, 0xf0), 2);
    f.overlay->setFilled(true);
    const QString corner = f.hexAt(23, 23);
    f.dragOnImage({20, 20}, {120, 80});
    ASSERT_EQ(f.overlay->count(), 1);
    EXPECT_EQ(f.overlay->shapes()[0].kind, ShapeKind::Ellipse);
    EXPECT_EQ(f.overlay->shapes()[0].points, (std::vector<PointD>{{20, 20}, {120, 80}}));
    f.apply();
    EXPECT_EQ(f.hexAt(70, 50), QStringLiteral("#10a0f0"));
    EXPECT_EQ(f.hexAt(23, 23), corner);
}

// Why: Shift while dragging keeps a box's sides equal. Dragged from 20, 20 to 120, 60 with
// Shift down, the longer way, 100, becomes both sides.
TEST(Annotate, ShiftKeepsABoxSquare) {
    AnnotatePage f;
    f.choose(Tool::Rectangle, Qt::red, 2);
    QTest::mousePress(&f.view, Qt::LeftButton, Qt::ShiftModifier, f.at(20, 20));
    QTest::mouseEvent(QTest::MouseMove, &f.view, Qt::NoButton, Qt::ShiftModifier, f.at(70, 40));
    QTest::mouseEvent(QTest::MouseMove, &f.view, Qt::NoButton, Qt::ShiftModifier, f.at(120, 60));
    QTest::mouseRelease(&f.view, Qt::LeftButton, Qt::ShiftModifier, f.at(120, 60));
    ASSERT_EQ(f.overlay->count(), 1);
    EXPECT_EQ(f.overlay->shapes()[0].points, (std::vector<PointD>{{20, 20}, {120, 120}}));
    QMetaObject::invokeMethod(f.page, "cancelAnnotate");
}

// Why: numbered points mark steps, so each click gives the next number without typing. Three
// clicks say 1, 2, 3; with the second removed the next is still 4, as 3 is there. What one
// says can be changed like text. Applied, the disc is in the colour chosen: red, read at its
// edge, away from the digit in its middle (the disc's radius is 16 for a text size of 20).
TEST(Annotate, NumbersCountUpWithEachClick) {
    AnnotatePage f;
    f.choose(Tool::Number, Qt::red, 2);
    f.overlay->setTextSize(20);
    for (const QPoint at : {QPoint(40, 40), QPoint(100, 40), QPoint(160, 40)}) {
        QTest::mouseClick(&f.view, Qt::LeftButton, Qt::NoModifier, f.at(at.x(), at.y()));
    }
    ASSERT_EQ(f.overlay->count(), 3);
    EXPECT_EQ(f.overlay->shapes()[0].text, "1");
    EXPECT_EQ(f.overlay->shapes()[2].text, "3");
    EXPECT_EQ(f.overlay->selected(), 2);

    QTest::mouseClick(&f.view, Qt::LeftButton, Qt::NoModifier, f.at(100, 40));
    ASSERT_EQ(f.overlay->selected(), 1);
    QTest::keyClick(&f.view, Qt::Key_Delete);
    QTest::mouseClick(&f.view, Qt::LeftButton, Qt::NoModifier, f.at(100, 40));
    ASSERT_EQ(f.overlay->count(), 3);
    EXPECT_EQ(f.overlay->shapes()[2].text, "4");

    f.overlay->setText(QStringLiteral("B"));
    EXPECT_EQ(f.overlay->shapes()[2].text, "B");

    f.apply();
    EXPECT_EQ(f.hexAt(28, 40), QStringLiteral("#ff0000"));
    EXPECT_NE(f.hexAt(40, 62), QStringLiteral("#ff0000")) << "below the disc";
}

namespace {

// A pixel of the layer an edit draws, at a place in the picture: blue, green, red, alpha.
std::array<int, 4> drawnAt(const DrawEdit& edit, int x, int y) {
    const auto* pixel = reinterpret_cast<const std::uint8_t*>(edit.drawing.row(y - edit.y)) + 4 * (x - edit.x);
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
}

Shape whiteBox() {
    Shape box;
    box.kind = ShapeKind::Rectangle;
    box.points = {{100, 100}, {140, 130}};
    box.style.colour = {255, 255, 255, 255};
    box.style.filled = true;
    return box;
}

}  // namespace

// Why: the dark edge is as wide as it is set, on every side, whatever the kind of shape, as
// it is made from the shape's pixels. A filled box from 100 to 140 across with an edge of 3:
// the pixels at 98 and 141, whose middles are 1.5 outside, are fully dark; at 95 and 144,
// 4.5 outside, beyond the edge and its fade of 1.5, nothing is drawn. The box stays white.
TEST(Annotate, TheDarkEdgeIsAsWideAsSet) {
    Shape box = whiteBox();
    box.style.outline = true;
    box.style.outlineWidth = 3;
    const DrawEdit edit = renderShapes(std::span(&box, 1), {400, 300}, {0, 255});
    ASSERT_FALSE(edit.drawing.isNull());
    for (const int x : {98, 141}) {
        const auto dark = drawnAt(edit, x, 115);
        EXPECT_EQ(dark[3], 255) << x;
        EXPECT_LT(dark[2], 40) << x;
    }
    EXPECT_EQ(drawnAt(edit, 95, 115)[3], 0);
    EXPECT_EQ(drawnAt(edit, 144, 115)[3], 0);
    EXPECT_EQ(drawnAt(edit, 120, 95)[3], 0) << "above too";
    EXPECT_EQ(drawnAt(edit, 120, 115), (std::array<int, 4>{255, 255, 255, 255}));
}

// Why: a shadow lies where it is set, by a distance each way that need not be right and
// down. Set 6 to the left and none down, without blur, the box's shadow shows from 94 to 100
// across and nowhere to the right of the box; black, and letting the picture through.
TEST(Annotate, TheShadowLiesWhereItIsSet) {
    Shape box = whiteBox();
    box.style.shadow = true;
    box.style.shadowOffset = PointD{-6, 0};
    box.style.shadowBlur = 0;
    const DrawEdit edit = renderShapes(std::span(&box, 1), {400, 300}, {0, 255});
    ASSERT_FALSE(edit.drawing.isNull());
    const auto shade = drawnAt(edit, 97, 115);
    EXPECT_GT(shade[3], 100);
    EXPECT_LT(shade[3], 200);
    EXPECT_EQ(shade[2], 0);
    EXPECT_EQ(drawnAt(edit, 92, 115)[3], 0);
    EXPECT_EQ(drawnAt(edit, 142, 115)[3], 0);
    EXPECT_EQ(drawnAt(edit, 120, 132)[3], 0) << "none below";

    // Blurred, it fades instead of ending: further out there is still some, and less.
    box.style.shadowBlur = 3;
    const DrawEdit soft = renderShapes(std::span(&box, 1), {400, 300}, {0, 255});
    EXPECT_GT(drawnAt(soft, 92, 115)[3], 0);
    EXPECT_LT(drawnAt(soft, 92, 115)[3], drawnAt(soft, 97, 115)[3]);
}

// Why: the sizes of the edge and the shadow are part of a tool's style like its colour: set,
// they go to the shape selected and are remembered for the next; below zero they follow the
// shape's size again. The panel's one shadow distance is the same right and down.
TEST(Annotate, EdgeAndShadowSizesAreSetAndRemembered) {
    {
        AnnotatePage f;
        f.choose(Tool::Line, Qt::red, 4);
        f.dragOnImage({20, 20}, {120, 20});
        ASSERT_EQ(f.overlay->selected(), 0);
        f.overlay->setOutlineWidth(5);
        f.overlay->setShadowOffset(7);
        f.overlay->setShadowBlur(2);
        const ShapeStyle& style = f.overlay->shapes()[0].style;
        EXPECT_EQ(style.outlineWidth, 5);
        EXPECT_EQ(style.shadowOffset, (PointD{7, 7}));
        EXPECT_EQ(style.shadowBlur, 2);
        QMetaObject::invokeMethod(f.page, "cancelAnnotate");
    }
    AnnotatePage f;
    f.overlay->setTool(Tool::Line);
    EXPECT_EQ(f.overlay->outlineWidth(), 5);
    EXPECT_EQ(f.overlay->shadowOffset(), 7);
    EXPECT_EQ(f.overlay->shadowBlur(), 2);
    f.overlay->setOutlineWidth(-1);
    f.overlay->setShadowOffset(-1);
    f.overlay->setShadowBlur(-1);
    f.dragOnImage({20, 20}, {120, 20});
    const ShapeStyle& style = f.overlay->shapes()[0].style;
    EXPECT_FALSE(style.outlineWidth.has_value());
    EXPECT_FALSE(style.shadowOffset.has_value());
    EXPECT_FALSE(style.shadowBlur.has_value());
    QMetaObject::invokeMethod(f.page, "cancelAnnotate");
}

// Why: with Shift held, a line drawn nearly level comes out exactly level. Dragged from
// 20, 40 to 120, 44, about 2 degrees off, both ends are at 40 down.
TEST(Annotate, ShiftKeepsALineLevel) {
    AnnotatePage f;
    f.choose(Tool::Line, Qt::red, 2);
    QTest::mousePress(&f.view, Qt::LeftButton, Qt::ShiftModifier, f.at(20, 40));
    QTest::mouseEvent(QTest::MouseMove, &f.view, Qt::NoButton, Qt::ShiftModifier, f.at(70, 42));
    QTest::mouseEvent(QTest::MouseMove, &f.view, Qt::NoButton, Qt::ShiftModifier, f.at(120, 44));
    QTest::mouseRelease(&f.view, Qt::LeftButton, Qt::ShiftModifier, f.at(120, 44));
    ASSERT_EQ(f.overlay->count(), 1);
    const std::vector<PointD>& ends = f.overlay->shapes()[0].points;
    EXPECT_DOUBLE_EQ(ends[1].y, ends[0].y);
    EXPECT_GT(ends[1].x, ends[0].x + 90);
    QMetaObject::invokeMethod(f.page, "cancelAnnotate");
}

// Why: an arrow's shaft must be there at every width. It once vanished at some widths, as
// the shaft and the head were joined by a path operation that fails on certain shapes. For
// every whole width from 3 to 40 and ends at several places, also between pixels, the pixel in
// the middle of the shaft, a quarter of the way along, is in the arrow's colour.
TEST(Annotate, AnArrowHasItsShaftAtEveryWidth) {
    const std::vector<std::pair<PointD, PointD>> places{
        {{50, 100}, {300, 100}}, {{50.3, 80.7}, {310.2, 140.4}}, {{300, 250}, {60, 40}}, {{200, 30}, {200, 280}}, {{61.5, 200.25}, {333.1, 190.8}}};
    for (const auto& [from, to] : places) {
        for (int width = 3; width <= 40; ++width) {
            Shape arrow;
            arrow.kind = ShapeKind::Arrow;
            arrow.points = {from, to};
            arrow.style.width = width;
            arrow.style.colour = {16, 160, 240, 255};
            const DrawEdit edit = renderShapes(std::span(&arrow, 1), {400, 300}, {0, 255});
            ASSERT_FALSE(edit.drawing.isNull());
            const int x = static_cast<int>(from.x + (to.x - from.x) / 4);
            const int y = static_cast<int>(from.y + (to.y - from.y) / 4);
            EXPECT_EQ(drawnAt(edit, x, y), (std::array<int, 4>{240, 160, 16, 255})) << "width " << width << " from " << from.x << "," << from.y;
        }
    }
}

// Why: the page has one tool at work at a time, and starting another leaves the one before
// it as cancelling would, whichever the two are. Input: an arrow drawn and not applied, then
// each other tool started in turn, Adjust with a change being previewed. The truth is the
// page's `tool`, that the arrow is gone unapplied, that the colour preview ends with Adjust,
// and that at most one panel is in view throughout.
TEST(ViewerPage, StartingAToolLeavesTheOneAtWork) {
    AnnotatePage f;
    f.choose(Tool::Arrow, QColor(0x10, 0xa0, 0xf0), 6);
    f.dragOnImage({20, 20}, {120, 20});
    ASSERT_EQ(f.overlay->count(), 1);
    EXPECT_EQ(f.page->property("tool").toString(), QStringLiteral("annotate"));

    const auto panelsInView = [&] {
        int revealed = 0;
        for (const char* name : {"cropPanel", "annotatePanel", "straightenPanel", "adjustPanel", "probePanel"}) {
            QObject* panel = f.page->findChild<QObject*>(QString::fromLatin1(name));
            EXPECT_NE(panel, nullptr) << name;
            revealed += panel && panel->property("revealed").toBool();
        }
        return revealed;
    };
    EXPECT_EQ(panelsInView(), 1);

    QMetaObject::invokeMethod(f.page, "startAdjust");
    EXPECT_EQ(f.page->property("tool").toString(), QStringLiteral("adjust"));
    EXPECT_FALSE(f.page->property("annotating").toBool());
    EXPECT_EQ(f.overlay->count(), 0) << "the arrow was given up";
    EXPECT_FALSE(f.image->modified()) << "and not drawn into the picture";
    EXPECT_EQ(panelsInView(), 1);
    f.page->findChild<QObject*>(QStringLiteral("adjustPanel"))->setProperty("midtones", 1.5);
    ASSERT_TRUE(f.image->colorPreviewActive());

    const std::pair<const char*, const char*> others[] = {
        {"startProbe", "probe"}, {"startCrop", "crop"}, {"startStraighten", "straighten"}, {"startAnnotate", "annotate"}};
    for (const auto& [start, name] : others) {
        QMetaObject::invokeMethod(f.page, start);
        EXPECT_EQ(f.page->property("tool").toString(), QString::fromLatin1(name));
        EXPECT_TRUE(f.page->property("toolActive").toBool());
        EXPECT_FALSE(f.image->colorPreviewActive()) << "Adjust was left before " << name;
        EXPECT_EQ(panelsInView(), 1) << name;
    }
    EXPECT_FALSE(f.image->modified()) << "no tool was applied";

    QMetaObject::invokeMethod(f.page, "cancelAnnotate");
    EXPECT_EQ(f.page->property("tool").toString(), QString());
    EXPECT_FALSE(f.page->property("toolActive").toBool());
    EXPECT_EQ(panelsInView(), 0);
    // Ending a tool that is not the one at work leaves the one that is.
    QMetaObject::invokeMethod(f.page, "startCrop");
    QMetaObject::invokeMethod(f.page, "stopProbe");
    QMetaObject::invokeMethod(f.page, "cancelStraighten");
    EXPECT_EQ(f.page->property("tool").toString(), QStringLiteral("crop"));
}

// Why: the colours offered are a chosen set, ColorBrewer's Set1 with white and black, and a
// tool starts in one of them so that its swatch is marked. Input: the Annotate panel as it
// opens for the arrow and the marker with nothing remembered, and a click on a swatch. The
// truth is the list of swatches, and the overlay's colour before and after the click.
TEST(Annotate, OffersSet1WithWhiteAndBlackAndStartsInOneOfThem) {
    QSettings().remove(QStringLiteral("Annotate"));  // nothing remembered from tests before
    AnnotatePage f;
    QStringList offered;
    QList<QQuickItem*> swatches;
    // What a Repeater makes is found among the items shown, not among the objects owned.
    const std::function<void(QQuickItem*)> collect = [&](QQuickItem* item) {
        if (item->objectName() == QStringLiteral("annotateColour")) {
            swatches.append(item);
            offered.append(item->property("color").value<QColor>().name());
        }
        for (QQuickItem* child : item->childItems()) {
            collect(child);
        }
    };
    collect(f.page->findChild<QQuickItem*>(QStringLiteral("annotatePanel")));
    const QStringList set1 = {"#e41a1c", "#377eb8", "#4daf4a", "#984ea3", "#ff7f00", "#ffff33",
                              "#a65628", "#f781bf", "#999999", "#ffffff", "#000000"};
    ASSERT_EQ(offered, set1);

    for (Tool tool : {Tool::Arrow, Tool::Marker}) {
        f.overlay->setTool(tool);
        EXPECT_TRUE(set1.contains(f.overlay->colour().name())) << f.overlay->colour().name().toStdString();
    }

    f.overlay->setTool(Tool::Arrow);
    QQuickItem* blue = swatches.at(1);
    QTest::mouseClick(&f.view, Qt::LeftButton, Qt::NoModifier,
                      blue->mapToScene(QPointF(blue->width() / 2, blue->height() / 2)).toPoint());
    EXPECT_EQ(f.overlay->colour().name(), QStringLiteral("#377eb8"));
    EXPECT_TRUE(blue->property("current").toBool());
    QSettings().remove(QStringLiteral("Annotate"));
}

// Why: no colour of a tool is written into the C++; they come from Theme through the style.
// The pattern that stands in for a pixelated box, until its pixels are ready, is one such.
// Input: a pixelated box painted for preview with a style of made-up colours, on a clear
// image. The truth is that only those two colours appear, and that burning in draws nothing.
TEST(Annotate, ThePatternStandingInForPixelsTakesTheStylesColours) {
    Shape box;
    box.kind = ShapeKind::Pixelate;
    box.points = {{4, 4}, {36, 28}};
    OverlayStyle style;
    style.setProperty("line", QColor(250, 0, 250));
    style.setProperty("dim", QColor(0, 250, 0));

    const auto painted = [&](const OverlayStyle* preview) {
        QImage image(40, 32, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        paintShape(&painter, box, preview);
        painter.end();
        return image;
    };
    const QImage shown = painted(&style);
    int lines = 0, fills = 0, others = 0;
    for (int y = 0; y < shown.height(); ++y) {
        for (int x = 0; x < shown.width(); ++x) {
            const QColor c = shown.pixelColor(x, y);
            if (c.alpha() == 0) {
                continue;
            }
            // Where the smoothed line meets the pattern the two are mixed, so each pixel is
            // some of one colour and the rest of the other: red and blue go together, and
            // with green they make up the 250 of either.
            const bool mixed = std::abs(c.red() - c.blue()) <= 8 && std::abs(c.red() + c.green() - 250) <= 16;
            lines += mixed && c.red() > 200;
            fills += mixed && c.green() > 200;
            others += !mixed;
        }
    }
    EXPECT_GT(lines, 50);
    EXPECT_GT(fills, 50);
    EXPECT_EQ(others, 0) << "white or black would be a colour written into the painter";

    QImage clear(shown.size(), shown.format());
    clear.fill(Qt::transparent);
    EXPECT_EQ(painted(nullptr), clear) << "a pixelated box has no drawing of its own";
}

// Why: the Annotate panel, with text's options, is taller than a window that is not full
// screen, and its lower controls must still be reachable. Input: the panel in a window 300
// high. The truth is the panel's scroller: its content is taller than it, the wheel moves it,
// and the heading and the Apply button stay in view meanwhile.
TEST(Annotate, ThePanelScrollsWhenTheWindowIsLow) {
    AnnotatePage f;
    f.overlay->setTool(Tool::Text);
    f.view.resize(900, 300);
    auto* panel = f.page->findChild<QQuickItem*>(QStringLiteral("annotatePanel"));
    ASSERT_NE(panel, nullptr);
    auto* scroller = panel->findChild<QQuickItem*>(QStringLiteral("toolPanelScroller"));
    ASSERT_NE(scroller, nullptr);
    ASSERT_TRUE(waitUntil([&] { return scroller->property("contentHeight").toDouble() > scroller->height(); }));
    EXPECT_TRUE(scroller->property("interactive").toBool());

    const QPointF middle = scroller->mapToScene(QPointF(scroller->width() / 2, scroller->height() / 2));
    QWheelEvent wheel(middle, f.view.mapToGlobal(middle), QPoint(), QPoint(0, -360), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&f.view, &wheel);
    EXPECT_TRUE(waitUntil([&] { return scroller->property("contentY").toDouble() > 0; }));

    auto* apply = panel->findChild<QQuickItem*>(QStringLiteral("annotateApply"));
    ASSERT_NE(apply, nullptr);
    const QPointF applyBottom = apply->mapToScene(QPointF(0, apply->height()));
    EXPECT_LE(applyBottom.y(), f.view.height()) << "Apply stays in the window";
    EXPECT_GE(scroller->mapToScene(QPointF(0, 0)).y(), panel->mapToScene(QPointF(0, 0)).y() + 10) << "below the heading";
}

// Why: a panel that fits does not scroll, so that a drag on it is not taken for scrolling.
// Input: the Annotate panel in the ordinary 600-high window with the arrow, which has few
// options. The truth is the scroller's interactive flag.
TEST(Annotate, ThePanelDoesNotScrollWhenItFits) {
    AnnotatePage f;
    f.overlay->setTool(Tool::Arrow);
    auto* scroller = f.page->findChild<QQuickItem*>(QStringLiteral("annotatePanel"))->findChild<QQuickItem*>(QStringLiteral("toolPanelScroller"));
    ASSERT_NE(scroller, nullptr);
    waitUntil([] { return false; }, 100);
    EXPECT_LE(scroller->property("contentHeight").toDouble(), scroller->height());
    EXPECT_FALSE(scroller->property("interactive").toBool());
}

// Why: were the controls narrower while the panel scrolls, to make room for the scroll bar, a
// panel of wrapped text that only just fits could switch between the two widths every frame
// and starve the rest of the window (the Straighten panel once did, and pictures stopped
// loading). Input: the Annotate panel first fitting, then scrolling in a low window. The truth
// is the width of the column that holds the controls, which must not change.
TEST(Annotate, ThePanelsControlsKeepTheirWidthWhetherItScrollsOrNot) {
    AnnotatePage f;
    f.overlay->setTool(Tool::Text);
    auto* scroller = f.page->findChild<QQuickItem*>(QStringLiteral("annotatePanel"))->findChild<QQuickItem*>(QStringLiteral("toolPanelScroller"));
    ASSERT_NE(scroller, nullptr);
    QQuickItem* column = scroller->property("contentItem").value<QQuickItem*>()->childItems().value(0);
    ASSERT_NE(column, nullptr);
    ASSERT_TRUE(waitUntil([&] { return !scroller->property("interactive").toBool(); }));
    const qreal fitting = column->width();

    f.view.resize(900, 300);
    ASSERT_TRUE(waitUntil([&] { return scroller->property("interactive").toBool(); }));
    EXPECT_EQ(column->width(), fitting);
}
