#include "test_jpeg.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include <gtest/gtest.h>

#include <jpeglib.h>

namespace iv::test {

std::vector<std::uint8_t> encodeJpeg(const JpegOptions& options) {
    jpeg_compress_struct cinfo{};
    jpeg_error_mgr errors{};
    cinfo.err = jpeg_std_error(&errors);
    jpeg_create_compress(&cinfo);

    unsigned char* output = nullptr;
    unsigned long outputSize = 0;
    jpeg_mem_dest(&cinfo, &output, &outputSize);

    cinfo.image_width = static_cast<JDIMENSION>(options.width);
    cinfo.image_height = static_cast<JDIMENSION>(options.height);
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 95, TRUE);
    if (options.progressive) {
        jpeg_simple_progression(&cinfo);
    }
    jpeg_start_compress(&cinfo, TRUE);

    if (!options.exif.empty()) {
        jpeg_write_marker(&cinfo, JPEG_APP0 + 1, options.exif.data(), static_cast<unsigned int>(options.exif.size()));
    }
    if (options.orientation != 0) {
        // "Exif\0\0", a little-endian TIFF header, and an IFD with one SHORT entry.
        const auto value = static_cast<JOCTET>(options.orientation);
        const JOCTET exif[] = {'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 42, 0, 8, 0, 0, 0,
                               1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, value, 0, 0, 0, 0, 0, 0, 0};
        jpeg_write_marker(&cinfo, JPEG_APP0 + 1, exif, sizeof(exif));
    }
    if (!options.iccProfile.empty()) {
        jpeg_write_icc_profile(&cinfo, options.iccProfile.data(),
                               static_cast<unsigned int>(options.iccProfile.size()));
    }

    std::vector<JSAMPLE> row(static_cast<std::size_t>(options.width) * 3);
    for (int y = 0; y < options.height; ++y) {
        for (int x = 0; x < options.width; ++x) {
            const Rgb pixel = options.pixel(x, y);
            row[static_cast<std::size_t>(x) * 3] = pixel.r;
            row[static_cast<std::size_t>(x) * 3 + 1] = pixel.g;
            row[static_cast<std::size_t>(x) * 3 + 2] = pixel.b;
        }
        JSAMPROW rows[] = {row.data()};
        jpeg_write_scanlines(&cinfo, rows, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    std::vector<std::uint8_t> bytes(output, output + outputSize);
    std::free(output);
    return bytes;
}

DecodedJpeg decodeJpeg(const std::vector<std::uint8_t>& bytes) {
    jpeg_decompress_struct cinfo{};
    jpeg_error_mgr errors{};
    cinfo.err = jpeg_std_error(&errors);
    errors.error_exit = [](j_common_ptr) { throw std::runtime_error("bad JPEG"); };
    DecodedJpeg decoded;
    jpeg_create_decompress(&cinfo);
    try {
        jpeg_mem_src(&cinfo, bytes.data(), static_cast<unsigned long>(bytes.size()));
        jpeg_save_markers(&cinfo, JPEG_APP0 + 1, 0xFFFF);
        jpeg_save_markers(&cinfo, JPEG_APP0 + 2, 0xFFFF);
        jpeg_read_header(&cinfo, TRUE);
        for (jpeg_saved_marker_ptr marker = cinfo.marker_list; marker; marker = marker->next) {
            if (marker->marker == JPEG_APP0 + 1 && marker->data_length >= 6 && std::memcmp(marker->data, "Exif\0", 6) == 0) {
                decoded.exif.assign(marker->data, marker->data + marker->data_length);
            }
        }
        JOCTET* icc = nullptr;
        unsigned int iccSize = 0;
        if (jpeg_read_icc_profile(&cinfo, &icc, &iccSize)) {
            decoded.iccProfile.assign(icc, icc + iccSize);
            std::free(icc);
        }
        decoded.progressive = cinfo.progressive_mode != 0;
        cinfo.out_color_space = JCS_RGB;
        jpeg_start_decompress(&cinfo);
        decoded.width = static_cast<int>(cinfo.output_width);
        decoded.height = static_cast<int>(cinfo.output_height);
        decoded.pixels.resize(static_cast<std::size_t>(decoded.width) * decoded.height);
        std::vector<JSAMPLE> row(static_cast<std::size_t>(decoded.width) * 3);
        while (cinfo.output_scanline < cinfo.output_height) {
            const auto y = static_cast<std::size_t>(cinfo.output_scanline);
            JSAMPROW rows[] = {row.data()};
            jpeg_read_scanlines(&cinfo, rows, 1);
            for (std::size_t x = 0; x < static_cast<std::size_t>(decoded.width); ++x) {
                decoded.pixels[y * static_cast<std::size_t>(decoded.width) + x] = {row[x * 3], row[x * 3 + 1], row[x * 3 + 2]};
            }
        }
        jpeg_finish_decompress(&cinfo);
    } catch (const std::runtime_error&) {
        ADD_FAILURE() << "not a valid JPEG";
        decoded = {};
    }
    jpeg_destroy_decompress(&cinfo);
    return decoded;
}

void writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace iv::test
