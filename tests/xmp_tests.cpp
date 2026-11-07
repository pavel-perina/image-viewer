#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/xmp.h"

namespace iv {
namespace {

std::string reset(const std::string& packet) {
    std::vector<std::uint8_t> bytes(packet.begin(), packet.end());
    resetXmpOrientation(bytes);
    return {bytes.begin(), bytes.end()};
}

TEST(Xmp, ResetsTheOrientationAttribute) {
    EXPECT_EQ(reset(R"(<rdf:Description tiff:Orientation="6" tiff:Make="X"/>)"),
              R"(<rdf:Description tiff:Orientation="1" tiff:Make="X"/>)");
    EXPECT_EQ(reset("<rdf:Description\n   tiff:Orientation = '8'/>"), "<rdf:Description\n   tiff:Orientation = '1'/>");
}

TEST(Xmp, ResetsTheOrientationElement) {
    EXPECT_EQ(reset("<tiff:Orientation>3</tiff:Orientation>"), "<tiff:Orientation>1</tiff:Orientation>");
    EXPECT_EQ(reset("<tiff:Orientation>\n 5 \n</tiff:Orientation>"), "<tiff:Orientation>\n 1 \n</tiff:Orientation>");
}

TEST(Xmp, LeavesEverythingElseAlone) {
    for (const std::string packet : {
             R"(<rdf:Description tiff:Orientation="16"/>)",       // not one digit
             R"(<rdf:Description tiff:Orientation="x"/>)",
             R"(<rdf:Description xtiff:Orientation="6"/>)",       // another name
             R"(<rdf:Description tiff:OrientationX="6"/>)",
             "<tiff:Orientation>6 7</tiff:Orientation>",
             R"(<rdf:Description exif:PixelXDimension="6"/>)",
             R"(tiff:Orientation="6")",                           // nothing before it
             R"(<rdf:Description tiff:Orientation="6)",           // cut short
             "<rdf:Description tiff:Orientation",
         }) {
        EXPECT_EQ(reset(packet), packet);
    }
}

}  // namespace
}  // namespace iv
