#include "core/font_families.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <unordered_set>

namespace iv {

namespace {

// Weights, widths and slants, spelled out and as fonts shorten them. "Text" is a weight in
// IBM Plex, between regular and medium.
constexpr std::array styleWords = {
    "thin",     "hairline",  "extralight", "ultralight", "semilight", "light",          "book",
    "regular",  "normal",    "text",       "medium",     "semibold",  "demibold",       "bold",
    "extrabold", "ultrabold", "heavy",     "black",      "extended",  "expanded",       "semiexpanded",
    "condensed", "semicondensed", "extracondensed", "ultracondensed", "narrow", "compressed",
    "oblique",  "italic",    "lt",         "xlt",        "extlt",     "md",             "medm",
    "smbd",     "smbld",     "sbd",        "bd",         "xbd",       "hv",             "blk",
    "th",       "ex",        "obl",        "it",         "cn",
};

bool isStyleWord(std::string_view word) {
    std::string lower(word);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::find(styleWords.begin(), styleWords.end(), lower) != styleWords.end();
}

}  // namespace

std::vector<std::string> withoutStyleNames(const std::vector<std::string>& families) {
    const std::unordered_set<std::string> all(families.begin(), families.end());
    std::vector<std::string> kept;
    for (const std::string& name : families) {
        // Take style words off the end, one at a time; a family name met on the way is the
        // design this one is a style of.
        bool style = false;
        std::string_view rest = name;
        for (std::size_t space = rest.rfind(' '); space != std::string_view::npos; space = rest.rfind(' ')) {
            if (!isStyleWord(rest.substr(space + 1))) {
                break;
            }
            rest = rest.substr(0, space);
            if (all.contains(std::string(rest))) {
                style = true;
                break;
            }
        }
        if (!style) {
            kept.push_back(name);
        }
    }
    return kept;
}

}  // namespace iv
