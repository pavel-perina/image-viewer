#include "test_tiff.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include <tiffio.h>

namespace iv::test {

namespace {

// Writes one sample at a position in a packed row, scaling an 8-bit value to the file's format.
void putSample(std::vector<std::uint8_t>& out, std::size_t index, std::uint8_t value, const TiffImage& image) {
    const std::size_t bytes = static_cast<std::size_t>(image.bits) / 8;
    std::uint8_t* at = out.data() + index * bytes;
    if (image.sampleFormat == SAMPLEFORMAT_IEEEFP) {
        if (image.bits == 32) {
            const float v = static_cast<float>(value) / 255.0F;
            std::memcpy(at, &v, sizeof(v));
        } else {
            const double v = value / 255.0;
            std::memcpy(at, &v, sizeof(v));
        }
    } else if (image.bits == 8) {
        at[0] = image.sampleFormat == SAMPLEFORMAT_INT ? value ^ 0x80 : value;
    } else if (image.bits == 16) {
        std::uint16_t v = static_cast<std::uint16_t>(value * 257);
        if (image.sampleFormat == SAMPLEFORMAT_INT) {
            v ^= 0x8000;
        }
        std::memcpy(at, &v, sizeof(v));
    } else if (image.bits == 32) {
        const std::uint32_t v = value * 16843009u;
        std::memcpy(at, &v, sizeof(v));
    }
}

std::uint8_t channel(const Rgb& pixel, int sample) {
    return sample == 0 ? pixel.r : sample == 1 ? pixel.g : pixel.b;
}

void writeBlock(TIFF* tiff, const TiffImage& image, int x0, int y0, int width, int height, int sample, bool oneSample,
                std::uint32_t index) {
    const int perPixel = oneSample ? 1 : image.samples;
    const std::size_t count = static_cast<std::size_t>(width) * height * perPixel;
    std::vector<std::uint8_t> bytes(count * std::max(1, image.bits / 8), 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool inside = x0 + x < image.width && y0 + y < image.height;
            const Rgb pixel = inside ? image.pixel(x0 + x, y0 + y) : Rgb{0, 0, 0};
            const std::size_t base = (static_cast<std::size_t>(y) * width + x) * perPixel;
            for (int s = 0; s < perPixel; ++s) {
                putSample(bytes, base + s, channel(pixel, oneSample ? sample : s), image);
            }
        }
    }
    const auto size = static_cast<tmsize_t>(bytes.size());
    const tmsize_t written = image.tileSize > 0 ? TIFFWriteEncodedTile(tiff, index, bytes.data(), size)
                                                : TIFFWriteEncodedStrip(tiff, index, bytes.data(), size);
    if (written < 0) {
        throw std::runtime_error("cannot write test TIFF data");
    }
}

void writeTags(TIFF* tiff, const TiffImage& image) {
    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, static_cast<std::uint32_t>(image.width));
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, static_cast<std::uint32_t>(image.height));
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, static_cast<std::uint16_t>(image.bits));
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, static_cast<std::uint16_t>(image.samples));
    TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT, static_cast<std::uint16_t>(image.sampleFormat));
    TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, static_cast<std::uint16_t>(image.planar));
    TIFFSetField(tiff, TIFFTAG_COMPRESSION, static_cast<std::uint16_t>(image.compression));
    TIFFSetField(tiff, TIFFTAG_ORIENTATION, static_cast<std::uint16_t>(image.orientation));
    const int photometric = image.photometric >= 0 ? image.photometric
                            : image.samples >= 3   ? PHOTOMETRIC_RGB
                                                   : PHOTOMETRIC_MINISBLACK;
    TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, static_cast<std::uint16_t>(photometric));
    if (image.reduced) {
        TIFFSetField(tiff, TIFFTAG_SUBFILETYPE, static_cast<std::uint32_t>(FILETYPE_REDUCEDIMAGE));
    }
    if (!image.iccProfile.empty()) {
        TIFFSetField(tiff, TIFFTAG_ICCPROFILE, static_cast<std::uint32_t>(image.iccProfile.size()), image.iccProfile.data());
    }
    if (image.tileSize > 0) {
        TIFFSetField(tiff, TIFFTAG_TILEWIDTH, static_cast<std::uint32_t>(image.tileSize));
        TIFFSetField(tiff, TIFFTAG_TILELENGTH, static_cast<std::uint32_t>(image.tileSize));
    } else {
        TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, static_cast<std::uint32_t>(image.rowsPerStrip));
    }
    if (image.compression == COMPRESSION_JPEG) {
        TIFFSetField(tiff, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);
    }
}

void writePixels(TIFF* tiff, const TiffImage& image) {
    const int planes = image.planar == PLANARCONFIG_SEPARATE ? image.samples : 1;
    const bool separate = planes > 1;
    if (image.tileSize > 0) {
        const int across = (image.width + image.tileSize - 1) / image.tileSize;
        const int down = (image.height + image.tileSize - 1) / image.tileSize;
        std::uint32_t index = 0;
        for (int plane = 0; plane < planes; ++plane) {
            for (int row = 0; row < down; ++row) {
                for (int col = 0; col < across; ++col) {
                    writeBlock(tiff, image, col * image.tileSize, row * image.tileSize, image.tileSize, image.tileSize,
                               plane, separate, index++);
                }
            }
        }
    } else {
        const int strips = (image.height + image.rowsPerStrip - 1) / image.rowsPerStrip;
        std::uint32_t index = 0;
        for (int plane = 0; plane < planes; ++plane) {
            for (int strip = 0; strip < strips; ++strip) {
                const int y = strip * image.rowsPerStrip;
                writeBlock(tiff, image, 0, y, image.width, std::min(image.rowsPerStrip, image.height - y), plane, separate, index++);
            }
        }
    }
}

}  // namespace

void writeTiff(const std::filesystem::path& path, const std::vector<TiffImage>& images, bool bigTiff) {
    TIFF* tiff = TIFFOpen(path.string().c_str(), bigTiff ? "w8" : "w");
    if (!tiff) {
        throw std::runtime_error("cannot create test TIFF");
    }
    for (const TiffImage& image : images) {
        writeTags(tiff, image);
        if (!image.subIfds.empty()) {
            const std::vector<toff_t> placeholders(image.subIfds.size(), 0);
            TIFFSetField(tiff, TIFFTAG_SUBIFD, static_cast<std::uint16_t>(placeholders.size()), placeholders.data());
        }
        writePixels(tiff, image);
        if (!TIFFWriteDirectory(tiff)) {
            throw std::runtime_error("cannot write test TIFF directory");
        }
        for (const TiffImage& sub : image.subIfds) {
            writeTags(tiff, sub);
            writePixels(tiff, sub);
            if (!TIFFWriteDirectory(tiff)) {
                throw std::runtime_error("cannot write test TIFF SubIFD");
            }
        }
    }
    TIFFClose(tiff);
}

}  // namespace iv::test
