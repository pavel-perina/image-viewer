#include "core/thumbnail_cache.h"

#include <algorithm>
#include <chrono>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <system_error>
#include <vector>

#ifdef IV_HAVE_JXL
#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#include <jxl/encode.h>
#include <jxl/encode_cxx.h>
#endif
#include <jpeglib.h>
#include <sqlite3.h>

#include "core/thumbnail.h"

namespace iv {

namespace {

// Raise when the schema changes; the old file is then replaced.
constexpr int kSchemaVersion = 1;
// Raise when thumbnails would come out differently, so that old ones are made again.
constexpr int kFilterVersion = 1;
constexpr int kJpegQuality = 92;
#ifdef IV_HAVE_JXL
// About as large as JPEG at the quality above, with about 40% less RMS error.
constexpr float kJxlDistance = 0.5F;
constexpr int kJxlEffort = 3;
#endif
constexpr std::int64_t kSecondsPerDay = 24 * 60 * 60;
constexpr int kStoresBetweenTrims = 1024;
constexpr double kTrimTarget = 0.8;

std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

struct JpegError {
    jpeg_error_mgr base;
    std::jmp_buf jump;
};

[[noreturn]] void onJpegError(j_common_ptr cinfo) {
    std::longjmp(reinterpret_cast<JpegError*>(cinfo->err)->jump, 1);
}

// What longjmp has to find intact lives on the heap, as a local variable changed after setjmp
// has no defined value afterwards.
// What libjpeg allocates for its output, freed once: not to be copied.
struct JpegOutput {
    unsigned char* data = nullptr;
    unsigned long size = 0;
    JpegOutput() = default;
    JpegOutput(const JpegOutput&) = delete;
    JpegOutput& operator=(const JpegOutput&) = delete;
    ~JpegOutput() { std::free(data); }
};

// Thumbnails are stored as JPEG only by builds without JPEG XL.
[[maybe_unused]] std::vector<unsigned char> encodeJpeg(const ImageBuffer& image) {
    const auto output = std::make_unique<JpegOutput>();
    jpeg_compress_struct cinfo{};
    JpegError errors{};
    cinfo.err = jpeg_std_error(&errors.base);
    errors.base.error_exit = onJpegError;
    if (setjmp(errors.jump)) {
        jpeg_destroy_compress(&cinfo);
        return {};
    }
    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, &output->data, &output->size);
    cinfo.image_width = static_cast<JDIMENSION>(image.width());
    cinfo.image_height = static_cast<JDIMENSION>(image.height());
    cinfo.input_components = 4;
    cinfo.in_color_space = JCS_EXT_BGRX;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, kJpegQuality, TRUE);
    // The sharp detail of a small image is exactly what chroma subsampling would blur.
    for (int c = 0; c < cinfo.num_components; ++c) {
        cinfo.comp_info[c].h_samp_factor = 1;
        cinfo.comp_info[c].v_samp_factor = 1;
    }
    jpeg_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline < cinfo.image_height) {
        auto* row = reinterpret_cast<JSAMPROW>(image.row(static_cast<int>(cinfo.next_scanline)));
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    return {output->data, output->data + output->size};
}

std::optional<ImageBuffer> decodeJpeg(const void* bytes, int size) {
    const auto image = std::make_unique<ImageBuffer>();
    jpeg_decompress_struct cinfo{};
    JpegError errors{};
    cinfo.err = jpeg_std_error(&errors.base);
    errors.base.error_exit = onJpegError;
    // libjpeg only warns about missing data and fills it with grey, but these files are our own, so
    // it means the entry is damaged.
    errors.base.emit_message = [](j_common_ptr info, int level) {
        if (level < 0) {
            onJpegError(info);
        }
    };
    if (setjmp(errors.jump)) {
        jpeg_destroy_decompress(&cinfo);
        return std::nullopt;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, static_cast<const unsigned char*>(bytes), static_cast<unsigned long>(size));
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_EXT_BGRX;
    jpeg_start_decompress(&cinfo);
    try {
        *image = ImageBuffer::create({static_cast<int>(cinfo.output_width), static_cast<int>(cinfo.output_height)}, PixelFormat::Bgrx8);
    } catch (const std::bad_alloc&) {
        jpeg_destroy_decompress(&cinfo);
        return std::nullopt;
    }
    while (cinfo.output_scanline < cinfo.output_height) {
        auto* row = reinterpret_cast<JSAMPROW>(image->row(static_cast<int>(cinfo.output_scanline)));
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return *image;
}

#ifdef IV_HAVE_JXL
std::vector<unsigned char> encodeJxl(const ImageBuffer& image) {
    const auto encoder = JxlEncoderMake(nullptr);
    JxlBasicInfo info;
    JxlEncoderInitBasicInfo(&info);
    info.xsize = static_cast<std::uint32_t>(image.width());
    info.ysize = static_cast<std::uint32_t>(image.height());
    info.bits_per_sample = 8;
    info.num_color_channels = 3;
    JxlColorEncoding color;
    JxlColorEncodingSetToSRGB(&color, JXL_FALSE);
    if (JxlEncoderSetBasicInfo(encoder.get(), &info) != JXL_ENC_SUCCESS ||
        JxlEncoderSetColorEncoding(encoder.get(), &color) != JXL_ENC_SUCCESS) {
        return {};
    }
    JxlEncoderFrameSettings* settings = JxlEncoderFrameSettingsCreate(encoder.get(), nullptr);
    JxlEncoderSetFrameDistance(settings, kJxlDistance);
    JxlEncoderFrameSettingsSetOption(settings, JXL_ENC_FRAME_SETTING_EFFORT, kJxlEffort);

    std::vector<unsigned char> pixels(static_cast<std::size_t>(image.width()) * image.height() * 3);
    unsigned char* out = pixels.data();
    for (int y = 0; y < image.height(); ++y) {
        const auto* in = reinterpret_cast<const unsigned char*>(image.row(y));
        for (int x = 0; x < image.width(); ++x, in += 4) {
            *out++ = in[2];
            *out++ = in[1];
            *out++ = in[0];
        }
    }
    const JxlPixelFormat format{3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    if (JxlEncoderAddImageFrame(settings, &format, pixels.data(), pixels.size()) != JXL_ENC_SUCCESS) {
        return {};
    }
    JxlEncoderCloseInput(encoder.get());

    std::vector<unsigned char> bytes(pixels.size() / 2 + 1024);
    std::size_t used = 0;
    for (;;) {
        unsigned char* next = bytes.data() + used;
        std::size_t available = bytes.size() - used;
        const JxlEncoderStatus status = JxlEncoderProcessOutput(encoder.get(), &next, &available);
        used = bytes.size() - available;
        if (status == JXL_ENC_SUCCESS) {
            bytes.resize(used);
            return bytes;
        }
        if (status != JXL_ENC_NEED_MORE_OUTPUT) {
            return {};
        }
        bytes.resize(bytes.size() * 2);
    }
}

std::optional<ImageBuffer> decodeJxl(const void* bytes, int size) {
    const auto decoder = JxlDecoderMake(nullptr);
    if (JxlDecoderSubscribeEvents(decoder.get(), JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS ||
        JxlDecoderSetInput(decoder.get(), static_cast<const std::uint8_t*>(bytes), static_cast<std::size_t>(size)) != JXL_DEC_SUCCESS) {
        return std::nullopt;
    }
    JxlDecoderCloseInput(decoder.get());
    const JxlPixelFormat format{3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    JxlBasicInfo info{};
    std::vector<unsigned char> pixels;
    for (;;) {
        switch (JxlDecoderProcessInput(decoder.get())) {
        case JXL_DEC_BASIC_INFO:
            if (JxlDecoderGetBasicInfo(decoder.get(), &info) != JXL_DEC_SUCCESS) {
                return std::nullopt;
            }
            break;
        case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
            pixels.resize(static_cast<std::size_t>(info.xsize) * info.ysize * 3);
            if (JxlDecoderSetImageOutBuffer(decoder.get(), &format, pixels.data(), pixels.size()) != JXL_DEC_SUCCESS) {
                return std::nullopt;
            }
            break;
        case JXL_DEC_FULL_IMAGE:
            break;
        case JXL_DEC_SUCCESS: {
            if (pixels.empty()) {
                return std::nullopt;
            }
            const ImageBuffer image = ImageBuffer::create({static_cast<int>(info.xsize), static_cast<int>(info.ysize)}, PixelFormat::Bgrx8);
            const unsigned char* in = pixels.data();
            for (int y = 0; y < image.height(); ++y) {
                auto* out = reinterpret_cast<unsigned char*>(image.row(y));
                for (int x = 0; x < image.width(); ++x, in += 3, out += 4) {
                    out[0] = in[2];
                    out[1] = in[1];
                    out[2] = in[0];
                    out[3] = 255;
                }
            }
            return image;
        }
        default:
            return std::nullopt;
        }
    }
}
#endif

// Each entry says by its first bytes what it is, so a file can hold both kinds, and a kind this
// build cannot read is simply not found, and made again.
std::vector<unsigned char> encodeThumbnail(const ImageBuffer& image) {
#ifdef IV_HAVE_JXL
    return encodeJxl(image);
#else
    return encodeJpeg(image);
#endif
}

std::optional<ImageBuffer> decodeThumbnail(const void* bytes, int size) {
#ifdef IV_HAVE_JXL
    if (size > 0 && JxlSignatureCheck(static_cast<const std::uint8_t*>(bytes), static_cast<std::size_t>(size)) >= JXL_SIG_CODESTREAM) {
        try {
            return decodeJxl(bytes, size);
        } catch (const std::bad_alloc&) {
            return std::nullopt;
        }
    }
#endif
    return decodeJpeg(bytes, size);
}

// SQLite wants UTF-8 on every system, while string() gives the system's own encoding.
std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

void closeStatement(sqlite3_stmt* statement) { sqlite3_finalize(statement); }
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&closeStatement)>;

Statement prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &statement, nullptr);
    return {statement, closeStatement};
}

void closeDatabase(sqlite3* db) { sqlite3_close(db); }
using Database = std::unique_ptr<sqlite3, decltype(&closeDatabase)>;

struct Opened {
    Database db{nullptr, closeDatabase};
    // Not a cache this program can use: damaged, not a database, or another version. Other
    // failures, such as another running copy holding a lock, must not lose that copy's file.
    bool replaceable = false;
};

bool isDamage(int code) {
    const int primary = code & 0xff;
    return primary == SQLITE_NOTADB || primary == SQLITE_CORRUPT;
}

Opened openDatabase(const std::filesystem::path& file) {
    sqlite3* raw = nullptr;
    const int opened = sqlite3_open_v2(utf8(file).c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr);
    Database db(raw, closeDatabase);
    if (opened != SQLITE_OK) {
        return {.replaceable = isDamage(opened)};
    }
    sqlite3_busy_timeout(db.get(), 2000);  // another running copy of the program
    int version = -1;
    {
        const Statement query = prepare(db.get(), "PRAGMA user_version");
        const int step = query ? sqlite3_step(query.get()) : sqlite3_errcode(db.get());
        if (step != SQLITE_ROW) {
            return {.replaceable = isDamage(step)};
        }
        version = sqlite3_column_int(query.get(), 0);
    }
    // A newer version is left to the copy of the program that made it.
    if (version > kSchemaVersion) {
        return {};
    }
    if (version != 0 && version != kSchemaVersion) {
        return {.replaceable = true};
    }
    // Losing the last thumbnails in a power cut does not matter, and waiting for the disk would.
    const std::string setup = std::string(
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "CREATE TABLE IF NOT EXISTS thumbnails("
        "  path TEXT NOT NULL, edge INTEGER NOT NULL, color TEXT NOT NULL,"
        "  filter INTEGER NOT NULL, modified INTEGER NOT NULL, size INTEGER NOT NULL,"
        "  last_used INTEGER NOT NULL, bytes BLOB NOT NULL,"
        "  PRIMARY KEY(path, edge, color));"
        "CREATE INDEX IF NOT EXISTS by_use ON thumbnails(last_used);"
        "PRAGMA user_version=") + std::to_string(kSchemaVersion) + ";";
    if (const int result = sqlite3_exec(db.get(), setup.c_str(), nullptr, nullptr, nullptr); result != SQLITE_OK) {
        return {.replaceable = isDamage(result)};
    }
    return {.db = std::move(db)};
}

std::string colorKey(const ColorTarget& target) {
    if (!target.enabled) {
        return "off";
    }
    if (!target.displayProfile) {
        return "srgb";
    }
    std::uint64_t hash = 14695981039346656037ull;  // FNV-1a, stable across runs unlike std::hash
    for (const auto byte : *target.displayProfile) {
        hash = (hash ^ static_cast<std::uint8_t>(byte)) * 1099511628211ull;
    }
    return std::to_string(hash);
}

}  // namespace

struct ThumbnailCache::Impl {
    Database db{nullptr, closeDatabase};
    Statement select{nullptr, closeStatement};
    Statement touch{nullptr, closeStatement};
    Statement insert{nullptr, closeStatement};
    std::uint64_t maxBytes = 0;
    int storesSinceTrim = 0;
    std::mutex mutex;  // one connection, used by one thread at a time

    void trimLocked() const {
        std::uint64_t total = 0;
        {
            const Statement query = prepare(db.get(), "SELECT TOTAL(LENGTH(bytes)) FROM thumbnails");
            if (query && sqlite3_step(query.get()) == SQLITE_ROW) {
                total = static_cast<std::uint64_t>(sqlite3_column_double(query.get(), 0));
            }
        }
        if (total <= maxBytes) {
            return;
        }
        const auto target = static_cast<std::uint64_t>(static_cast<double>(maxBytes) * kTrimTarget);
        const Statement oldest = prepare(db.get(), "SELECT rowid, LENGTH(bytes) FROM thumbnails ORDER BY last_used");
        const Statement remove = prepare(db.get(), "DELETE FROM thumbnails WHERE rowid = ?");
        if (!oldest || !remove) {
            return;
        }
        std::vector<std::int64_t> doomed;
        while (total > target && sqlite3_step(oldest.get()) == SQLITE_ROW) {
            doomed.push_back(sqlite3_column_int64(oldest.get(), 0));
            total -= std::min<std::uint64_t>(total, static_cast<std::uint64_t>(sqlite3_column_int64(oldest.get(), 1)));
        }
        sqlite3_exec(db.get(), "BEGIN", nullptr, nullptr, nullptr);
        for (const std::int64_t row : doomed) {
            sqlite3_bind_int64(remove.get(), 1, row);
            sqlite3_step(remove.get());
            sqlite3_reset(remove.get());
        }
        sqlite3_exec(db.get(), "COMMIT", nullptr, nullptr, nullptr);
    }
};

ThumbnailCache::ThumbnailCache(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
ThumbnailCache::~ThumbnailCache() = default;

std::unique_ptr<ThumbnailCache> ThumbnailCache::open(const std::filesystem::path& file, std::uint64_t maxBytes) {
    Opened opened = openDatabase(file);
    if (!opened.db && opened.replaceable) {
        // It holds nothing that cannot be made again, so a bad file is replaced.
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
        std::filesystem::remove(std::filesystem::path(file) += "-wal", ignored);
        std::filesystem::remove(std::filesystem::path(file) += "-shm", ignored);
        opened = openDatabase(file);
    }
    Database db = std::move(opened.db);
    if (!db) {
        return nullptr;  // thumbnails are then made each time, as without a cache
    }
    auto impl = std::make_unique<Impl>();
    impl->db = std::move(db);
    impl->maxBytes = maxBytes;
    impl->select = prepare(impl->db.get(),
                           "SELECT rowid, filter, modified, size, last_used, bytes FROM thumbnails WHERE path = ? AND edge = ? AND color = ?");
    impl->touch = prepare(impl->db.get(), "UPDATE thumbnails SET last_used = ? WHERE rowid = ?");
    impl->insert = prepare(impl->db.get(),
                           "INSERT OR REPLACE INTO thumbnails(path, edge, color, filter, modified, size, last_used, bytes) VALUES(?, ?, ?, ?, ?, ?, ?, ?)");
    if (!impl->select || !impl->touch || !impl->insert) {
        return nullptr;
    }
    impl->trimLocked();
    return std::unique_ptr<ThumbnailCache>(new ThumbnailCache(std::move(impl)));
}

std::optional<ImageBuffer> ThumbnailCache::find(const Key& key) {
    const std::scoped_lock lock(m_impl->mutex);
    sqlite3_stmt* select = m_impl->select.get();
    sqlite3_bind_text(select, 1, key.path.c_str(), static_cast<int>(key.path.size()), SQLITE_STATIC);
    sqlite3_bind_int(select, 2, key.edge);
    sqlite3_bind_text(select, 3, key.color.c_str(), static_cast<int>(key.color.size()), SQLITE_STATIC);
    std::optional<ImageBuffer> found;
    std::int64_t refresh = -1;
    if (sqlite3_step(select) == SQLITE_ROW && sqlite3_column_int(select, 1) == kFilterVersion &&
        sqlite3_column_int64(select, 2) == key.modified && static_cast<std::uint64_t>(sqlite3_column_int64(select, 3)) == key.size) {
        found = decodeThumbnail(sqlite3_column_blob(select, 5), sqlite3_column_bytes(select, 5));
        // Writing on every read would make browsing wear on the disk for little.
        if (found && now() - sqlite3_column_int64(select, 4) > kSecondsPerDay) {
            refresh = sqlite3_column_int64(select, 0);
        }
    }
    sqlite3_reset(select);
    if (refresh >= 0) {
        sqlite3_stmt* touch = m_impl->touch.get();
        sqlite3_bind_int64(touch, 1, now());
        sqlite3_bind_int64(touch, 2, refresh);
        sqlite3_step(touch);
        sqlite3_reset(touch);
    }
    return found;
}

void ThumbnailCache::store(const Key& key, const ImageBuffer& thumbnail) {
    if (thumbnail.format() != PixelFormat::Bgrx8 || thumbnail.width() <= 0 || thumbnail.height() <= 0) {
        return;
    }
    std::vector<unsigned char> bytes;
    try {
        bytes = encodeThumbnail(thumbnail);  // outside the lock, as the other threads have no need to wait for it
    } catch (const std::bad_alloc&) {
        return;
    }
    if (bytes.empty()) {
        return;
    }
    const std::scoped_lock lock(m_impl->mutex);
    sqlite3_stmt* insert = m_impl->insert.get();
    sqlite3_bind_text(insert, 1, key.path.c_str(), static_cast<int>(key.path.size()), SQLITE_STATIC);
    sqlite3_bind_int(insert, 2, key.edge);
    sqlite3_bind_text(insert, 3, key.color.c_str(), static_cast<int>(key.color.size()), SQLITE_STATIC);
    sqlite3_bind_int(insert, 4, kFilterVersion);
    sqlite3_bind_int64(insert, 5, key.modified);
    sqlite3_bind_int64(insert, 6, static_cast<std::int64_t>(key.size));
    sqlite3_bind_int64(insert, 7, now());
    sqlite3_bind_blob(insert, 8, bytes.data(), static_cast<int>(bytes.size()), SQLITE_STATIC);
    sqlite3_step(insert);
    sqlite3_reset(insert);
    if (++m_impl->storesSinceTrim >= kStoresBetweenTrims) {
        m_impl->storesSinceTrim = 0;
        m_impl->trimLocked();
    }
}

void ThumbnailCache::trim() {
    const std::scoped_lock lock(m_impl->mutex);
    m_impl->trimLocked();
}

void ThumbnailCache::setMaxBytes(std::uint64_t maxBytes) {
    const std::scoped_lock lock(m_impl->mutex);
    if (m_impl->maxBytes == maxBytes) return;
    m_impl->maxBytes = maxBytes;
    m_impl->trimLocked();
}

ThumbnailCache::Usage ThumbnailCache::usage() {
    const std::scoped_lock lock(m_impl->mutex);
    const Statement query = prepare(m_impl->db.get(), "SELECT COUNT(*), TOTAL(LENGTH(bytes)) FROM thumbnails");
    if (!query || sqlite3_step(query.get()) != SQLITE_ROW) {
        return {};
    }
    return {static_cast<std::uint64_t>(sqlite3_column_int64(query.get(), 0)),
            static_cast<std::uint64_t>(sqlite3_column_double(query.get(), 1))};
}

bool ThumbnailCache::clear() {
    const std::scoped_lock lock(m_impl->mutex);
    sqlite3* db = m_impl->db.get();
    // Overwrites what it frees, in case another copy of the program keeps the rewrite below from
    // happening now.
    sqlite3_exec(db, "PRAGMA secure_delete=ON", nullptr, nullptr, nullptr);
    const bool deleted = sqlite3_exec(db, "DELETE FROM thumbnails", nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_exec(db, "PRAGMA secure_delete=OFF", nullptr, nullptr, nullptr);
    if (deleted) {
        sqlite3_exec(db, "VACUUM", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA wal_checkpoint(TRUNCATE)", nullptr, nullptr, nullptr);
    }
    m_impl->storesSinceTrim = 0;
    return deleted;
}

Result<ImageBuffer> makeCachedThumbnail(ThumbnailCache* cache, const std::filesystem::path& path, int maxEdge,
                                        std::stop_token stop, const ColorTarget& target) {
    std::optional<ThumbnailCache::Key> key;
    if (cache) {
        std::error_code timeError;
        std::error_code sizeError;
        const auto modified = std::filesystem::last_write_time(path, timeError);
        const auto size = std::filesystem::file_size(path, sizeError);
        if (!timeError && !sizeError) {
            key = ThumbnailCache::Key{utf8(path), maxEdge, colorKey(target),
                                      static_cast<std::int64_t>(modified.time_since_epoch().count()), size};
            if (auto found = cache->find(*key)) {
                return *std::move(found);
            }
        }
    }
    auto thumbnail = makeThumbnail(path, maxEdge, stop, target);
    if (thumbnail && key) {
        cache->store(*key, *thumbnail);
    }
    return thumbnail;
}

}  // namespace iv
