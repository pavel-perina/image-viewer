// Edit > Annotate in the large view: drawing shapes on the picture with the mouse, changing
// them before they are applied, and what applying leaves in the pixels. The picture is 400 by
// 300, shown at its own size, light (about 220) around a dark (about 30) rectangle in the
// middle; it is a JPEG, so its values are near those and not exact. Pixels are read back
// through the probe, which gives the edited picture as it would be saved.

#include "ui_test_support.h"

#include "ui/annotate_overlay.h"
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
