find_package(GTest REQUIRED)
find_package(JPEG REQUIRED)
find_package(TIFF REQUIRED)
include(GoogleTest)

# Links no Qt, so a Qt dependency creeping into core breaks this build.
add_executable(iv_core_tests
    color_tests.cpp
    core_tests.cpp
    format_tests.cpp
    info_tests.cpp
    jpeg_transform_tests.cpp
    test_exif.cpp
    test_exif.h
    test_jpeg.cpp
    test_jpeg.h
    test_tiff.cpp
    test_tiff.h
    tiff_tests.cpp
)
set_target_properties(iv_core_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
find_package(SQLite3 REQUIRED)
target_link_libraries(iv_core_tests PRIVATE iv_core JPEG::JPEG TIFF::TIFF SQLite::SQLite3 PkgConfig::LCMS2 GTest::gtest GTest::gtest_main)
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

find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(iv_ui_tests
    test_jpeg.cpp
    test_jpeg.h
    test_tiff.cpp
    test_tiff.h
    ui_tests.cpp
)
target_link_libraries(iv_ui_tests PRIVATE iv_ui iv_uiplugin iv_core JPEG::JPEG TIFF::TIFF GTest::gtest Qt6::Gui Qt6::Quick Qt6::Test)
# The directory tree shows the system's temporary folder, where tests running at the same time make
# their own, which moves the rows between finding one and clicking it. So these run alone.
set(IV_TREE_TESTS "BrowserPage.Tree*:BrowserPage.ClickingAFolder*:BrowserPage.DoubleClickingAFolder*")
gtest_discover_tests(iv_ui_tests
    TEST_FILTER "-${IV_TREE_TESTS}"
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fusion"
    DISCOVERY_MODE PRE_TEST
)
gtest_discover_tests(iv_ui_tests
    TEST_FILTER "${IV_TREE_TESTS}"
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fusion" RUN_SERIAL TRUE
    DISCOVERY_MODE PRE_TEST
)
