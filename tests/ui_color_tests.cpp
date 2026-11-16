#include "ui_test_support.h"

#include <QPainter>

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
