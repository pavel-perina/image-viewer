#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

#include "core/color.h"
#include "core/image_buffer.h"
#include "core/image_source.h"

namespace iv {

// Thumbnails kept on disk between runs, so that a folder seen before appears without decoding
// anything. Safe to use from several threads. A cache that cannot be used only costs time, so
// nothing here reports errors: a thumbnail is simply not found, or not kept.
class ThumbnailCache {
public:
    // Everything a thumbnail depends on. The file's time and size say whether it is out of date.
    struct Key {
        std::string path;
        int edge = 0;
        std::string color;
        std::int64_t modified = 0;
        std::uint64_t size = 0;
    };

    // Creates the file if needed, and starts again if it is damaged or of another version.
    // Returns null if it cannot be opened at all. Beyond maxBytes the least recently used go.
    static std::unique_ptr<ThumbnailCache> open(const std::filesystem::path& file, std::uint64_t maxBytes);
    ~ThumbnailCache();

    std::optional<ImageBuffer> find(const Key& key);
    void store(const Key& key, const ImageBuffer& thumbnail);  // expects Bgrx8
    void trim();
    // Changes the budget and trims if necessary. Safe from a worker like other cache calls.
    void setMaxBytes(std::uint64_t maxBytes);

    struct Usage {
        std::uint64_t count = 0;
        std::uint64_t bytes = 0;  // of the thumbnails themselves
    };
    Usage usage();
    // Removes every thumbnail and rewrites the file, so that what they showed, and where they
    // came from, is not left behind in its free space. Copies of the program sharing the file
    // lose theirs too. False if that could not be done.
    bool clear();

private:
    struct Impl;
    explicit ThumbnailCache(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

// makeThumbnail, going through the cache if there is one. Blocks, so call it from a worker.
Result<ImageBuffer> makeCachedThumbnail(ThumbnailCache* cache, const std::filesystem::path& path, int maxEdge,
                                        std::stop_token stop, const ColorTarget& target = {});

}  // namespace iv
