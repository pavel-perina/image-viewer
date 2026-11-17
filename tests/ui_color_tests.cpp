#include "ui_test_support.h"

#include <QPainter>
#include <QSGTextureProvider>

#include "core/palette.h"
#include "ui/color_choice_model.h"
#include "ui/color_ramp.h"

using iv::ColorChoice;
using iv::ColorRamp;

// Why: the chooser's sliders, number boxes and hex field all show one colour, so setting any
// of them sets the rest. Input: hex ff0000, then green set to 255, then lightness set to 0,
// then text that is no colour. The truth: red 255 with the Oklch lightness of sRGB red, 0.628;
// then ffff00; then black; and the bad text changes nothing.
TEST(ColorChoice, EveryChannelSetsTheOthers) {
    ColorChoice choice;
    choice.setHex(QStringLiteral("ff0000"));
    EXPECT_EQ(choice.red(), 255);
    EXPECT_EQ(choice.green(), 0);
    EXPECT_NEAR(choice.lightness(), 0.628, 1e-3);
    choice.setGreen(255);
    EXPECT_EQ(choice.hex(), QStringLiteral("#ffff00"));
    choice.setLightness(0);
    EXPECT_EQ(choice.hex(), QStringLiteral("#000000"));
    choice.setHex(QStringLiteral("not a colour"));
    EXPECT_EQ(choice.hex(), QStringLiteral("#000000"));
}

// Why: a gray has no hue of its own, so the hue slider would jump to 0 when chroma reaches 0
// or a gray is typed; it keeps the hue last set instead. Input: hue 120 and some chroma, then
// chroma 0, then a gray typed as hex. The truth: a gray colour each time, the hue still 120.
TEST(ColorChoice, AGrayKeepsTheHueLastSet) {
    ColorChoice choice;
    choice.setLightness(0.6);
    choice.setChroma(0.1);
    choice.setHue(120);
    choice.setChroma(0);
    EXPECT_EQ(choice.red(), choice.green());
    EXPECT_EQ(choice.green(), choice.blue());
    EXPECT_EQ(choice.hue(), 120);
    choice.setHex(QStringLiteral("#808080"));
    EXPECT_EQ(choice.hue(), 120);
}

// Why: a slider dragged where sRGB cannot follow stays where it was dragged, the colour taking
// the most chroma that fits, and the tracks say so. Input: a blue, with chroma set to the most
// the slider offers. The truth: chroma reads as set and outside is on, while the colour is the
// one fromOklch gives; there are six tracks of 48 stops, the chroma track's last outside.
TEST(ColorChoice, ASliderPastSrgbStaysWhereItWasSet) {
    ColorChoice choice;
    choice.setHex(QStringLiteral("#2040c0"));
    choice.setChroma(choice.maxChroma());
    EXPECT_DOUBLE_EQ(choice.chroma(), choice.maxChroma());
    EXPECT_TRUE(choice.outside());
    const iv::Rgba8 fitted = iv::fromOklch(choice.lightness(), choice.chroma(), choice.hue());
    EXPECT_EQ(choice.colour(), QColor(fitted.r, fitted.g, fitted.b));
    const QVariantList ramps = choice.ramps();
    ASSERT_EQ(ramps.size(), 6);
    const QVariantList chroma = ramps[ColorChoice::Chroma].toList();
    ASSERT_EQ(chroma.size(), ColorChoice::kRampStops);
    EXPECT_TRUE(chroma.last().toMap().value(QStringLiteral("outside")).toBool());
    EXPECT_FALSE(chroma.first().toMap().value(QStringLiteral("outside")).toBool());
}

// Why: a track shows its colours left to right and hatches what sRGB cannot show. Input: a
// 100 x 10 track from black to white, its right half marked outside, hatched in pure red. The
// truth: the left end dark and without red; red hatching somewhere in the right half only.
TEST(ColorRamp, DrawsItsColoursAndHatchesWhatLiesOutside) {
    ColorRamp ramp;
    ramp.setSize(QSizeF(100, 10));
    ramp.setHatch(QColor(255, 0, 0));
    QVariantList stops;
    for (int i = 0; i < 11; ++i) {
        stops.append(QVariantMap{{QStringLiteral("colour"), QColor::fromRgbF(i / 10.0F, i / 10.0F, i / 10.0F)},
                                 {QStringLiteral("outside"), i > 5}});
    }
    ramp.setStops(stops);
    QImage image(100, 10, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    ramp.paint(&painter);
    painter.end();
    const auto reddish = [](QColor c) { return c.red() > c.green() + 60; };
    EXPECT_LT(image.pixelColor(1, 5).value(), 40);
    bool redLeft = false;
    bool redRight = false;
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 100; ++x) {
            (x < 45 ? redLeft : redRight) |= x < 45 || x > 60 ? reddish(image.pixelColor(x, y)) : false;
        }
    }
    EXPECT_FALSE(redLeft);
    EXPECT_TRUE(redRight);
}

// Why: the chooser's sliders and boxes set the colour, and each track follows it. Input: a
// chooser started on #808080; the red slider moved to 255; then chroma set to the most. The
// truth: the red box shows 128, then the colour is #ff8080 and the green track starts at
// red 255; at the most chroma the note that sRGB cannot show it appears.
TEST(ColorChooser, ItsSlidersSetTheColourAndTheTracksFollow) {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import ImageViewer
        Window {
            width: 500; height: 500; visible: true
            property alias chooser: chooser
            ColorChooser { id: chooser; anchors.fill: parent; colour: "#808080" }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* chooser = root->property("chooser").value<QQuickItem*>();
    QQuickItem* redBox = findNamed(chooser, QStringLiteral("chooserRed"));
    QQuickItem* redSlider = findNamed(chooser, QStringLiteral("chooserRedSlider"));
    QQuickItem* chromaSlider = findNamed(chooser, QStringLiteral("chooserChromaSlider"));
    QQuickItem* outside = findNamed(chooser, QStringLiteral("chooserOutside"));
    ASSERT_TRUE(redBox && redSlider && chromaSlider && outside);
    EXPECT_EQ(redBox->property("realValue").toDouble(), 128);

    redSlider->setProperty("value", 255);
    QMetaObject::invokeMethod(redSlider, "moved");
    EXPECT_EQ(chooser->property("colour").value<QColor>(), QColor(QStringLiteral("#ff8080")));
    auto* green = findNamed(chooser, QStringLiteral("chooserGreenSlider"));
    const QVariantList stops = green->property("background").value<QQuickItem*>()->property("stops").toList();
    ASSERT_FALSE(stops.isEmpty());
    EXPECT_EQ(stops.first().toMap().value(QStringLiteral("colour")).value<QColor>(), QColor(255, 0, 128));
    EXPECT_EQ(outside->property("opacity").toDouble(), 0);

    chromaSlider->setProperty("value", chromaSlider->property("to"));
    QMetaObject::invokeMethod(chromaSlider, "moved");
    EXPECT_EQ(outside->property("opacity").toDouble(), 1);
}

// Why: a colour field opens the chooser from its swatch, and the colour chosen there is
// handed to its owner, while Cancel changes nothing. Input: a field on #102030 whose owner
// takes what is picked; a click on the swatch; hex 00ff00 typed in the chooser, then OK. The
// truth: the chooser opens on #102030; after OK the field and its owner show #00ff00.
TEST(ColorChooser, AColourFieldOpensItFromItsSwatch) {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 600; height: 600; visible: true
            property color kept: "#102030"
            property alias field: field
            ColorField { id: field; x: 20; y: 20; width: 160; colour: kept; onPicked: (c) => kept = c }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
    auto* field = root->property("field").value<QQuickItem*>();
    QQuickItem* swatch = findNamed(field, QStringLiteral("colourFieldSwatch"));
    ASSERT_NE(swatch, nullptr);
    QTest::mouseClick(window, Qt::LeftButton, {}, swatch->mapToScene(QPointF(swatch->width() / 2, swatch->height() / 2)).toPoint());
    QObject* dialog = nullptr;
    ASSERT_TRUE(waitUntil([&] {
        dialog = root->findChild<QObject*>(QStringLiteral("colorChooserDialog"));
        return dialog && dialog->property("opened").toBool();
    }));
    auto* content = dialog->property("contentItem").value<QQuickItem*>();
    QQuickItem* hex = findNamed(content, QStringLiteral("chooserHex"));
    ASSERT_NE(hex, nullptr);
    EXPECT_EQ(hex->property("text").toString(), QStringLiteral("#102030"));
    hex->setProperty("text", QStringLiteral("00ff00"));
    QMetaObject::invokeMethod(hex, "editingFinished");
    QMetaObject::invokeMethod(dialog, "accept");
    EXPECT_EQ(root->property("kept").value<QColor>(), QColor(0, 255, 0));
    EXPECT_EQ(field->property("text").toString(), QStringLiteral("#00ff00"));
}

// Why: the texture of the colour preview's table lives on the render thread and is released
// by a job there; a closing window may drop the job unrun, and the texture's provider used to
// be lost with it, as the sanitizers' leak check showed for every test of the main window.
// Input: a window with a table texture that has been drawn, then destroyed. The truth: the
// provider is deleted, as a QPointer to it shows.
TEST(ColorLutTexture, ItsProviderGoesWithTheWindow) {
    QPointer<QSGTextureProvider> provider;
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import ImageViewer
            Window {
                width: 100; height: 100; visible: true
                property alias lut: lut
                ColorLutTexture { id: lut; width: 10; height: 10 }
            }
        )", QUrl());
        ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
        std::unique_ptr<QObject> root(component.create());
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        auto* lut = root->property("lut").value<QQuickItem*>();
        QImage table(4, 1, QImage::Format_RGBA8888);
        table.fill(Qt::gray);
        lut->setProperty("image", table);
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        window->grabWindow();
        provider = lut->textureProvider();
        ASSERT_FALSE(provider.isNull());
    }
    QCoreApplication::processEvents();
    EXPECT_TRUE(provider.isNull());
}
