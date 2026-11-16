find_package(GTest REQUIRED)
find_package(JPEG REQUIRED)
find_package(TIFF REQUIRED)
include(GoogleTest)

# Links no Qt, so a Qt dependency creeping into core breaks this build.
add_executable(iv_core_tests
    color_tests.cpp
    crop_tests.cpp
    disk_gate_tests.cpp
    document_tests.cpp
    probe_tests.cpp
    perspective_tests.cpp
    gray_tests.cpp
    color_edit_tests.cpp
    core_tests.cpp
    format_tests.cpp
    info_tests.cpp
    jpeg_transform_tests.cpp
    writer_tests.cpp
    xmp_tests.cpp
    save_tests.cpp
    shape_tests.cpp
    layer_tests.cpp
    coverage_tests.cpp
    fuzzy_match_tests.cpp
    palette_tests.cpp
    font_families_tests.cpp
    strip_layout_tests.cpp
    canvas_tests.cpp
    color_choice_tests.cpp
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
# PNG_FOUND from src/core does not reach this directory, so look again.
find_package(PNG)
if(PNG_FOUND)
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
    test_exif.cpp
    test_exif.h
    test_jpeg.cpp
    test_jpeg.h
    test_tiff.cpp
    test_tiff.h
    ui_test_support.h
    ui_test_main.cpp
    ui_browse_tests.cpp
    ui_browser_page_tests.cpp
    ui_image_view_tests.cpp
    ui_annotate_tests.cpp
    ui_search_combo_tests.cpp
    ui_controls_tests.cpp
    ui_edit_tests.cpp
    ui_file_tests.cpp
    ui_window_tests.cpp
    ui_strip_tests.cpp
    ui_color_tests.cpp
)
target_link_libraries(iv_ui_tests PRIVATE iv_ui iv_uiplugin iv_style iv_styleplugin Qt6::QuickControls2 iv_core JPEG::JPEG TIFF::TIFF GTest::gtest Qt6::Gui Qt6::Quick Qt6::Test)
# Every UI test file includes the same Qt Quick and GoogleTest headers, which take most of the time.
target_precompile_headers(iv_ui_tests PRIVATE ui_test_support.h)
# The directory tree shows the system's temporary folder, where tests running at the same time make
# their own, which moves the rows between finding one and clicking or dropping on it. So these run
# alone, also as a drop on the wrong row would put files in another test's folder.
set(IV_TREE_TESTS "BrowserPage.Tree*:BrowserPage.ClickingAFolder*:BrowserPage.DoubleClickingAFolder*:Drop.FilesOnAFolderOfTheTree*:Drop.NotOnTheFolder*")
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
