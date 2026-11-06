#include "core/whole_image_source.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace iv {

namespace {

constexpr int kTileSize = 2048;
constexpr std::uint64_t kMaxDecodedBytes = 8ull * 1024 * 1024 * 1024;

class WholeImageSource final : public ImageSource {
public:
    WholeImageSource(SubimageInfo info, Size size, WholeImageDecoder decoder) : m_decoder(std::move(decoder)) {
        info.levels = {{size, {kTileSize, kTileSize}}};
        m_subimages.push_back(std::move(info));
    }

    std::span<const SubimageInfo> subimages() const override { return m_subimages; }

    Result<ImageBuffer> readTile(const TileCoord& coord, std::stop_token stop) override {
        const LevelInfo& level = m_subimages[0].levels[0];
        const std::int64_t x = static_cast<std::int64_t>(coord.col) * level.tileSize.width;
        const std::int64_t y = static_cast<std::int64_t>(coord.row) * level.tileSize.height;
        if (coord.subimage != 0 || coord.level != 0 || coord.col < 0 || coord.row < 0 || x >= level.size.width ||
            y >= level.size.height) {
            return std::unexpected("tile does not exist");
        }
        const std::scoped_lock lock(m_mutex);
        if (!m_error.empty()) {
            return std::unexpected(m_error);
        }
        if (m_decoder) {
            auto decoded = m_decoder(stop);
            if (!decoded) {
                if (decoded.error() != "cancelled") {
                    m_error = decoded.error();  // every other tile would fail the same way
                }
                return std::unexpected(decoded.error());
            }
            m_pixels = std::move(*decoded);
            m_decoder = nullptr;  // releases the compressed file it holds
        }
        return m_pixels.view(static_cast<int>(x), static_cast<int>(y),
                             std::min<int>(level.tileSize.width, level.size.width - static_cast<int>(x)),
                             std::min<int>(level.tileSize.height, level.size.height - static_cast<int>(y)));
    }

private:
    std::vector<SubimageInfo> m_subimages;
    std::mutex m_mutex;
    WholeImageDecoder m_decoder;
    ImageBuffer m_pixels;
    std::string m_error;
};

}  // namespace

std::shared_ptr<ImageSource> makeWholeImageSource(SubimageInfo info, Size size, WholeImageDecoder decoder) {
    return std::make_shared<WholeImageSource>(std::move(info), size, std::move(decoder));
}

bool fitsInMemoryLimit(Size size, PixelFormat format) {
    if (size.width <= 0 || size.height <= 0) {
        return false;
    }
    return static_cast<std::uint64_t>(size.width) * size.height * bytesPerPixel(format) <= kMaxDecodedBytes;
}

}  // namespace iv
