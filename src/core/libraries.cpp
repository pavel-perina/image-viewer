#include "core/libraries.h"

#include <cstdio>
#include <format>

#include <jpeglib.h>
#include <lcms2.h>
#include <sqlite3.h>
#include <tiffio.h>

#ifdef IV_HAVE_PNG
#include <png.h>
#endif
#ifdef IV_HAVE_WEBP
#include <webp/decode.h>
#endif
#ifdef IV_HAVE_AVIF
#include <avif/avif.h>
#endif
#ifdef IV_HAVE_JXL
#include <jxl/decode.h>
#endif

namespace iv {

namespace {

// "LIBTIFF, Version 4.7.2\nCopyright ..." gives "4.7.2".
std::string tiffVersion() {
    const std::string text = TIFFGetVersion();
    const std::string marker = "Version ";
    const std::size_t start = text.find(marker);
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t from = start + marker.size();
    return text.substr(from, text.find_first_of(" \n", from) - from);
}

// 2190 is 2.19, and 2191 would be 2.19.1.
std::string lcmsVersion() {
    const int encoded = cmsGetEncodedCMMversion();
    const int patch = encoded % 10;
    std::string version = std::format("{}.{}", encoded / 1000, encoded / 10 % 100);
    if (patch != 0) {
        version += std::format(".{}", patch);
    }
    return version;
}

}  // namespace

std::vector<Library> libraries() {
    std::vector<Library> list;
    // The library reports no version of its own, so this is the one it was built against.
#ifdef LIBJPEG_TURBO_VERSION_NUMBER
    constexpr int jpeg = LIBJPEG_TURBO_VERSION_NUMBER;
    list.push_back({"libjpeg-turbo", std::format("{}.{}.{}", jpeg / 1000000, jpeg / 1000 % 1000, jpeg % 1000),
                    "BSD-3-Clause, IJG, zlib", "https://libjpeg-turbo.org"});
#else
    list.push_back({"libjpeg", std::to_string(JPEG_LIB_VERSION), "IJG", "https://ijg.org"});
#endif
    list.push_back({"LibTIFF", tiffVersion(), "libtiff (BSD-style)", "https://libtiff.gitlab.io/libtiff/"});
#ifdef IV_HAVE_PNG
    list.push_back({"libpng", png_get_libpng_ver(nullptr), "PNG Reference Library License v2", "https://www.libpng.org/pub/png/libpng.html"});
#endif
#ifdef IV_HAVE_WEBP
    const int webp = WebPGetDecoderVersion();
    list.push_back({"libwebp", std::format("{}.{}.{}", webp >> 16, (webp >> 8) & 0xff, webp & 0xff),
                    "BSD-3-Clause", "https://chromium.googlesource.com/webm/libwebp"});
#endif
#ifdef IV_HAVE_AVIF
    list.push_back({"libavif", avifVersion(), "BSD-2-Clause", "https://github.com/AOMediaCodec/libavif"});
#endif
#ifdef IV_HAVE_JXL
    const std::uint32_t jxl = JxlDecoderVersion();
    list.push_back({"libjxl", std::format("{}.{}.{}", jxl / 1000000, jxl / 1000 % 1000, jxl % 1000),
                    "BSD-3-Clause", "https://github.com/libjxl/libjxl"});
#endif
    list.push_back({"Little CMS", lcmsVersion(), "MIT", "https://www.littlecms.com"});
    list.push_back({"SQLite", sqlite3_libversion(), "Public domain", "https://sqlite.org"});
    return list;
}

}  // namespace iv
