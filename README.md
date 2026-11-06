# Image Viewer

A fast, multiplatform image viewer and basic editor in the spirit of ACDSee and FastStone
Image Viewer. Early development: it currently opens JPEG, TIFF (including pyramids, multipage, BigTIFF and
16-bit or float images), PNG, WebP, AVIF and JPEG XL. Animations show their first frame. See
`doc/design`.

## Build

Needs a C++23 compiler, CMake 3.28+, Qt 6.8+ (Quick, QuickControls2), libjpeg-turbo, libtiff, LittleCMS 2
and, for the tests, GoogleTest. libpng, libwebp, libavif and libjxl are optional, and each adds its format.

openSUSE:

    sudo zypper in qt6-base-devel qt6-declarative-devel libjpeg62-devel libtiff-devel liblcms2-devel libpng16-devel libwebp-devel libavif-devel libjxl-devel gtest

Build and test:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build
    ctest --test-dir build

Run:

    build/src/app/imageviewer [file-or-directory]

## Controls

| Input | Action |
| --- | --- |
| Click a thumbnail | Select it and show it in the preview |
| Double-click a thumbnail, Enter | Open the large view |
| Esc | Back to the browser, or leave full screen |
| Right, PgDown, Space | Next image |
| Left, PgUp, Backspace | Previous image |
| Ctrl+PgDown, Ctrl+PgUp | Next or previous page of a multipage file |
| Up, Down | Move one row in the browser |
| C | Turn colour management on or off |
| Ctrl+click | Add or remove a thumbnail from the selection |
| Shift+click, Shift+arrows | Select a range; with Ctrl+click, add a range |
| Ctrl+A | Select all images in the folder |
| Click between thumbnails | Clear the selection |
| Mouse wheel, + and - | Zoom |
| Drag | Pan |
| Double-click | Toggle fit and 100% |
| F | Fit to window, enlarging small images |
| 1 | 100% |
| F11 | Full screen |
| I | Show or hide the info panel, when not at full screen |
| Mouse at the right screen edge | At full screen, show the info panel until the mouse leaves it |

The info panel shows a histogram and details about the file, the image and the camera. Its
animation can be turned off in the View menu.

The toolbar has Auto Contrast for images with more than 8 bits per sample.
