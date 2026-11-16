#include "core/overview_source.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

#include "core/image_ops.h"

namespace iv {

namespace {

// The smallest level of every subimage ends up no larger than this on its long edge. Going
// further than the GPU needs also keeps zooming far out from skipping pixels.
constexpr int kMaxCoarsestEdge = 1024;
constexpr int kTileSize = 2048;
constexpr std::uint64_t kMaxOverviewBytes = 2ull * 1024 * 1024 * 1024;

struct Overviews {
    std::mutex mutex;
    std::vector<ImageBuffer> levels;  // finest first; empty until built
};

bool needsOverviews(const SubimageInfo& subimage) {
    const Size coarsest = subimage.levels.back().size;
    return std::max(coarsest.width, coarsest.height) > kMaxCoarsestEdge;
}

// Halves down to the limit, then drops the largest levels that do not fit the memory budget.
std::vector<Size> planOverviews(Size coarsest, PixelFormat format) {
    std::vector<Size> sizes;
    Size size = coarsest;
    while (std::max(size.width, size.height) > kMaxCoarsestEdge) {
        size = halvedSize(size);
        sizes.push_back(size);
    }
    std::uint64_t total = 0;
    std::size_t keepFrom = sizes.size();
    while (keepFrom > 0) {
        const Size candidate = sizes[keepFrom - 1];
        const auto bytes = static_cast<std::uint64_t>(candidate.width) * candidate.height * bytesPerPixel(format);
        if (total + bytes > kMaxOverviewBytes) {
            break;
        }
        total += bytes;
        --keepFrom;
    }
    sizes.erase(sizes.begin(), sizes.begin() + static_cast<std::ptrdiff_t>(keepFrom));
    return sizes;
}

class OverviewSource final : public ImageSource {
public:
    explicit OverviewSource(std::shared_ptr<ImageSource> base) : m_base(std::move(base)) {
        const auto subimages = m_base->subimages();
        m_subimages.assign(subimages.begin(), subimages.end());
        m_overviews.resize(m_subimages.size());
        m_realLevels.reserve(m_subimages.size());
        for (std::size_t i = 0; i < m_subimages.size(); ++i) {
            SubimageInfo& subimage = m_subimages[i];
            m_realLevels.push_back(static_cast<int>(subimage.levels.size()));
            if (needsOverviews(subimage)) {
                m_overviews[i] = std::make_unique<Overviews>();
                for (const Size size : planOverviews(subimage.levels.back().size, subimage.format)) {
                    subimage.levels.push_back({size, {kTileSize, kTileSize}, true});
                }
            }
        }
    }

    std::span<const SubimageInfo> subimages() const override { return m_subimages; }

    Result<ImageBuffer> roughLevel(int subimage, Size size, const std::stop_token& stop) override {
        return m_base->roughLevel(subimage, size, stop);
    }

    Result<ImageBuffer> readTile(const TileCoord& coord, std::stop_token stop) override {
        if (coord.subimage < 0 || coord.subimage >= std::ssize(m_subimages)) {
            return std::unexpected("tile does not exist");
        }
        const auto index = static_cast<std::size_t>(coord.subimage);
        const int realLevels = m_realLevels[index];
        if (coord.level < realLevels) {
            return m_base->readTile(coord, stop);
        }

        const auto& levels = m_subimages[index].levels;
        if (coord.level >= std::ssize(levels)) {
            return std::unexpected("tile does not exist");
        }
        const LevelInfo& level = levels[static_cast<std::size_t>(coord.level)];
        const int x = coord.col * level.tileSize.width;
        const int y = coord.row * level.tileSize.height;
        if (coord.col < 0 || coord.row < 0 || x >= level.size.width || y >= level.size.height) {
            return std::unexpected("tile does not exist");
        }

        const auto built = build(coord.subimage, stop);
        if (!built) {
            return std::unexpected(built.error());
        }
        const ImageBuffer& whole = (*built)[static_cast<std::size_t>(coord.level - realLevels)];
        return whole.view(x, y, std::min(level.tileSize.width, level.size.width - x),
                          std::min(level.tileSize.height, level.size.height - y));
    }

private:
    Result<std::vector<ImageBuffer>> build(int subimageIndex, const std::stop_token& stop) {
        const auto index = static_cast<std::size_t>(subimageIndex);
        Overviews& overviews = *m_overviews[index];
        const std::scoped_lock lock(overviews.mutex);
        if (!overviews.levels.empty()) {
            return overviews.levels;
        }

        const SubimageInfo& subimage = m_subimages[index];
        const int realLevels = m_realLevels[index];
        const LevelInfo& base = subimage.levels[static_cast<std::size_t>(realLevels - 1)];
        std::vector<ImageBuffer> built;
        try {
            const ImageBuffer finest =
                ImageBuffer::create(subimage.levels[static_cast<std::size_t>(realLevels)].size, subimage.format).withAlpha(subimage.alpha);
            std::memset(finest.data(), 0, finest.stride() * static_cast<std::size_t>(finest.height()));
            // A budget that dropped the largest levels leaves more than one halving to do at once.
            const auto scanned = halvedSize(base.size) == finest.size()
                                     ? halveBase(subimageIndex, realLevels - 1, base, finest, stop)
                                     : scanBase(subimageIndex, realLevels - 1, base, finest, stop);
            if (!scanned) {
                return std::unexpected(scanned.error());
            }
            built.push_back(finest);
            for (std::size_t i = static_cast<std::size_t>(realLevels) + 1; i < subimage.levels.size(); ++i) {
                built.push_back(halve(built.back()));
            }
        } catch (const std::bad_alloc&) {
            return std::unexpected("not enough memory for overview");
        }
        overviews.levels = std::move(built);
        return overviews.levels;
    }

    // The weights reach past the edges of tiles, so whole rows of tiles are read into a strip,
    // and the last rows of one stay in it for the next.
    Result<bool> halveBase(int subimage, int levelIndex, const LevelInfo& base, const ImageBuffer& target,
                           const std::stop_token& stop) {
        constexpr int kKeptRows = 4;
        const int bytes = bytesPerPixel(target.format());
        const int rows = (base.size.height + base.tileSize.height - 1) / base.tileSize.height;
        const int cols = (base.size.width + base.tileSize.width - 1) / base.tileSize.width;
        const ImageBuffer strip = ImageBuffer::create({base.size.width, base.tileSize.height + kKeptRows}, target.format());
        std::string firstError;
        bool anyRead = false;
        int stripY = 0;
        int filled = 0;
        int targetRow = 0;
        for (int row = 0; row < rows; ++row) {
            const int tileRow = row * base.tileSize.height;
            const int height = std::min(base.tileSize.height, base.size.height - tileRow);
            for (int y = filled; y < filled + height; ++y) {
                std::memset(strip.row(y), 0, static_cast<std::size_t>(base.size.width) * bytes);  // a damaged tile stays black
            }
            for (int col = 0; col < cols; ++col) {
                auto tile = m_base->readTile({subimage, levelIndex, col, row}, stop);
                if (tile) {
                    anyRead = true;
                    const int x = col * base.tileSize.width;
                    const int copied = std::min(tile->width(), base.size.width - x);
                    for (int y = 0; y < std::min(tile->height(), height); ++y) {
                        std::memcpy(reinterpret_cast<std::uint8_t*>(strip.row(filled + y)) + static_cast<std::size_t>(x) * bytes,
                                    tile->row(y), static_cast<std::size_t>(copied) * bytes);
                    }
                } else if (tile.error() == "cancelled") {
                    return std::unexpected(tile.error());
                } else if (firstError.empty()) {
                    firstError = tile.error();
                }
            }
            filled += height;

            // A target row can be made once the last source row it reads, 2y + 2, has arrived.
            const int arrived = stripY + filled;
            const int usableEnd = arrived >= base.size.height ? target.height() : std::max(targetRow, (arrived - 1) / 2);
            if (usableEnd > targetRow) {
                halveRows(strip.view(0, 0, base.size.width, filled), stripY, base.size.height, target, targetRow, usableEnd);
                targetRow = usableEnd;
            }
            const int keepFrom = std::max(2 * targetRow - 1, stripY);
            const int kept = arrived - keepFrom;
            for (int y = 0; y < kept; ++y) {
                std::memmove(strip.row(y), strip.row(keepFrom - stripY + y), static_cast<std::size_t>(base.size.width) * bytes);
            }
            stripY = keepFrom;
            filled = kept;
        }
        if (!anyRead) {
            return std::unexpected(firstError.empty() ? std::string("cannot read image") : firstError);
        }
        return true;
    }

    Result<bool> scanBase(int subimage, int levelIndex, const LevelInfo& base, const ImageBuffer& target,
                          const std::stop_token& stop) {
        const int cols = (base.size.width + base.tileSize.width - 1) / base.tileSize.width;
        const int rows = (base.size.height + base.tileSize.height - 1) / base.tileSize.height;
        std::string firstError;
        bool anyRead = false;
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                auto tile = m_base->readTile({subimage, levelIndex, col, row}, stop);
                if (tile) {
                    anyRead = true;
                    shrinkBlockInto(*tile, col * base.tileSize.width, row * base.tileSize.height, base.size, target);
                } else if (tile.error() == "cancelled") {
                    return std::unexpected(tile.error());
                } else if (firstError.empty()) {
                    firstError = tile.error();  // a damaged tile stays black; the rest is still useful
                }
            }
        }
        if (!anyRead) {
            return std::unexpected(firstError.empty() ? std::string("cannot read image") : firstError);
        }
        return true;
    }

    std::shared_ptr<ImageSource> m_base;
    std::vector<SubimageInfo> m_subimages;
    std::vector<int> m_realLevels;
    std::vector<std::unique_ptr<Overviews>> m_overviews;
};

}  // namespace

std::shared_ptr<ImageSource> withOverviews(std::shared_ptr<ImageSource> source) {
    const auto subimages = source->subimages();
    if (std::ranges::none_of(subimages, needsOverviews)) {
        return source;
    }
    return std::make_shared<OverviewSource>(std::move(source));
}

}  // namespace iv
