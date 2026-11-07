#include "core/jxl_transcode.h"

#include <algorithm>
#include <new>
#include <thread>

#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#include <jxl/encode.h>
#include <jxl/encode_cxx.h>
#include <jxl/thread_parallel_runner.h>
#include <jxl/thread_parallel_runner_cxx.h>

#include "core/file_io.h"

namespace iv {

namespace {

std::size_t threadCount() { return std::clamp(std::thread::hardware_concurrency(), 1u, 8u); }

Result<std::vector<std::uint8_t>> encode(std::span<const std::uint8_t> jpeg) {
    const auto encoder = JxlEncoderMake(nullptr);
    const auto runner = JxlThreadParallelRunnerMake(nullptr, threadCount());
    if (!encoder || !runner ||
        JxlEncoderSetParallelRunner(encoder.get(), JxlThreadParallelRunner, runner.get()) != JXL_ENC_SUCCESS ||
        JxlEncoderStoreJPEGMetadata(encoder.get(), JXL_TRUE) != JXL_ENC_SUCCESS) {
        return std::unexpected("cannot start the JPEG XL encoder");
    }
    JxlEncoderFrameSettings* settings = JxlEncoderFrameSettingsCreate(encoder.get(), nullptr);
    if (JxlEncoderAddJPEGFrame(settings, jpeg.data(), jpeg.size()) != JXL_ENC_SUCCESS) {
        return std::unexpected("this JPEG cannot be repacked without loss");
    }
    JxlEncoderCloseInput(encoder.get());

    std::vector<std::uint8_t> bytes(jpeg.size() + 4096);
    std::size_t used = 0;
    for (;;) {
        std::uint8_t* next = bytes.data() + used;
        std::size_t available = bytes.size() - used;
        const JxlEncoderStatus status = JxlEncoderProcessOutput(encoder.get(), &next, &available);
        used = bytes.size() - available;
        if (status == JXL_ENC_SUCCESS) {
            bytes.resize(used);
            return bytes;
        }
        if (status != JXL_ENC_NEED_MORE_OUTPUT) {
            return std::unexpected("cannot encode JPEG XL");
        }
        bytes.resize(bytes.size() * 2);
    }
}

}  // namespace

Result<std::vector<std::uint8_t>> jxlToJpeg(std::span<const std::uint8_t> jxl) {
    const auto decoder = JxlDecoderMake(nullptr);
    const auto runner = JxlThreadParallelRunnerMake(nullptr, threadCount());
    if (!decoder || !runner ||
        JxlDecoderSetParallelRunner(decoder.get(), JxlThreadParallelRunner, runner.get()) != JXL_DEC_SUCCESS ||
        JxlDecoderSubscribeEvents(decoder.get(), JXL_DEC_JPEG_RECONSTRUCTION | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS ||
        JxlDecoderSetInput(decoder.get(), jxl.data(), jxl.size()) != JXL_DEC_SUCCESS) {
        return std::unexpected("cannot start the JPEG XL decoder");
    }
    JxlDecoderCloseInput(decoder.get());

    std::vector<std::uint8_t> jpeg;
    std::size_t used = 0;
    bool reconstructing = false;
    for (;;) {
        switch (JxlDecoderProcessInput(decoder.get())) {
        case JXL_DEC_JPEG_RECONSTRUCTION:
            reconstructing = true;
            jpeg.resize(std::max<std::size_t>(jxl.size() * 2, 65536));
            if (JxlDecoderSetJPEGBuffer(decoder.get(), jpeg.data(), jpeg.size()) != JXL_DEC_SUCCESS) {
                return std::unexpected("cannot rebuild the JPEG");
            }
            break;
        case JXL_DEC_JPEG_NEED_MORE_OUTPUT:
            used = jpeg.size() - JxlDecoderReleaseJPEGBuffer(decoder.get());
            jpeg.resize(jpeg.size() * 2);
            if (JxlDecoderSetJPEGBuffer(decoder.get(), jpeg.data() + used, jpeg.size() - used) != JXL_DEC_SUCCESS) {
                return std::unexpected("cannot rebuild the JPEG");
            }
            break;
        case JXL_DEC_FULL_IMAGE:
            // With a JPEG buffer set, the image comes out there and not as pixels.
            break;
        case JXL_DEC_SUCCESS:
            if (!reconstructing) {
                return std::unexpected("this JPEG XL file was not made from a JPEG");
            }
            used = jpeg.size() - JxlDecoderReleaseJPEGBuffer(decoder.get());
            jpeg.resize(used);
            return jpeg;
        case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
            return std::unexpected("this JPEG XL file was not made from a JPEG");
        default:
            return std::unexpected("cannot decode the JPEG XL file");
        }
    }
}

Result<std::vector<std::uint8_t>> jpegToJxl(std::span<const std::uint8_t> jpeg) {
    try {
        auto jxl = encode(jpeg);
        if (!jxl) {
            return jxl;
        }
        const auto rebuilt = jxlToJpeg(*jxl);
        if (!rebuilt || !std::ranges::equal(*rebuilt, jpeg)) {
            return std::unexpected("the original file could not be rebuilt exactly, so it was not converted");
        }
        return jxl;
    } catch (const std::bad_alloc&) {
        return std::unexpected("not enough memory");
    }
}

Result<std::filesystem::path> convertJpegFileToJxl(const std::filesystem::path& jpeg) {
    std::filesystem::path target = jpeg;
    target.replace_extension(".jxl");
    if (std::filesystem::exists(target)) {
        return std::unexpected(target.filename().string() + " already exists");
    }
    const auto bytes = readFile(jpeg);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    const auto jxl = jpegToJxl(*bytes);
    if (!jxl) {
        return std::unexpected(jxl.error());
    }
    if (auto written = writeFileSafely(target, *jxl, false); !written) {
        return std::unexpected(written.error());
    }
    std::error_code ec;
    const auto modified = std::filesystem::last_write_time(jpeg, ec);
    if (!ec) {
        std::filesystem::last_write_time(target, modified, ec);
    }
    return target;
}

}  // namespace iv
