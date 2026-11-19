#pragma once

// The picture every format test writes, and the checks they share: that it opens, shows the
// right colours in the right places, keeps or lacks alpha, and survives damage.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "core/image_details.h"
#include "core/image_ops.h"
#include "core/image_source.h"
#include "test_jpeg.h"

namespace iv::test::formats {

using Bytes = std::vector<std::uint8_t>;

constexpr int kWidth = 64;
constexpr int kHeight = 48;

// Left half red-ish, right half blue-ish. The bottom right is transparent, below four rows
// that are half transparent.
constexpr int kHalfRows = 4;
inline void pixelAt(int x, int y, std::array<std::uint8_t, 4>& rgba) {
    rgba = x < kWidth / 2 ? std::array<std::uint8_t, 4>{200, 40, 20, 255} : std::array<std::uint8_t, 4>{20, 40, 200, 255};
    if (x >= kWidth / 2 && y >= kHeight / 2) {
        rgba[3] = y < kHeight / 2 + kHalfRows ? 128 : 0;
    }
}

// A folder for this process, since ctest runs tests in parallel, removed when it ends.
inline const std::filesystem::path& testFolder() {
    struct Folder {
        std::filesystem::path path = std::filesystem::temp_directory_path() / ("iv_format_" + std::to_string(std::random_device{}()));
        Folder() { std::filesystem::create_directories(path); }
        ~Folder() { std::filesystem::remove_all(path); }
    };
    static const Folder folder;
    return folder.path;
}

inline std::filesystem::path write(const std::string& name, const Bytes& bytes) {
    const auto path = testFolder() / name;
    test::writeFile(path, bytes);
    return path;
}

inline std::shared_ptr<ImageSource> open(const std::filesystem::path& path) {
    auto source = openImageSource(path);
    EXPECT_TRUE(source.has_value()) << path << ": " << (source ? "" : source.error());
    return source ? *source : nullptr;
}

inline std::array<int, 3> shown(ImageSource& source, int x, int y) {
    const SubimageInfo& info = source.subimages()[0];
    const auto tile = source.readTile({0, 0, 0, 0}, {});
    if (!tile) {
        ADD_FAILURE() << tile.error();
        return {};
    }
    const ImageBuffer display = toDisplay(*tile, fullRange(info.format));
    const auto* p = reinterpret_cast<const std::uint8_t*>(display.row(y)) + static_cast<std::size_t>(x) * 4;
    return {p[2], p[1], p[0]};
}

inline void expectNear(const std::array<int, 3>& actual, const std::array<int, 3>& expected, int tolerance);

// A pixel as decoded, before anything is done for showing it: red, green, blue and the fourth
// channel, each brought to 0 to 255.
inline std::array<int, 4> decoded(ImageSource& source, int x, int y) {
    const auto tile = source.readTile({0, 0, 0, 0}, {});
    if (!tile) {
        ADD_FAILURE() << tile.error();
        return {};
    }
    const std::byte* p = tile->row(y) + static_cast<std::size_t>(x) * static_cast<std::size_t>(bytesPerPixel(tile->format()));
    switch (tile->format()) {
    case PixelFormat::Bgrx8: {
        const auto* b = reinterpret_cast<const std::uint8_t*>(p);
        return {b[2], b[1], b[0], b[3]};
    }
    case PixelFormat::Rgba16: {
        const auto* w = reinterpret_cast<const std::uint16_t*>(p);
        return {(w[0] + 128) / 257, (w[1] + 128) / 257, (w[2] + 128) / 257, (w[3] + 128) / 257};
    }
    case PixelFormat::Rgba32F: {
        const auto* f = reinterpret_cast<const float*>(p);
        const auto byte = [](float v) { return static_cast<int>(std::lround(v * 255)); };
        return {byte(f[0]), byte(f[1]), byte(f[2]), byte(f[3])};
    }
    default:
        ADD_FAILURE() << "a format without a fourth channel";
        return {};
    }
}

// What a format's alpha test promises. The decoded pixels are compared with the picture the
// test wrote, pixelAt: the half transparent rows keep their own colour, not one mixed with a
// background or multiplied by alpha, and their alpha; the transparent part has alpha 0. Its
// colour is not checked, as an encoder may drop what cannot be seen. The colour tolerance is
// for lossy formats.
inline void expectAlphaKept(ImageSource& source, int colourTolerance) {
    EXPECT_TRUE(source.subimages()[0].alpha);
    const auto tile = source.readTile({0, 0, 0, 0}, {});
    ASSERT_TRUE(tile);
    EXPECT_TRUE(tile->hasAlpha());
    const auto half = decoded(source, kWidth - 8, kHeight / 2 + 1);
    expectNear({half[0], half[1], half[2]}, {20, 40, 200}, colourTolerance);
    EXPECT_NEAR(half[3], 128, 1);
    EXPECT_EQ(decoded(source, kWidth - 8, kHeight - 8)[3], 0);
    EXPECT_EQ(decoded(source, 8, 8)[3], 255);
}

// A file without alpha says so, and its tiles too, whatever their fourth channel holds.
inline void expectNoAlpha(ImageSource& source) {
    EXPECT_FALSE(source.subimages()[0].alpha);
    const auto tile = source.readTile({0, 0, 0, 0}, {});
    ASSERT_TRUE(tile);
    EXPECT_FALSE(tile->hasAlpha());
}

inline void expectNear(const std::array<int, 3>& actual, const std::array<int, 3>& expected, int tolerance) {
    EXPECT_NEAR(actual[0], expected[0], tolerance);
    EXPECT_NEAR(actual[1], expected[1], tolerance);
    EXPECT_NEAR(actual[2], expected[2], tolerance);
}

// Checks the picture: not swapped, not flipped, not shifted. Allows for lossy compression.
inline void expectPicture(ImageSource& source, int tolerance) {
    expectNear(shown(source, 8, 8), {200, 40, 20}, tolerance);
    expectNear(shown(source, kWidth - 8, 8), {20, 40, 200}, tolerance);
    expectNear(shown(source, 8, kHeight - 8), {200, 40, 20}, tolerance);
}

inline std::string valueOf(const std::vector<DetailRow>& rows, const std::string& label) {
    const auto it = std::ranges::find(rows, label, &DetailRow::label);
    return it == rows.end() ? std::string() : it->value;
}

// Random damage and truncation: any outcome is fine except a crash or a hang.
inline void survivesDamage(const std::string& name, const Bytes& bytes) {
    const auto path = testFolder() / ("damaged_" + name);
    const auto attempt = [&](const Bytes& damaged) {
        test::writeFile(path, damaged);
        auto source = openImageSource(path);
        if (source) {
            (void)(*source)->readTile({0, 0, 0, 0}, {});
        }
    };
    for (std::size_t at = 0; at < bytes.size(); at += std::max<std::size_t>(1, bytes.size() / 60)) {
        Bytes copy = bytes;
        for (std::size_t i = 0; i < 4 && at + i < copy.size(); ++i) {
            copy[at + i] = static_cast<std::uint8_t>(0xA5 ^ i);
        }
        attempt(copy);
        attempt(Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(at)));
    }
}


}  // namespace iv::test::formats
