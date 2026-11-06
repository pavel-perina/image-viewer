# Pure C++ on purpose: nothing in this library may depend on Qt.
find_package(JPEG REQUIRED)

add_library(iv_core STATIC
    exif.cpp
    exif.h
    geometry.cpp
    geometry.h
    image_buffer.cpp
    image_buffer.h
    image_source.cpp
    image_source.h
    jpeg_source.cpp
    jpeg_source.h
    thumbnail.cpp
    thumbnail.h
    viewport.cpp
    viewport.h
    work_queue.cpp
    work_queue.h
)

target_include_directories(iv_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(iv_core PRIVATE JPEG::JPEG)
