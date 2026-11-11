#pragma once

#include <string>
#include <vector>

namespace iv {

// A library the program is built with, for the About dialog and for bug reports.
struct Library {
    std::string name;
    std::string version;  // as loaded, where the library can tell
    std::string licence;
    std::string url;
};

// The image and colour libraries, only the formats this build has.
std::vector<Library> libraries();

}  // namespace iv
