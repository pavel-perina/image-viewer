find_package(GTest REQUIRED)
find_package(JPEG REQUIRED)
find_package(TIFF REQUIRED)
include(GoogleTest)

# Links no Qt, so a Qt dependency creeping into core breaks this build.
add_executable(iv_core_tests
    core_tests.cpp
    format_tests.cpp
    info_tests.cpp
    test_exif.cpp
    test_exif.h
    test_jpeg.cpp
    test_jpeg.h
    test_tiff.cpp
    test_tiff.h
    tiff_tests.cpp
)
set_target_properties(iv_core_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(iv_core_tests PRIVATE iv_core JPEG::JPEG TIFF::TIFF GTest::gtest GTest::gtest_main)
# The formats' own encoders create the test files.
find_package(PkgConfig REQUIRED)
if(PNG_FOUND)
    find_package(PNG REQUIRED)
    target_link_libraries(iv_core_tests PRIVATE PNG::PNG)
endif()
if(WEBP_FOUND)
    pkg_check_modules(WEBPMUX REQUIRED IMPORTED_TARGET libwebpmux)
    target_link_libraries(iv_core_tests PRIVATE PkgConfig::WEBP PkgConfig::WEBPMUX)
endif()
if(AVIF_FOUND)
    target_link_libraries(iv_core_tests PRIVATE PkgConfig::AVIF)
endif()
if(JXL_FOUND)
    target_link_libraries(iv_core_tests PRIVATE PkgConfig::JXL)
endif()
gtest_discover_tests(iv_core_tests)

add_executable(iv_ui_tests
    test_jpeg.cpp
    test_jpeg.h
    test_tiff.cpp
    test_tiff.h
    ui_tests.cpp
)
target_link_libraries(iv_ui_tests PRIVATE iv_ui iv_uiplugin iv_core JPEG::JPEG TIFF::TIFF GTest::gtest Qt6::Gui Qt6::Quick)
gtest_discover_tests(iv_ui_tests
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fusion"
    DISCOVERY_MODE PRE_TEST
)
