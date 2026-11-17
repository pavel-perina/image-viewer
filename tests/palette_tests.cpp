// The interface's colours worked out from a hue and two amounts of colour (core/palette.h).
// Inputs: hues all round the circle, and the defaults. The truth is WCAG contrast between the
// colours that are read against each other, and the lightness order the roles must keep, for
// any hue; and that the defaults are the willow look they were chosen for.

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <string>

#include "core/oklab.h"
#include "core/palette.h"

using namespace iv;

namespace {

Rgba8 role(const std::vector<PaletteRole>& palette, const char* name) {
    const PaletteRole* found = findRole(palette, name);
    EXPECT_NE(found, nullptr) << name;
    return found ? found->colour : Rgba8{};
}

}  // namespace

// Why: the reason lightness is fixed per role. Whatever hue someone picks, text stays readable
// on every surface it sits on, and the selection's text on the selection.
TEST(Palette, TextStaysReadableForEveryHue) {
    for (double hue = 0; hue < 360; hue += 15) {
        const auto palette = derivePalette({.hue = hue, .accentChroma = 0.12, .surfaceChroma = 0.03});
        for (const char* surface : {"window", "base", "alternateBase", "button", "raised", "header", "toolTip"}) {
            EXPECT_GE(contrastRatio(role(palette, "text"), role(palette, surface)), 7) << surface << " at " << hue;
            EXPECT_GE(contrastRatio(role(palette, "textMuted"), role(palette, surface)), 4.5) << surface << " at " << hue;
        }
        EXPECT_GE(contrastRatio(role(palette, "highlightedText"), role(palette, "highlight")), 4.5) << hue;
        EXPECT_GE(contrastRatio(role(palette, "text"), role(palette, "defaultButtonTop")), 4.5) << hue;
        EXPECT_GE(contrastRatio(role(palette, "accent"), role(palette, "window")), 3) << "marks and focus, " << hue;
    }
}

// Why: a selection must not be taken for a hovered row, nor a surface for its neighbour. The
// lightness order of the roles holds whatever the hue.
TEST(Palette, SurfacesKeepTheirOrderForEveryHue) {
    for (double hue = 0; hue < 360; hue += 15) {
        const auto palette = derivePalette({.hue = hue, .accentChroma = 0.12, .surfaceChroma = 0.03});
        const auto luma = [&](const char* name) { return contrastRatio(role(palette, name), Rgba8{0, 0, 0, 255}); };
        EXPECT_LT(luma("shadow"), luma("dark")) << hue;
        EXPECT_LT(luma("dark"), luma("base")) << hue;
        EXPECT_LT(luma("base"), luma("window")) << hue;
        EXPECT_LT(luma("window"), luma("button")) << hue;
        EXPECT_LT(luma("button"), luma("light")) << hue;
        EXPECT_GT(contrastRatio(role(palette, "highlight"), role(palette, "window")), 2) << "a selection stands out, " << hue;
    }
}

// Why: colourfulness the screen cannot show is reduced, not clipped per channel, which would
// shift the hue and the lightness. Read back in Oklab, the colour has the lightness and hue asked
// for, with less chroma.
TEST(Palette, ColourOutOfGamutKeepsItsLightnessAndHue) {
    const Rgba8 asked = fromOklch(0.8, 0.4, 150);  // far more green than sRGB has at this lightness
    const Oklab back = linearToOklab(srgbDecode(asked.r / 255.0), srgbDecode(asked.g / 255.0), srgbDecode(asked.b / 255.0));
    EXPECT_NEAR(back.L, 0.8, 0.005);
    EXPECT_NEAR(std::atan2(back.b, back.a) * 180 / std::numbers::pi, 150, 1.5);
    const double chroma = std::hypot(back.a, back.b);
    EXPECT_GT(chroma, 0.1) << "as much as fits, not gray";
    EXPECT_LT(chroma, 0.4);
}

// Why: no colour at all gives gray: equal channels for every surface and text.
TEST(Palette, NoChromaGivesGray) {
    for (const PaletteRole& r : derivePalette({.hue = 150, .accentChroma = 0, .surfaceChroma = 0})) {
        EXPECT_EQ(r.colour.r, r.colour.g) << r.name;
        EXPECT_EQ(r.colour.g, r.colour.b) << r.name;
    }
}

// Why: a subtle surface tint looks clearly green on some displays. Defaults must keep
// backgrounds and text neutral, while green marks and actions still stand out.
TEST(Palette, TheDefaultsKeepSurfacesNeutralAndAccentsGreen) {
    const auto palette = derivePalette({});
    const Rgba8 window = role(palette, "window");
    const Rgba8 accent = role(palette, "accent");
    for (const char* name : {"window", "base", "button", "text", "textMuted", "raised", "header",
                             "controlTop", "controlBottom", "panel", "bubble", "badge"}) {
        const Rgba8 colour = role(palette, name);
        EXPECT_EQ(colour.r, colour.g) << name;
        EXPECT_EQ(colour.g, colour.b) << name;
    }
    EXPECT_LT(window.g, 60) << "dark";
    EXPECT_GT(accent.g, 170) << "light";
    EXPECT_GT(accent.g, accent.r);
    EXPECT_GT(role(palette, "defaultButtonTop").g, role(palette, "defaultButtonTop").r);
    EXPECT_EQ(role(palette, "panel").a, 230) << "panels let the picture through a little";
}

// Why: neutral defaults should not remove the ability to choose tinted surfaces later.
// Input: the former surface chroma; the window must once again carry the chosen green hue.
TEST(Palette, SurfaceTintRemainsAnExplicitChoice) {
    const auto palette = derivePalette({.surfaceChroma = 0.014});
    EXPECT_GT(role(palette, "window").g, role(palette, "window").r);
}
