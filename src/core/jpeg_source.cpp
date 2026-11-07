#include "core/jpeg_source.h"

#include <algorithm>
#include <array>
#include <climits>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <new>

#include <jpeglib.h>

#include "core/color.h"
#include "core/exif.h"
#include "core/image_details.h"
#include "core/image_ops.h"
#include "core/xmp.h"

namespace iv {

namespace {

constexpr int kTileSize = 2048;
// The scales libjpeg decodes at directly, which are reduced levels only of a JPEG too large to be
// decoded whole. Below that size the levels are made by halving in linear light, as for every
// other format, and the scales serve only as a quick first look.
constexpr std::array<int, 4> kScaleDenominators{1, 2, 4, 8};
// Refuse to decode a level larger than this, rather than risk the OOM killer. The viewer
// keeps showing a smaller level instead.
constexpr std::uint64_t kMaxDecodedBytes = 8ull * 1024 * 1024 * 1024;

struct ErrorManager {
    jpeg_error_mgr base;
    std::jmp_buf jump;
    char message[JMSG_LENGTH_MAX];
};

[[noreturn]] void onError(j_common_ptr cinfo) {
    auto* errors = reinterpret_cast<ErrorManager*>(cinfo->err);
    cinfo->err->format_message(cinfo, errors->message);
    std::longjmp(errors->jump, 1);
}

void onMessage(j_common_ptr) {}

// libjpeg reports errors with longjmp, so every function here that calls into it sets a
// jump target and keeps only trivially destructible locals.
class Decompressor {
public:
    Decompressor() {
        m_cinfo.err = jpeg_std_error(&m_errors.base);
        m_errors.base.error_exit = onError;
        m_errors.base.output_message = onMessage;
    }

    ~Decompressor() {
        if (m_created) {
            jpeg_destroy_decompress(&m_cinfo);
        }
    }

    Decompressor(const Decompressor&) = delete;
    Decompressor& operator=(const Decompressor&) = delete;

    const char* errorMessage() const { return m_errors.message; }
    jpeg_decompress_struct& cinfo() { return m_cinfo; }

    bool readHeader(std::span<const std::uint8_t> bytes, bool keepMetadata) {
        if (setjmp(m_errors.jump)) {
            return false;
        }
        jpeg_create_decompress(&m_cinfo);
        m_created = true;
        jpeg_mem_src(&m_cinfo, bytes.data(), static_cast<unsigned long>(bytes.size()));
        if (keepMetadata) {
            jpeg_save_markers(&m_cinfo, JPEG_APP0 + 1, 0xFFFF);  // EXIF
            jpeg_save_markers(&m_cinfo, JPEG_APP0 + 2, 0xFFFF);  // ICC profile
        }
        jpeg_read_header(&m_cinfo, TRUE);
        return true;
    }

    bool outputSize(int denominator, Size& size) {
        if (setjmp(m_errors.jump)) {
            return false;
        }
        m_cinfo.scale_num = 1;
        m_cinfo.scale_denom = static_cast<unsigned int>(denominator);
        jpeg_calc_output_dimensions(&m_cinfo);
        size = {static_cast<int>(m_cinfo.output_width), static_cast<int>(m_cinfo.output_height)};
        return true;
    }

    // Returns false on error, or with stopped set when the stop token fired.
    bool decode(int denominator, const ImageBuffer& target, const std::stop_token& stop, bool& stopped) {
        if (setjmp(m_errors.jump)) {
            return false;
        }
        m_cinfo.scale_num = 1;
        m_cinfo.scale_denom = static_cast<unsigned int>(denominator);
        const bool cmyk = m_cinfo.jpeg_color_space == JCS_CMYK || m_cinfo.jpeg_color_space == JCS_YCCK;
        m_cinfo.out_color_space = cmyk ? JCS_CMYK : JCS_EXT_BGRX;
        jpeg_start_decompress(&m_cinfo);

        JSAMPARRAY cmykRow = nullptr;
        if (cmyk) {
            cmykRow = m_cinfo.mem->alloc_sarray(reinterpret_cast<j_common_ptr>(&m_cinfo), JPOOL_IMAGE,
                                                m_cinfo.output_width * 4, 1);
        }
        while (m_cinfo.output_scanline < m_cinfo.output_height) {
            if (stop.stop_requested()) {
                stopped = true;
                jpeg_abort_decompress(&m_cinfo);
                return false;
            }
            auto* out = reinterpret_cast<JSAMPLE*>(target.row(static_cast<int>(m_cinfo.output_scanline)));
            if (cmyk) {
                jpeg_read_scanlines(&m_cinfo, cmykRow, 1);
                convertCmykRow(cmykRow[0], out, m_cinfo.output_width, m_cinfo.saw_Adobe_marker);
            } else {
                JSAMPROW row = out;
                jpeg_read_scanlines(&m_cinfo, &row, 1);
            }
        }
        jpeg_finish_decompress(&m_cinfo);
        return true;
    }

private:
    // A rough conversion until colour management can use the file's CMYK profile.
    // Adobe applications store CMYK inverted.
    static void convertCmykRow(const JSAMPLE* in, JSAMPLE* out, unsigned int width, bool inverted) {
        for (unsigned int i = 0; i < width; ++i, in += 4, out += 4) {
            const int k = inverted ? in[3] : 255 - in[3];
            const auto channel = [&](int value) {
                return static_cast<JSAMPLE>((inverted ? value : 255 - value) * k / 255);
            };
            out[0] = channel(in[2]);
            out[1] = channel(in[1]);
            out[2] = channel(in[0]);
            out[3] = 255;
        }
    }

    jpeg_decompress_struct m_cinfo{};
    ErrorManager m_errors{};
    bool m_created = false;
};

class JpegSource final : public ImageSource {
public:
    JpegSource(std::vector<std::uint8_t> bytes, SubimageInfo info)
        : m_bytes(std::move(bytes)) {
        m_subimages.push_back(std::move(info));
    }

    std::span<const SubimageInfo> subimages() const override { return m_subimages; }

    Result<ImageBuffer> readTile(const TileCoord& coord, std::stop_token stop) override {
        const auto& levels = m_subimages[0].levels;
        if (coord.subimage != 0 || coord.level < 0 || coord.level >= std::ssize(levels)) {
            return std::unexpected("tile does not exist");
        }
        const LevelInfo& level = levels[static_cast<std::size_t>(coord.level)];
        const int x = coord.col * level.tileSize.width;
        const int y = coord.row * level.tileSize.height;
        if (coord.col < 0 || coord.row < 0 || x >= level.size.width || y >= level.size.height) {
            return std::unexpected("tile does not exist");
        }

        auto pixels = decodedLevel(coord.level, stop);
        if (!pixels) {
            return std::unexpected(pixels.error());
        }
        return pixels->view(x, y, std::min(level.tileSize.width, level.size.width - x),
                            std::min(level.tileSize.height, level.size.height - y));
    }

    // At 1/2, 1/4 or 1/8 straight from libjpeg, which uses only the low frequencies of each
    // block and so averages gamma-encoded values, or smaller from 1/8 by halving.
    Result<ImageBuffer> roughLevel(int subimage, Size size, const std::stop_token& stop) override {
        if (subimage != 0) {
            return std::unexpected("no rough level");
        }
        Decompressor decompressor;
        if (!decompressor.readHeader(m_bytes, false)) {
            return std::unexpected(decompressor.errorMessage());
        }
        for (const int denominator : {2, 4, 8}) {
            Size scaled;
            if (!decompressor.outputSize(denominator, scaled)) {
                return std::unexpected(decompressor.errorMessage());
            }
            if (scaled == size || denominator == 8) {
                if (size.width > scaled.width || size.height > scaled.height) {
                    return std::unexpected("no rough level of this size");
                }
                auto pixels = decode(decompressor, denominator, scaled, stop);
                while (pixels && pixels->size() != size && pixels->width() > size.width) {
                    pixels = halve(*pixels);
                }
                if (pixels && pixels->size() != size) {
                    return std::unexpected("no rough level of this size");
                }
                return pixels;
            }
        }
        return std::unexpected("no rough level of this size");
    }

private:
    static Result<ImageBuffer> decode(Decompressor& decompressor, int denominator, Size size, const std::stop_token& stop) {
        ImageBuffer buffer;
        try {
            buffer = ImageBuffer::create(size, PixelFormat::Bgrx8);
        } catch (const std::bad_alloc&) {
            return std::unexpected("not enough memory to decode image");
        }
        bool stopped = false;
        if (!decompressor.decode(denominator, buffer, stop, stopped)) {
            return std::unexpected(stopped ? "cancelled" : decompressor.errorMessage());
        }
        return buffer;
    }

    // Entropy-coded JPEG data cannot be entered at an arbitrary tile, so a whole level is
    // decoded at once and the most recent one is kept to serve its other tiles.
    Result<ImageBuffer> decodedLevel(int levelIndex, const std::stop_token& stop) {
        const std::scoped_lock lock(m_mutex);
        if (m_decodedLevel == levelIndex) {
            return m_decoded;
        }
        const Size size = m_subimages[0].levels[static_cast<std::size_t>(levelIndex)].size;
        const auto bytesNeeded = static_cast<std::uint64_t>(size.width) * size.height * 4;
        if (bytesNeeded > kMaxDecodedBytes) {
            return std::unexpected("image is too large to decode at this zoom level");
        }

        Decompressor decompressor;
        if (!decompressor.readHeader(m_bytes, false)) {
            return std::unexpected(decompressor.errorMessage());
        }
        auto buffer = decode(decompressor, kScaleDenominators[static_cast<std::size_t>(levelIndex)], size, stop);
        if (!buffer) {
            return buffer;
        }
        m_decoded = *buffer;
        m_decodedLevel = levelIndex;
        return buffer;
    }

    const std::vector<std::uint8_t> m_bytes;
    std::vector<SubimageInfo> m_subimages;
    std::mutex m_mutex;
    ImageBuffer m_decoded;
    int m_decodedLevel = -1;
};

}  // namespace

bool isJpeg(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF;
}

Result<std::shared_ptr<ImageSource>> openJpegSource(std::vector<std::uint8_t> bytes) {
    if (bytes.size() > std::numeric_limits<unsigned int>::max()) {
        return std::unexpected("JPEG file is too large");
    }

    SubimageInfo info;
    info.role = SubimageRole::Page;
    info.format = PixelFormat::Bgrx8;

    Decompressor decompressor;
    if (!decompressor.readHeader(bytes, true)) {
        return std::unexpected(decompressor.errorMessage());
    }
    auto& cinfo = decompressor.cinfo();
    if (cinfo.data_precision != 8) {
        return std::unexpected("only 8-bit JPEG is supported");
    }

    for (const int denominator : kScaleDenominators) {
        Size size;
        if (!decompressor.outputSize(denominator, size)) {
            return std::unexpected(decompressor.errorMessage());
        }
        // Keep only levels that really shrink, so tiny images do not get duplicate levels.
        if (info.levels.empty() || info.levels.back().size != size) {
            info.levels.push_back({size, {kTileSize, kTileSize}});
        }
        const Size full = info.levels.front().size;
        if (static_cast<std::uint64_t>(full.width) * static_cast<std::uint64_t>(full.height) * 4 <= kMaxDecodedBytes) {
            break;  // decoded whole, and the rest made from that
        }
    }

    for (jpeg_saved_marker_ptr marker = cinfo.marker_list; marker; marker = marker->next) {
        if (marker->marker != JPEG_APP0 + 1) {
            continue;
        }
        const std::span<const std::uint8_t> data{marker->data, marker->data_length};
        if (!info.exif.present) {
            info.exif = parseExif(data);
            if (info.exif.present) {
                info.orientation = info.exif.orientation;
                const auto tiff = exifTiffStructure(data);
                info.rawExif.assign(tiff.begin(), tiff.end());
                continue;
            }
        }
        if (info.xmp.empty() && data.size() > kXmpJpegPrefix.size() &&
            std::equal(kXmpJpegPrefix.begin(), kXmpJpegPrefix.end(), data.begin())) {
            info.xmp.assign(data.begin() + static_cast<std::ptrdiff_t>(kXmpJpegPrefix.size()), data.end());
        }
    }

    info.stored.container = "JPEG";
    info.stored.compression = cinfo.progressive_mode ? "JPEG progressive, lossy" : "JPEG, lossy";
    switch (cinfo.jpeg_color_space) {
    case JCS_GRAYSCALE: info.stored.colorModel = "Grayscale"; break;
    case JCS_YCbCr:     info.stored.colorModel = "YCbCr"; break;
    case JCS_RGB:       info.stored.colorModel = "RGB"; break;
    case JCS_CMYK:      info.stored.colorModel = "CMYK"; break;
    case JCS_YCCK:      info.stored.colorModel = "YCCK"; break;
    default:            break;
    }
    if ((cinfo.jpeg_color_space == JCS_YCbCr || cinfo.jpeg_color_space == JCS_YCCK) && cinfo.num_components >= 3) {
        info.stored.chromaSubsampling = chromaSubsamplingName(cinfo.comp_info[0].h_samp_factor, cinfo.comp_info[0].v_samp_factor,
                                                              cinfo.comp_info[1].h_samp_factor, cinfo.comp_info[1].v_samp_factor);
    }

    JOCTET* icc = nullptr;
    unsigned int iccLength = 0;
    if (jpeg_read_icc_profile(&cinfo, &icc, &iccLength)) {
        info.iccProfile.assign(icc, icc + iccLength);
        std::free(icc);
    }
    if (info.iccProfile.empty() && info.exif.adobeRgb) {
        info.iccProfile = adobeRgbProfile();
    }

    return std::make_shared<JpegSource>(std::move(bytes), std::move(info));
}

}  // namespace iv
