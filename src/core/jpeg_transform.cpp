#include "core/jpeg_transform.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include <jpeglib.h>

#include "core/exif.h"
#include "core/file_io.h"

namespace iv {

namespace {

// One of the eight ways to turn and mirror a rectangle: first an optional transpose, which
// swaps x and y, then optional mirroring along each axis of the result.
struct Turn {
    bool transpose = false;
    bool flipX = false;
    bool flipY = false;

    bool isIdentity() const { return !transpose && !flipX && !flipY; }
};

// x' = a*x + b*y, y' = c*x + d*y, ignoring where the origin ends up.
struct Matrix {
    int a, b, c, d;
};

Matrix toMatrix(Turn turn) {
    Matrix m = turn.transpose ? Matrix{0, 1, 1, 0} : Matrix{1, 0, 0, 1};
    if (turn.flipX) {
        m.a = -m.a;
        m.b = -m.b;
    }
    if (turn.flipY) {
        m.c = -m.c;
        m.d = -m.d;
    }
    return m;
}

Turn toTurn(Matrix m) {
    if (m.a == 0) {
        return {true, m.b < 0, m.c < 0};
    }
    return {false, m.a < 0, m.d < 0};
}

// Applies first, then second.
Turn combine(Turn first, Turn second) {
    const Matrix p = toMatrix(second);
    const Matrix q = toMatrix(first);
    return toTurn({p.a * q.a + p.b * q.c, p.a * q.b + p.b * q.d, p.c * q.a + p.d * q.c, p.c * q.b + p.d * q.d});
}

Turn turnOf(Orientation orientation) {
    switch (orientation) {
    case Orientation::Normal:           return {false, false, false};
    case Orientation::MirrorHorizontal: return {false, true, false};
    case Orientation::Rotate180:        return {false, true, true};
    case Orientation::MirrorVertical:   return {false, false, true};
    case Orientation::Transpose:        return {true, false, false};
    case Orientation::Rotate90CW:       return {true, true, false};
    case Orientation::Transverse:       return {true, true, true};
    case Orientation::Rotate270CW:      return {true, false, true};
    }
    return {};
}

Turn turnOf(JpegEdit edit) {
    switch (edit) {
    case JpegEdit::RotateClockwise:        return turnOf(Orientation::Rotate90CW);
    case JpegEdit::RotateCounterclockwise: return turnOf(Orientation::Rotate270CW);
    case JpegEdit::Rotate180:              return turnOf(Orientation::Rotate180);
    case JpegEdit::FlipHorizontal:         return turnOf(Orientation::MirrorHorizontal);
    case JpegEdit::FlipVertical:           return turnOf(Orientation::MirrorVertical);
    case JpegEdit::ApplyOrientation:       return {};
    }
    return {};
}

bool startsWith(const jpeg_marker_struct* marker, std::string_view prefix) {
    return marker->data_length >= prefix.size() && std::memcmp(marker->data, prefix.data(), prefix.size()) == 0;
}

constexpr std::string_view kExifPrefix{"Exif\0\0", 6};

// Changes an EXIF block in place, so that it stays valid byte for byte.
class ExifPatcher {
public:
    explicit ExifPatcher(std::span<std::uint8_t> tiff) : m_data(tiff) {}

    // Sets the orientation to 1, unlinks the thumbnail and sets the stored pixel dimensions to
    // those of the result. Whatever is malformed is left alone.
    void apply(int width, int height) {
        if (m_data.size() < 8) {
            return;
        }
        if (m_data[0] == 'I' && m_data[1] == 'I') {
            m_little = true;
        } else if (m_data[0] != 'M' || m_data[1] != 'M') {
            return;
        }
        if (read16(2) != 42) {
            return;
        }
        const std::size_t ifd0 = read32(4);
        std::size_t exifIfd = 0;
        const std::size_t end = forEachEntry(ifd0, [&](std::size_t entry) {
            const std::uint16_t tag = read16(entry);
            if (tag == 0x0112) {
                setInteger(entry, 1);
            } else if (tag == 0x8769) {
                exifIfd = integer(entry).value_or(0);
            }
        });
        // The offset of the next directory, IFD1, which holds the thumbnail.
        if (end != 0 && end + 4 <= m_data.size()) {
            write32(end, 0);
        }
        if (exifIfd != 0) {
            forEachEntry(exifIfd, [&](std::size_t entry) {
                const std::uint16_t tag = read16(entry);
                if (tag == 0xA002) {
                    setInteger(entry, static_cast<std::uint32_t>(width));
                } else if (tag == 0xA003) {
                    setInteger(entry, static_cast<std::uint32_t>(height));
                }
            });
        }
    }

private:
    static constexpr std::size_t kEntrySize = 12;

    std::uint16_t read16(std::size_t at) const {
        return m_little ? static_cast<std::uint16_t>(m_data[at] | m_data[at + 1] << 8)
                        : static_cast<std::uint16_t>(m_data[at] << 8 | m_data[at + 1]);
    }
    std::uint32_t read32(std::size_t at) const {
        const std::uint32_t high = read16(at);
        const std::uint32_t low = read16(at + 2);
        return m_little ? (low << 16 | high) : (high << 16 | low);
    }
    void write16(std::size_t at, std::uint16_t value) {
        m_data[at + (m_little ? 0 : 1)] = static_cast<std::uint8_t>(value);
        m_data[at + (m_little ? 1 : 0)] = static_cast<std::uint8_t>(value >> 8);
    }
    void write32(std::size_t at, std::uint32_t value) {
        write16(at + (m_little ? 0 : 2), static_cast<std::uint16_t>(value));
        write16(at + (m_little ? 2 : 0), static_cast<std::uint16_t>(value >> 16));
    }

    // A SHORT or LONG with a single value, which then sits in the entry itself.
    std::optional<std::uint32_t> integer(std::size_t entry) const {
        if (read32(entry + 4) != 1) {
            return std::nullopt;
        }
        switch (read16(entry + 2)) {
        case 3:  return read16(entry + 8);
        case 4:  return read32(entry + 8);
        default: return std::nullopt;
        }
    }
    void setInteger(std::size_t entry, std::uint32_t value) {
        if (!integer(entry) || (read16(entry + 2) == 3 && value > 0xFFFF)) {
            return;
        }
        if (read16(entry + 2) == 3) {
            write16(entry + 8, static_cast<std::uint16_t>(value));
        } else {
            write32(entry + 8, value);
        }
    }

    // Visits each entry of the directory and returns where its next-directory offset is, or 0
    // if the directory is not all there.
    template <typename Visit>
    std::size_t forEachEntry(std::size_t ifd, Visit visit) {
        if (ifd < 8 || ifd > m_data.size() || m_data.size() - ifd < 2) {
            return 0;
        }
        const std::size_t count = read16(ifd);
        const std::size_t end = ifd + 2 + count * kEntrySize;
        if (end > m_data.size()) {
            return 0;
        }
        for (std::size_t i = 0; i < count; ++i) {
            visit(ifd + 2 + i * kEntrySize);
        }
        return end;
    }

    std::span<std::uint8_t> m_data;
    bool m_little = false;
};

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

// The block size of the image after the turn, in pixels: an edge that moves must be a multiple.
void mcuSize(const jpeg_decompress_struct& cinfo, bool transposed, int& width, int& height) {
    const int h = cinfo.max_h_samp_factor * DCTSIZE;
    const int v = cinfo.max_v_samp_factor * DCTSIZE;
    width = transposed ? v : h;
    height = transposed ? h : v;
}

// libjpeg reports errors with longjmp, so the functions that call into it set a jump target and
// keep only trivially destructible locals.
class Transformer {
public:
    Transformer() {
        // Shared, so that one jump target catches errors from both.
        m_src.err = jpeg_std_error(&m_errors.base);
        m_dst.err = &m_errors.base;
        m_errors.base.error_exit = onError;
        m_errors.base.output_message = onMessage;
    }

    ~Transformer() {
        if (m_dstCreated) {
            jpeg_destroy_compress(&m_dst);
        }
        if (m_srcCreated) {
            jpeg_destroy_decompress(&m_src);
        }
        std::free(m_output);
    }

    Transformer(const Transformer&) = delete;
    Transformer& operator=(const Transformer&) = delete;

    std::string error() const { return m_errors.message; }

    bool readHeader(std::span<const std::uint8_t> jpeg) {
        if (setjmp(m_errors.jump)) {
            return false;
        }
        jpeg_create_decompress(&m_src);
        m_srcCreated = true;
        jpeg_mem_src(&m_src, jpeg.data(), static_cast<unsigned long>(jpeg.size()));
        jpeg_save_markers(&m_src, JPEG_COM, 0xFFFF);
        for (int i = 0; i < 16; ++i) {
            jpeg_save_markers(&m_src, JPEG_APP0 + i, 0xFFFF);
        }
        jpeg_read_header(&m_src, TRUE);
        return true;
    }

    std::span<std::uint8_t> exif() const {
        for (jpeg_saved_marker_ptr marker = m_src.marker_list; marker; marker = marker->next) {
            if (marker->marker == JPEG_APP0 + 1 && startsWith(marker, kExifPrefix)) {
                return {marker->data + kExifPrefix.size(), marker->data_length - kExifPrefix.size()};
            }
        }
        return {};
    }

    int width() const { return static_cast<int>(m_src.image_width); }
    int height() const { return static_cast<int>(m_src.image_height); }
    const jpeg_decompress_struct& source() const { return m_src; }

    // Width and height are those of the result, which may cut off the partial blocks at an edge
    // that moves.
    bool transform(Turn turn, int width, int height) {
        m_turn = turn;
        m_width = static_cast<JDIMENSION>(width);
        m_height = static_cast<JDIMENSION>(height);
        if (setjmp(m_errors.jump)) {
            return false;
        }
        requestTargets();
        jvirt_barray_ptr* sourceArrays = jpeg_read_coefficients(&m_src);

        jpeg_create_compress(&m_dst);
        m_dstCreated = true;
        jpeg_copy_critical_parameters(&m_src, &m_dst);
        if (turn.transpose) {
            transposeParameters();
        }
        m_dst.image_width = m_width;
        m_dst.image_height = m_height;
        if (m_src.progressive_mode) {
            jpeg_simple_progression(&m_dst);
        } else {
            m_dst.optimize_coding = TRUE;
        }
        for (int ci = 0; ci < m_src.num_components; ++ci) {
            moveBlocks(ci, sourceArrays[ci]);
        }

        jpeg_mem_dest(&m_dst, &m_output, &m_outputSize);
        jpeg_write_coefficients(&m_dst, m_targets.data());
        copyMarkers();
        jpeg_finish_compress(&m_dst);
        jpeg_finish_decompress(&m_src);
        return true;
    }

    std::vector<std::uint8_t> output() const { return {m_output, m_output + m_outputSize}; }

private:
    // The size of a component's block array, padded to whole MCUs as libjpeg allocates it.
    static JDIMENSION paddedWidth(const jpeg_component_info& c) {
        return (c.width_in_blocks + c.h_samp_factor - 1) / c.h_samp_factor * c.h_samp_factor;
    }
    static JDIMENSION paddedHeight(const jpeg_component_info& c) {
        return (c.height_in_blocks + c.v_samp_factor - 1) / c.v_samp_factor * c.v_samp_factor;
    }

    // The size of a component's block array in the result. Along a mirrored axis the image
    // ends on a whole MCU, after trimming if need be, so there is no padding.
    JDIMENSION targetColumns(const jpeg_component_info& c) const {
        if (m_turn.flipX) {
            const int samples = m_turn.transpose ? c.v_samp_factor : c.h_samp_factor;
            const int maxSamples = m_turn.transpose ? m_src.max_v_samp_factor : m_src.max_h_samp_factor;
            return m_width / static_cast<JDIMENSION>(maxSamples * DCTSIZE) * static_cast<JDIMENSION>(samples);
        }
        return m_turn.transpose ? paddedHeight(c) : paddedWidth(c);
    }
    JDIMENSION targetRows(const jpeg_component_info& c) const {
        if (m_turn.flipY) {
            const int samples = m_turn.transpose ? c.h_samp_factor : c.v_samp_factor;
            const int maxSamples = m_turn.transpose ? m_src.max_h_samp_factor : m_src.max_v_samp_factor;
            return m_height / static_cast<JDIMENSION>(maxSamples * DCTSIZE) * static_cast<JDIMENSION>(samples);
        }
        return m_turn.transpose ? paddedWidth(c) : paddedHeight(c);
    }

    void requestTargets() {
        auto* common = reinterpret_cast<j_common_ptr>(&m_src);
        for (int ci = 0; ci < m_src.num_components; ++ci) {
            const jpeg_component_info& c = m_src.comp_info[ci];
            const JDIMENSION columns = targetColumns(c);
            const JDIMENSION rows = targetRows(c);
            // The writer reads a whole MCU row at a time.
            const int mcuRows = m_turn.transpose ? c.h_samp_factor : c.v_samp_factor;
            m_targets[static_cast<std::size_t>(ci)] = m_src.mem->request_virt_barray(
                common, JPOOL_IMAGE, FALSE, columns, rows, static_cast<JDIMENSION>(mcuRows));
        }
    }

    void transposeParameters() {
        std::swap(m_dst.image_width, m_dst.image_height);
        for (int ci = 0; ci < m_dst.num_components; ++ci) {
            std::swap(m_dst.comp_info[ci].h_samp_factor, m_dst.comp_info[ci].v_samp_factor);
        }
        // The coefficients are stored divided by these, so they turn with them.
        for (JQUANT_TBL* table : m_dst.quant_tbl_ptrs) {
            if (table) {
                for (int v = 0; v < DCTSIZE; ++v) {
                    for (int u = v + 1; u < DCTSIZE; ++u) {
                        std::swap(table->quantval[v * DCTSIZE + u], table->quantval[u * DCTSIZE + v]);
                    }
                }
            }
        }
    }

    // Fills the target array of a component: each block comes from where the turn takes it,
    // with its coefficients transposed and every odd frequency along a mirrored axis negated.
    void moveBlocks(int ci, jvirt_barray_ptr source) {
        auto* common = reinterpret_cast<j_common_ptr>(&m_src);
        const jpeg_component_info& c = m_src.comp_info[ci];
        const JDIMENSION columns = targetColumns(c);
        const JDIMENSION rows = targetRows(c);
        jvirt_barray_ptr target = m_targets[static_cast<std::size_t>(ci)];
        for (JDIMENSION y = 0; y < rows; ++y) {
            JBLOCKROW out = m_src.mem->access_virt_barray(common, target, y, 1, TRUE)[0];
            const JDIMENSION py = m_turn.flipY ? rows - 1 - y : y;
            JBLOCKROW sourceRow = m_turn.transpose ? nullptr : m_src.mem->access_virt_barray(common, source, py, 1, FALSE)[0];
            for (JDIMENSION x = 0; x < columns; ++x) {
                const JDIMENSION px = m_turn.flipX ? columns - 1 - x : x;
                const JCOEF* in = m_turn.transpose ? m_src.mem->access_virt_barray(common, source, px, 1, FALSE)[0][py]
                                                   : sourceRow[px];
                JCOEF* block = out[x];
                for (int v = 0; v < DCTSIZE; ++v) {
                    for (int u = 0; u < DCTSIZE; ++u) {
                        JCOEF value = m_turn.transpose ? in[u * DCTSIZE + v] : in[v * DCTSIZE + u];
                        if ((m_turn.flipX && (u & 1)) != (m_turn.flipY && (v & 1))) {
                            value = static_cast<JCOEF>(-value);
                        }
                        block[v * DCTSIZE + u] = value;
                    }
                }
            }
        }
    }

    void copyMarkers() {
        for (jpeg_saved_marker_ptr marker = m_src.marker_list; marker; marker = marker->next) {
            // libjpeg writes its own JFIF and Adobe markers when the image needs them.
            if (m_dst.write_JFIF_header && marker->marker == JPEG_APP0 && startsWith(marker, {"JFIF\0", 5})) {
                continue;
            }
            if (m_dst.write_Adobe_marker && marker->marker == JPEG_APP0 + 14 && startsWith(marker, "Adobe")) {
                continue;
            }
            // Points at images after the end of this one, which are not kept.
            if (marker->marker == JPEG_APP0 + 2 && startsWith(marker, {"MPF\0", 4})) {
                continue;
            }
            jpeg_write_marker(&m_dst, marker->marker, marker->data, marker->data_length);
        }
    }

    jpeg_decompress_struct m_src{};
    jpeg_compress_struct m_dst{};
    ErrorManager m_errors{};
    bool m_srcCreated = false;
    bool m_dstCreated = false;
    Turn m_turn;
    JDIMENSION m_width = 0;
    JDIMENSION m_height = 0;
    std::array<jvirt_barray_ptr, MAX_COMPONENTS> m_targets{};
    unsigned char* m_output = nullptr;
    unsigned long m_outputSize = 0;
};

}  // namespace

Result<std::vector<std::uint8_t>> transformJpeg(std::span<const std::uint8_t> jpeg, JpegEdit edit, bool trimEdges) {
    Transformer transformer;
    if (!transformer.readHeader(jpeg)) {
        return std::unexpected("not a valid JPEG: " + transformer.error());
    }
    const std::span<std::uint8_t> exif = transformer.exif();
    const Orientation orientation = exif.empty() ? Orientation::Normal : parseExif(exif).orientation;
    const Turn turn = combine(turnOf(orientation), turnOf(edit));
    if (turn.isIdentity() && orientation == Orientation::Normal) {
        return std::vector<std::uint8_t>{};
    }

    int mcuWidth = 0;
    int mcuHeight = 0;
    mcuSize(transformer.source(), turn.transpose, mcuWidth, mcuHeight);
    int width = turn.transpose ? transformer.height() : transformer.width();
    int height = turn.transpose ? transformer.width() : transformer.height();
    if (turn.flipX && width % mcuWidth != 0) {
        if (!trimEdges || width < mcuWidth) {
            return std::unexpected("the width is not a multiple of " + std::to_string(mcuWidth) + ", so this needs the edge trimmed to be done without loss");
        }
        width -= width % mcuWidth;
    }
    if (turn.flipY && height % mcuHeight != 0) {
        if (!trimEdges || height < mcuHeight) {
            return std::unexpected("the height is not a multiple of " + std::to_string(mcuHeight) + ", so this needs the edge trimmed to be done without loss");
        }
        height -= height % mcuHeight;
    }

    if (!exif.empty()) {
        ExifPatcher(exif).apply(width, height);
    }
    if (!transformer.transform(turn, width, height)) {
        return std::unexpected("cannot transform: " + transformer.error());
    }
    return transformer.output();
}

Result<bool> transformJpegFile(const std::filesystem::path& path, JpegEdit edit, bool trimEdges) {
    auto bytes = readFile(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto result = transformJpeg(*bytes, edit, trimEdges);
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->empty()) {
        return false;
    }
    if (auto written = writeFileSafely(path, *result, true); !written) {
        return std::unexpected(written.error());
    }
    return true;
}

bool hasJpegExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".jpg" || extension == ".jpeg" || extension == ".jpe" || extension == ".jfif";
}

}  // namespace iv
