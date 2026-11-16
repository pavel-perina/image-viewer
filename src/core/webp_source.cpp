#include "core/webp_source.h"

#include <webp/decode.h>
#include <webp/demux.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

#include "core/image_details.h"
#include "core/image_ops.h"
#include "core/whole_image_source.h"

namespace iv {

namespace {

void readChunks(const std::vector<std::uint8_t>& bytes, SubimageInfo& info) {
    const WebPData data{bytes.data(), bytes.size()};
    WebPDemuxer* demux = WebPDemux(&data);
    if (!demux) {
        return;
    }
    WebPChunkIterator chunk;
    if (WebPDemuxGetChunk(demux, "ICCP", 1, &chunk)) {
        info.iccProfile.assign(chunk.chunk.bytes, chunk.chunk.bytes + chunk.chunk.size);
        WebPDemuxReleaseChunkIterator(&chunk);
    }
    if (WebPDemuxGetChunk(demux, "EXIF", 1, &chunk)) {
        info.exif = parseExif({chunk.chunk.bytes, chunk.chunk.size});
        info.orientation = info.exif.orientation;
        const auto tiff = exifTiffStructure({chunk.chunk.bytes, chunk.chunk.size});
        info.rawExif.assign(tiff.begin(), tiff.end());
        WebPDemuxReleaseChunkIterator(&chunk);
    }
    if (WebPDemuxGetChunk(demux, "XMP ", 1, &chunk)) {
        info.xmp.assign(chunk.chunk.bytes, chunk.chunk.bytes + chunk.chunk.size);
        WebPDemuxReleaseChunkIterator(&chunk);
    }
    WebPDemuxDelete(demux);
}

Result<ImageBuffer> decodeStill(const std::vector<std::uint8_t>& bytes, Size size) {
    const ImageBuffer image = ImageBuffer::create(size, PixelFormat::Bgrx8);
    const std::size_t capacity = image.stride() * static_cast<std::size_t>(size.height);
    if (!WebPDecodeBGRAInto(bytes.data(), bytes.size(), reinterpret_cast<std::uint8_t*>(image.row(0)), capacity,
                            static_cast<int>(image.stride()))) {
        return std::unexpected("cannot decode WebP image");
    }
    return image;
}

Result<ImageBuffer> decodeFirstFrame(const std::vector<std::uint8_t>& bytes, Size size) {
    WebPAnimDecoderOptions options;
    if (!WebPAnimDecoderOptionsInit(&options)) {
        return std::unexpected("incompatible WebP library");
    }
    options.color_mode = MODE_BGRA;
    const WebPData data{bytes.data(), bytes.size()};
    WebPAnimDecoder* decoder = WebPAnimDecoderNew(&data, &options);
    if (!decoder) {
        return std::unexpected("cannot decode animated WebP image");
    }
    std::uint8_t* frame = nullptr;
    int timestamp = 0;
    if (!WebPAnimDecoderGetNext(decoder, &frame, &timestamp)) {
        WebPAnimDecoderDelete(decoder);
        return std::unexpected("cannot decode animated WebP image");
    }
    const ImageBuffer image = ImageBuffer::create(size, PixelFormat::Bgrx8);
    for (int y = 0; y < size.height; ++y) {
        std::memcpy(image.row(y), frame + static_cast<std::size_t>(y) * size.width * 4, static_cast<std::size_t>(size.width) * 4);
    }
    WebPAnimDecoderDelete(decoder);
    return image;
}

}  // namespace

bool isWebp(std::span<const std::uint8_t> h) {
    return h.size() >= 12 && std::memcmp(h.data(), "RIFF", 4) == 0 && std::memcmp(h.data() + 8, "WEBP", 4) == 0;
}

Result<std::shared_ptr<ImageSource>> openWebpSource(std::vector<std::uint8_t> bytes) {
    WebPBitstreamFeatures features;
    if (WebPGetFeatures(bytes.data(), bytes.size(), &features) != VP8_STATUS_OK) {
        return std::unexpected("cannot read WebP header");
    }
    const Size size{features.width, features.height};
    if (!fitsInMemoryLimit(size, PixelFormat::Bgrx8)) {
        return std::unexpected("image is too large");
    }

    SubimageInfo info;
    info.format = PixelFormat::Bgrx8;
    info.alpha = features.has_alpha != 0;
    readChunks(bytes, info);
    StoredFormat& stored = info.stored;
    stored.container = "WebP";
    stored.colorModel = features.has_alpha ? "RGB + alpha" : "RGB";
    stored.animated = features.has_animation != 0;
    switch (features.format) {
    case 1:
        stored.compression = "WebP, lossy";
        stored.chromaSubsampling = "4:2:0";  // the only one lossy WebP has
        break;
    case 2:
        stored.compression = "WebP, lossless";
        break;
    default:
        stored.compression = "WebP, lossy or lossless";  // frames of an animation can differ
        break;
    }

    const bool animated = features.has_animation != 0;
    const bool hasAlpha = features.has_alpha != 0;
    return makeWholeImageSource(std::move(info), size,
                                [bytes = std::move(bytes), size, animated, hasAlpha](const std::stop_token& stop) -> Result<ImageBuffer> {
        if (stop.stop_requested()) {
            return std::unexpected("cancelled");
        }
        try {
            auto image = animated ? decodeFirstFrame(bytes, size) : decodeStill(bytes, size);
            if (image) {
                return image->withAlpha(hasAlpha);
            }
            return image;
        } catch (const std::bad_alloc&) {
            return std::unexpected("not enough memory to decode image");
        }
    });
}

}  // namespace iv
