# Pure C++ on purpose: nothing in this library may depend on Qt.
find_package(JPEG REQUIRED)
find_package(TIFF REQUIRED)

add_library(iv_core STATIC
    exif.cpp
    exif.h
    geometry.cpp
    geometry.h
    histogram.cpp
    histogram.h
    image_buffer.cpp
    image_buffer.h
    image_details.cpp
    image_details.h
    image_ops.cpp
    image_ops.h
    image_source.cpp
    image_source.h
    jpeg_source.cpp
    jpeg_source.h
    overview_source.cpp
    overview_source.h
    thumbnail.cpp
    thumbnail.h
    tiff_source.cpp
    tiff_source.h
    viewport.cpp
    viewport.h
    whole_image_source.cpp
    whole_image_source.h
    work_queue.cpp
    work_queue.h
)

target_include_directories(iv_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(iv_core PRIVATE JPEG::JPEG TIFF::TIFF)

# Optional formats: the viewer builds without them and simply does not list them.
find_package(PkgConfig REQUIRED)
find_package(PNG)
if(PNG_FOUND)
    target_sources(iv_core PRIVATE png_source.cpp png_source.h)
    target_link_libraries(iv_core PRIVATE PNG::PNG)
    target_compile_definitions(iv_core PUBLIC IV_HAVE_PNG)
endif()
pkg_check_modules(WEBP IMPORTED_TARGET GLOBAL libwebp libwebpdemux)
if(WEBP_FOUND)
    target_sources(iv_core PRIVATE webp_source.cpp webp_source.h)
    target_link_libraries(iv_core PRIVATE PkgConfig::WEBP)
    target_compile_definitions(iv_core PUBLIC IV_HAVE_WEBP)
endif()
pkg_check_modules(AVIF IMPORTED_TARGET GLOBAL libavif)
if(AVIF_FOUND)
    target_sources(iv_core PRIVATE avif_source.cpp avif_source.h)
    target_link_libraries(iv_core PRIVATE PkgConfig::AVIF)
    target_compile_definitions(iv_core PUBLIC IV_HAVE_AVIF)
endif()
pkg_check_modules(JXL IMPORTED_TARGET GLOBAL libjxl libjxl_threads)
if(JXL_FOUND)
    target_sources(iv_core PRIVATE jxl_source.cpp jxl_source.h)
    target_link_libraries(iv_core PRIVATE PkgConfig::JXL)
    target_compile_definitions(iv_core PUBLIC IV_HAVE_JXL)
endif()
message(STATUS "Optional formats: PNG=${PNG_FOUND} WebP=${WEBP_FOUND} AVIF=${AVIF_FOUND} JPEG XL=${JXL_FOUND}")
