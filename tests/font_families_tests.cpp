// Folding the families a font installs per style into one entry (core/font_families.h). Input:
// a made-up list shaped like what fontconfig gives for Iosevka, IBM Plex and others. The truth
// is the list kept: one entry per design, in the order given.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/font_families.h"

using namespace iv;

// Why: some fonts list every weight, width and slant as a family of its own, which buries the
// rest. Those go; designs of their own (Term, Fixed, Caption, Display) stay.
TEST(FontFamilies, StyleNamesFoldIntoTheirDesign) {
    const std::vector<std::string> listed = {
        "Inter", "Inter Display", "Iosevka", "Iosevka Extralight Extended Oblique", "Iosevka Fixed",
        "Iosevka Fixed Hv Ex Obl", "Iosevka Light", "Iosevka Term", "IBM Plex Sans", "IBM Plex Sans SmBld",
        "IBM Plex Sans Text", "Noto Sans", "Noto Sans SemiCondensed", "PT Sans", "PT Sans Caption"};
    EXPECT_EQ(withoutStyleNames(listed),
              (std::vector<std::string>{"Inter", "Inter Display", "Iosevka", "Iosevka Fixed", "Iosevka Term",
                                        "IBM Plex Sans", "Noto Sans", "PT Sans", "PT Sans Caption"}));
}

// Why: a style name stays when there is no family it is a style of, or the font could not be
// chosen at all.
TEST(FontFamilies, AStyleNameWithoutItsDesignStays) {
    EXPECT_EQ(withoutStyleNames({"Lonely Bold", "Other"}), (std::vector<std::string>{"Lonely Bold", "Other"}));
    EXPECT_EQ(withoutStyleNames({"Bold"}), (std::vector<std::string>{"Bold"})) << "a name that is one style word";
}
