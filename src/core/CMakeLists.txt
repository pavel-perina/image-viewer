# Pure C++ on purpose: nothing in this library may depend on Qt.
find_package(JPEG REQUIRED)
find_package(TIFF REQUIRED)
find_package(SQLite3 REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET GLOBAL lcms2)

add_library(iv_core STATIC
    bands.h
    color.cpp
    color.h
    color_edit.cpp
    color_edit.h
    crop.cpp
    crop.h
    disk_gate.cpp
    disk_gate.h
    document.cpp
    document.h
    exif.cpp
    exif.h
    file_io.cpp
    file_io.h
    font_families.cpp
    font_families.h
    fuzzy_match.cpp
    fuzzy_match.h
    geometry.cpp
    geometry.h
    histogram.cpp
    histogram.h
    image_buffer.cpp
    image_buffer.h
    image_details.cpp
    image_details.h
    coverage.cpp
    image_layer.cpp
    image_layer.h
    image_ops.cpp
    image_ops.h
    image_source.cpp
    image_source.h
    image_writer.cpp
    image_writer.h
    jpeg_source.cpp
    jpeg_source.h
    jpeg_transform.cpp
    jpeg_transform.h
    libraries.cpp
    libraries.h
    xmp.cpp
    xmp.h
    overview_source.cpp
    overview_source.h
    palette.cpp
    palette.h
    oklab.h
    perspective.cpp
    perspective.h
    pixel_probe.cpp
    pixel_probe.h
    save_image.cpp
    rgba.h
    shapes.cpp
    shapes.h
    save_image.h
    thumbnail.cpp
    thumbnail.h
    thumbnail_cache.cpp
    thumbnail_cache.h
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
target_link_libraries(iv_core PRIVATE JPEG::JPEG TIFF::TIFF SQLite::SQLite3 PkgConfig::LCMS2)

# Optional formats: the viewer builds without them and simply does not list them.
find_package(PNG)
if(PNG_FOUND)
    target_sources(iv_core PRIVATE png_source.cpp png_source.h)
    target_link_libraries(iv_core PRIVATE PNG::PNG)
    target_compile_definitions(iv_core PUBLIC IV_HAVE_PNG)
endif()
pkg_check_modules(WEBP IMPORTED_TARGET GLOBAL libwebp libwebpdemux libwebpmux)
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
    target_sources(iv_core PRIVATE jxl_source.cpp jxl_source.h jxl_transcode.cpp jxl_transcode.h)
    target_link_libraries(iv_core PRIVATE PkgConfig::JXL)
    target_compile_definitions(iv_core PUBLIC IV_HAVE_JXL)
endif()
message(STATUS "Optional formats: PNG=${PNG_FOUND} WebP=${WEBP_FOUND} AVIF=${AVIF_FOUND} JPEG XL=${JXL_FOUND}")
