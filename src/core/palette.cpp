#include "core/palette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "core/oklab.h"

namespace iv {

namespace {

// Where a role's colour comes from: the surface tint, the accent, or none.
enum class Source { Surface, Accent };

struct Rule {
    std::string_view name;
    double lightness;
    Source source;
    double chroma;  // times the chosen chroma of its source
    double hueShift = 0;
    double alpha = 1;
};

// Lightnesses taken from the willow mock-up, accents and text a little brighter. Surfaces go
// from the darkest (shadow) to the lightest (light); text from disabled to full.
constexpr std::array rules = {
    // Fusion's palette roles.
    Rule{"window", 0.25, Source::Surface, 1},
    Rule{"base", 0.20, Source::Surface, 1},           // lists and text fields
    Rule{"alternateBase", 0.27, Source::Surface, 1},
    Rule{"button", 0.33, Source::Surface, 1.2},
    Rule{"text", 0.92, Source::Surface, 1.8},
    Rule{"placeholderText", 0.62, Source::Surface, 1.5},
    Rule{"highlight", 0.50, Source::Accent, 0.75},    // selection
    Rule{"highlightedText", 0.97, Source::Surface, 1},
    Rule{"light", 0.42, Source::Surface, 1},          // the bevels of controls
    Rule{"midlight", 0.37, Source::Surface, 1},
    Rule{"mid", 0.39, Source::Surface, 1},
    Rule{"dark", 0.17, Source::Surface, 1},
    Rule{"shadow", 0.12, Source::Surface, 1},
    Rule{"link", 0.84, Source::Accent, 0.8},
    Rule{"linkVisited", 0.80, Source::Accent, 0.7, 50},
    Rule{"toolTip", 0.33, Source::Surface, 1},
    // Disabled.
    Rule{"textDisabled", 0.52, Source::Surface, 1},
    Rule{"buttonDisabled", 0.28, Source::Surface, 0.6},
    Rule{"highlightDisabled", 0.36, Source::Accent, 0.3},
    // The rest.
    Rule{"accent", 0.80, Source::Accent, 1},          // focus, scroll bar handles, marks
    Rule{"textMuted", 0.74, Source::Surface, 2},      // captions and secondary values
    Rule{"raised", 0.31, Source::Surface, 1},         // a dialog over the window
    Rule{"header", 0.28, Source::Surface, 1},         // the header row of a list
    Rule{"surfaceTop", 0.33, Source::Surface, 1.2},   // a card or group: its gradient, top
    Rule{"surfaceBottom", 0.30, Source::Surface, 1.1},
    Rule{"controlTop", 0.37, Source::Surface, 1.2},   // a button: its gradient
    Rule{"controlBottom", 0.32, Source::Surface, 1.2},
    Rule{"defaultButtonTop", 0.48, Source::Accent, 0.65},  // the button that does the job
    Rule{"defaultButtonBottom", 0.42, Source::Accent, 0.6},
    Rule{"defaultButtonEdge", 0.68, Source::Accent, 0.8},
    // Over the image: the window's colour, a little darker, letting the picture through.
    Rule{"panel", 0.22, Source::Surface, 1, 0, 0.90},  // tool panels and the info panel
    Rule{"bubble", 0.22, Source::Surface, 1, 0, 0.82}, // short messages
    Rule{"badge", 0.22, Source::Surface, 1, 0, 0.63},
};

std::uint8_t sample(double linear) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(srgbEncode(linear) * 255), 0L, 255L));
}

bool inGamut(const LinearRgb& c) {
    constexpr double slack = 1e-6;
    return c.r >= -slack && c.r <= 1 + slack && c.g >= -slack && c.g <= 1 + slack && c.b >= -slack && c.b <= 1 + slack;
}

double luminance(Rgba8 c) {
    return 0.2126 * srgbDecode(c.r / 255.0) + 0.7152 * srgbDecode(c.g / 255.0) + 0.0722 * srgbDecode(c.b / 255.0);
}

}  // namespace

Rgba8 fromOklch(double lightness, double chroma, double hue, double alpha) {
    const double angle = hue * std::numbers::pi / 180;
    const auto at = [&](double c) { return oklabToLinear({lightness, c * std::cos(angle), c * std::sin(angle)}); };
    LinearRgb rgb = at(chroma);
    if (!inGamut(rgb)) {
        // The most chroma that fits, by halving; gray always fits.
        double low = 0;
        double high = chroma;
        for (int step = 0; step < 24; ++step) {
            const double mid = (low + high) / 2;
            (inGamut(at(mid)) ? low : high) = mid;
        }
        rgb = at(low);
    }
    return {sample(rgb.r), sample(rgb.g), sample(rgb.b),
            static_cast<std::uint8_t>(std::clamp(std::lround(alpha * 255), 0L, 255L))};
}

std::vector<PaletteRole> derivePalette(const PaletteChoices& choices) {
    std::vector<PaletteRole> palette;
    palette.reserve(rules.size());
    for (const Rule& rule : rules) {
        const double chroma = rule.chroma * (rule.source == Source::Accent ? choices.accentChroma : choices.surfaceChroma);
        palette.push_back({rule.name, fromOklch(rule.lightness, chroma, choices.hue + rule.hueShift, rule.alpha)});
    }
    return palette;
}

const PaletteRole* findRole(const std::vector<PaletteRole>& palette, std::string_view name) {
    const auto found = std::ranges::find(palette, name, &PaletteRole::name);
    return found != palette.end() ? &*found : nullptr;
}

double contrastRatio(Rgba8 a, Rgba8 b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

}  // namespace iv
