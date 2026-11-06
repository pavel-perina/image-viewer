# Image Viewer

A fast, multiplatform image viewer and basic editor in the spirit of ACDSee and FastStone
Image Viewer. Early development: it currently opens JPEG, TIFF (including pyramids, multipage, BigTIFF and
16-bit or float images), PNG, WebP, AVIF and JPEG XL. Animations show their first frame. See
`doc/design`.

## Build

Needs a C++23 compiler, CMake 3.28+, Qt 6.8+ (Quick, QuickControls2, ShaderTools), libjpeg-turbo, libtiff, LittleCMS 2
and, for the tests, GoogleTest. libpng, libwebp, libavif and libjxl are optional, and each adds its format.

openSUSE:

    sudo zypper in qt6-base-devel qt6-declarative-devel qt6-shadertools-devel libjpeg62-devel libtiff-devel liblcms2-devel libpng16-devel libwebp-devel libavif-devel libjxl-devel gtest

Build and test:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build
    ctest --test-dir build

Run:

    build/src/app/imageviewer [file-or-directory]

## Graphics backend

Qt picks the backend through its RHI. To choose one, or to see which backend and GPU are in use:

    QSG_RHI_BACKEND=opengl build/src/app/imageviewer image.jpg
    QSG_RHI_BACKEND=vulkan build/src/app/imageviewer image.jpg
    QT_FORCE_STDERR_LOGGING=1 QSG_INFO=1 build/src/app/imageviewer image.jpg

Logging to stderr also matters when output is redirected, since Qt may otherwise send messages,
including QML errors, to the system journal.

`ctest` runs on the software renderer, where shaders do not exist. The test of the sharp resampling
shaders is skipped there, so run it by hand on a display:

    QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=opengl build/tests/iv_ui_tests --gtest_filter='ResampledView.*'

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
| M | Turn sharp resampling on or off (Magic Kernel Sharp 2013) |
| Ctrl+click | Add or remove a thumbnail from the selection |
| Shift+click, Shift+arrows | Select a range; with Ctrl+click, add a range |
| Ctrl+A | Select all images in the folder |
| Click between thumbnails | Clear the selection |
| Right-click | Menu of file operations; on a folder in the tree, of folder operations |
| Del | Move the selected files to the trash, or the shown image in the large view |
| Shift+Del | Delete the same files permanently, after asking |
| F2 | Rename |
| Ctrl+C | Copy files to the clipboard, a single file also as an image |
| Ctrl+V | Paste files, or save a clipboard image as a new PNG named after the time |
| Ctrl+Shift+C | Copy the full path as text |
| Ctrl+Shift+N | New folder |
| F5 | Scan the folder again |
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
