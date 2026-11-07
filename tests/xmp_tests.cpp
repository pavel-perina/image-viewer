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

std::string withoutLocation(const std::string& packet) {
    std::vector<std::uint8_t> bytes(packet.begin(), packet.end());
    removeXmpLocation(bytes);
    return {bytes.begin(), bytes.end()};
}

TEST(Xmp, RemovesTheLocationAttributes) {
    const std::string packet = R"(<rdf:Description exif:GPSLatitude="49,11.51N" xmp:Rating="4")"
                               "\n exif:GPSLongitude = '16,36.3E' exif:GPSAltitude=\"2345/10\"/>";
    const std::string result = withoutLocation(packet);
    EXPECT_EQ(result.size(), packet.size());
    EXPECT_EQ(result.find("GPS"), std::string::npos);
    EXPECT_EQ(result.find("49"), std::string::npos);
    EXPECT_EQ(result.find("2345"), std::string::npos);
    EXPECT_NE(result.find(R"( xmp:Rating="4")"), std::string::npos);
    EXPECT_TRUE(result.starts_with("<rdf:Description "));
    EXPECT_TRUE(result.ends_with(" />"));
}

TEST(Xmp, RemovesTheLocationElements) {
    const std::string packet = "<rdf:Description><exif:GPSLatitude>49,11.51N</exif:GPSLatitude>"
                               "<xmp:Rating>4</xmp:Rating><exif:GPSVersionID/></rdf:Description>";
    const std::string result = withoutLocation(packet);
    EXPECT_EQ(result.size(), packet.size());
    EXPECT_EQ(result.find("GPS"), std::string::npos);
    EXPECT_EQ(result.find("49"), std::string::npos);
    EXPECT_NE(result.find("<xmp:Rating>4</xmp:Rating>"), std::string::npos);
    EXPECT_TRUE(result.starts_with("<rdf:Description> "));
    EXPECT_TRUE(result.ends_with(" </rdf:Description>"));
}

TEST(Xmp, LeavesWhatIsNotLocationAlone) {
    for (const std::string packet : {
             R"(<rdf:Description photoshop:City="Brno"/>)",
             R"(<rdf:Description exif:GPSLatitude="49)",  // cut short
             "<exif:GPSLatitude>49",
         }) {
        EXPECT_EQ(withoutLocation(packet), packet);
    }
}

}  // namespace
}  // namespace iv
