# Image Viewer

A fast, multiplatform image viewer and basic editor in the spirit of ACDSee and FastStone
Image Viewer. Early development: it currently opens JPEG, TIFF (including pyramids, multipage, BigTIFF and
16-bit or float images), PNG, WebP, AVIF and JPEG XL. Animations show their first frame. See
`doc/design`.

JPEG files can be rotated and flipped without loss, and repacked as JPEG XL with a byte-for-byte
check that the original comes back (Edit > Lossless JPEG, or the file menu).

File > Save As (Ctrl+Shift+S) writes the image shown as JPEG, PNG, TIFF, WebP, AVIF or JPEG XL,
with options per format. Files are opened and saved through a chooser of its own, with places,
favourite and recent folders, the folder tree and a brief or detailed list; choosing a type
changes the extension of the name, and Save saves the name typed. Camera data (EXIF and XMP) and the colour profile are kept, and the
location can be removed.

Edit > Crop (X) in the large view chooses a rectangle to the pixel, free or at a fixed ratio
including that of each screen, with undo and redo (Ctrl+Z, Ctrl+Shift+Z). Edit > Resize (Ctrl+R)
scales to an exact size, with presets for the screens, long edges and percentages; a size of
another ratio fits inside, crops the middle or stretches. Edit > Straighten (S) levels a picture
and corrects perspective from lines drawn along edges that should be vertical or horizontal, or
found by Detect; the angles can be typed too, and the result keeps the largest rectangle inside.
Edit > Grayscale weighs the colours as JPEG does, or keeps their perceptual lightness (Oklab).
Edit > Adjust Colours (A) sets levels with an output range, brightness, contrast, midtones and
saturation in Oklab, and applies a colour table from a `.cube` file, previewed live on the GPU.
File > Save (Ctrl+S) overwrites the file
after a warning that offers a new name instead, and Save As writes another file;
leaving an edited image asks first.

View > Before and After (B) shows the original and the edited picture either side of a line that
can be dragged, zoomed and scrolled together, also while adjusting colours; holding O shows the
original alone. View > Compare Two Images does the same for two images of the same size chosen in
the browser.

Edit > Probe Colours (P) reads single pixels at full size: hex, the values as stored (16-bit and
float too), fractions from 0 to 1, and sRGB when the file has a colour profile. Each value copies
with a click, Ctrl+C copies the hex, and remembered colours show as a list or as swatches.

## Build

Needs a C++23 compiler, CMake 3.28+, Qt 6.8+ (Quick, QuickControls2, ShaderTools), libjpeg-turbo, libtiff, SQLite, LittleCMS 2
and, for the tests, GoogleTest. libpng, libwebp, libavif and libjxl are optional, and each adds its format.
Google Benchmark is optional and adds `iv_benchmarks` (see `doc/design/performance-notes.md`).

openSUSE:

    sudo zypper in qt6-base-devel qt6-declarative-devel qt6-shadertools-devel libjpeg62-devel libtiff-devel liblcms2-devel sqlite3-devel libpng16-devel libwebp-devel libavif-devel libjxl-devel gtest

Windows: see `doc/building-windows.md`. It builds with Conan and runs, but some tests still fail there.

Build and test:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build
    ctest --test-dir build

Run:

    build/src/app/imageviewer [file-or-directory]

Benchmarks, when built (`benchmark-devel` on openSUSE), optionally with a real photo:

    IV_BENCHMARK_IMAGE=photo.jpg build/benchmarks/iv_benchmarks

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

## Style

The controls use Qt's Fusion style. To see the application in another of Qt's styles, for
comparison, set it in the environment:

    QT_QUICK_CONTROLS_STYLE=Universal QT_QUICK_CONTROLS_UNIVERSAL_THEME=Dark build/src/app/imageviewer
    QT_QUICK_CONTROLS_STYLE=Material QT_QUICK_CONTROLS_MATERIAL_THEME=Dark build/src/app/imageviewer
    QT_QUICK_CONTROLS_STYLE=FluentWinUI3 build/src/app/imageviewer

The layouts are made for Fusion's sizes, so the others overflow in places.

The colours, for the controls too, are all set in `src/ui/qml/Theme.qml`, so the system's
colour scheme does not change them.

The interface uses the Inter typeface, embedded in the executable, under the SIL Open Font License
(`src/app/fonts/Inter-LICENSE.txt`).

## Controls

| Input | Action |
| --- | --- |
| Click a thumbnail | Select it and show it in the preview |
| Double-click a thumbnail, Enter | Open the large view |
| Esc, Enter | Back to the browser; Esc also leaves full screen |
| Right, Space | Next image |
| Left | Previous image |
| Backspace | Previous image in the large view; parent folder in the browser |
| PgDown, PgUp | Move one page of thumbnails in the browser; next or previous image in the large view |
| Home, End | First or last thumbnail |
| Ctrl+PgDown, Ctrl+PgUp | Next or previous page of a multipage file |
| Up, Down | Move one row in the browser |
| Tab, click | Give the keys to the thumbnails or the folder tree; the selection of thumbnails turns paler while the tree has them |
| In the folder tree: Up, Down | Open the folder above or below |
| In the folder tree: Right, Left | Unfold a folder, then go into it; fold it, then go to its parent |
| In the folder tree: F2, Del, Enter | Rename the folder, move it to the trash after asking, go to its thumbnails |
| C | Turn colour management on or off |
| M | Turn sharp resampling on or off (Magic Kernel Sharp 2013) |
| Ctrl+click | Add or remove a thumbnail from the selection |
| Shift+click, Shift+arrows, Shift+PgDown, Shift+PgUp | Select a range; with Ctrl+click, add a range |
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
| Alt+Left, Alt+Right, mouse side buttons | Back and forward through the folders visited |
| Alt+Up | Parent folder |
| Click a caret in the path bar | List the subfolders of that folder |
| Ctrl+L, click the empty end of the path bar | Edit or paste the path |
| Right-click the back arrow | Choose from the folders visited |
| Star in the path bar | Favorite and recent folders, and places (home, its usual folders, disks); a click on Places folds them |
| View > Clear Thumbnail Cache | Remove the thumbnails kept on disk, after asking |
| Enter, Esc, Ctrl+C in a message | Press the highlighted button, cancel, copy the message |
| Mouse wheel | Previous or next image in the large view; zoom in the preview |
| Ctrl+mouse wheel, + and - | Zoom |
| Hold the left button on an image that fits | Show that place at 100% until released |
| Drag | Pan |
| Double-click | Toggle fit and 100% |
| F | Fit to window, enlarging small images |
| 1 | 100% |
| F11 | Full screen |
| I | Show or hide the info panel, when not at full screen |
| Mouse at the right screen edge | At full screen, show the info panel until the mouse leaves it |

The info panel shows a histogram and details about the file, the image and the camera. Where
the camera recorded a GPS position, clicking the coordinates opens a page that lists maps to
see it in, such as OpenStreetMap and Mapy.com. Its animation can be turned off in the View menu.

The toolbar has Auto Contrast for images with more than 8 bits per sample.
