# Image Viewer

A fast, multiplatform image viewer and basic editor in the spirit of ACDSee and FastStone
Image Viewer. Early development: it currently opens JPEG files. See `doc/design`.

## Build

Needs a C++23 compiler, CMake 3.28+, Qt 6.8+ (Quick, QuickControls2), libjpeg-turbo and,
for the tests, GoogleTest.

openSUSE:

    sudo zypper in qt6-base-devel qt6-declarative-devel libjpeg62-devel gtest

Build and test:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build
    ctest --test-dir build

Run:

    build/src/app/imageviewer [file-or-directory]

## Controls

| Input | Action |
| --- | --- |
| Right, PgDown, Space | Next image |
| Left, PgUp, Backspace | Previous image |
| Mouse wheel, + and - | Zoom |
| Drag | Pan |
| Double-click | Toggle fit and 100% |
| F | Fit to window, enlarging small images |
| 1 | 100% |
| F11 | Full screen |
