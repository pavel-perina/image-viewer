#pragma once

// Fonts that install each style under a name of its own ("Iosevka Extralight Extended
// Oblique", "IBM Plex Sans SmBld") list as dozens of families. This keeps one entry per
// design: a name is left out when it is another family's name followed by style words alone.
// "Iosevka Term" and "PT Sans Caption" stay, as Term and Caption are not styles.

#include <string>
#include <vector>

namespace iv {

// The families, in the order given, without those that are another of them plus style words.
std::vector<std::string> withoutStyleNames(const std::vector<std::string>& families);

}  // namespace iv
