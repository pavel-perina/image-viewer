#include "core/image_source.h"

#include <array>
#include <fstream>
#include <new>

#ifdef IV_HAVE_AVIF
#include "core/avif_source.h"
#endif
#include "core/file_io.h"
#include "core/jpeg_source.h"
#ifdef IV_HAVE_JXL
#include "core/jxl_source.h"
#endif
#include "core/overview_source.h"
#ifdef IV_HAVE_PNG
#include "core/png_source.h"
#endif
#include "core/tiff_source.h"
#ifdef IV_HAVE_WEBP
#include "core/webp_source.h"
#endif

namespace iv {

Result<std::shared_ptr<ImageSource>> openImageSource(const std::filesystem::path& path) {
    // Only the header: a TIFF can be far larger than memory.
    std::array<std::uint8_t, 16> header{};
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return std::unexpected("cannot open file");
        }
        file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    }

    Result<std::shared_ptr<ImageSource>> source = std::unexpected("unsupported image format");
    if (isTiff(header)) {
        source = openTiffSource(path);
    } else {
        // These formats need their whole file in memory anyway, to decode it.
        using Opener = Result<std::shared_ptr<ImageSource>> (*)(std::vector<std::uint8_t>);
        Opener opener = nullptr;
        if (isJpeg(header)) {
            opener = openJpegSource;
        }
#ifdef IV_HAVE_PNG
        if (isPng(header)) {
            opener = openPngSource;
        }
#endif
#ifdef IV_HAVE_WEBP
        if (isWebp(header)) {
            opener = openWebpSource;
        }
#endif
#ifdef IV_HAVE_AVIF
        if (isAvif(header)) {
            opener = openAvifSource;
        }
#endif
#ifdef IV_HAVE_JXL
        if (isJxl(header)) {
            opener = openJxlSource;
        }
#endif
        if (opener) {
            auto bytes = readFile(path);
            if (!bytes) {
                return std::unexpected(bytes.error());
            }
            source = opener(std::move(*bytes));
        }
    }
    if (!source) {
        return source;
    }
    return withOverviews(std::move(*source));
}

std::span<const std::string_view> supportedExtensions() {
    static const std::vector<std::string_view> kExtensions{
        "jpg", "jpeg", "jpe", "jfif", "tif", "tiff",
#ifdef IV_HAVE_PNG
        "png",
#endif
#ifdef IV_HAVE_WEBP
        "webp",
#endif
#ifdef IV_HAVE_AVIF
        "avif",
#endif
#ifdef IV_HAVE_JXL
        "jxl",
#endif
    };
    return kExtensions;
}

}  // namespace iv
