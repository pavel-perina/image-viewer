cmake_minimum_required(VERSION 3.28)
project(ImageViewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

option(IV_BUILD_TESTS "Build the test suite" ON)

# Added before Qt is set up, so that core cannot pick up any Qt settings.
add_subdirectory(src/core)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.8)

add_subdirectory(src/ui)
add_subdirectory(src/app)

if(IV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
