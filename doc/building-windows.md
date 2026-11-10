# Building on Windows

Conan is used on Windows only. Linux builds against the system `-devel` packages (see `README.md`)
and never reads `conanfile.txt`.

Last built on 2026-11-08 with the versions below. The app builds and starts, but some tests still
fail: see "Test status".

## What you need

| Tool | Version used | Notes |
| --- | --- | --- |
| Visual Studio 2026, "Desktop development with C++" | 18.11, MSVC 14.51 | Conan finds and loads the compiler itself, so no Native Tools prompt is needed. |
| CMake | 4.4.2 | 3.28 or newer. |
| Ninja | 1.13.2 | On `PATH`. |
| Conan | 2.24.0 | Must be Conan 2. `uv tool install conan` works if it isn't installed. |
| Qt | 6.12.0, `msvc2022_64` | 6.8 or newer, from the Qt Online Installer. The MSVC 2022 kit works with VS 2026. |

**Qt must include Shader Tools.** Don't take it from Conan, whose Qt has no Shader Tools. The
installer doesn't always select it: in `C:\Qt\MaintenanceTool.exe`, add Qt 6.12.0 > Additional
Libraries > Qt Shader Tools. To check, `C:\Qt\6.12.0\msvc2022_64\bin\qsb.exe` must exist.

If Conan has never run on this machine, create a profile with `conan profile detect`. The profile
used here:

    [settings]
    arch=x86_64
    build_type=Release
    compiler=msvc
    compiler.cppstd=23
    compiler.runtime=dynamic
    compiler.runtime_type=Release
    compiler.version=195
    os=Windows

## Building

Run these in **cmd**, not PowerShell, because `conanbuild.bat` has to set the variables in the
shell that runs CMake. From the repository root:

    conan install . -of build/conan -s build_type=Release -c tools.cmake.cmaketoolchain:generator=Ninja --build=missing
    build\conan\conanbuild.bat
    cmake -S . -B build\release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/conan_toolchain.cmake -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64
    cmake --build build\release

The first `conan install` compiles every library from source, since Conan Center had no binaries
for this compiler and settings. Later runs reuse them from `~/.conan2`. `build\conan\conanbuild.bat`
puts the compiler and `pkgconf` on `PATH`, and needs running once per cmd window.

The libraries are linked statically, so only the Qt DLLs are needed at run time.

## Running

Each time `imageviewer.exe` is linked, a post-build step runs `windeployqt`, which copies the Qt
DLLs, plugins and QML modules into `build\release`. So the app starts from there without Qt on
`PATH`, by double-click too. Compiler runtime DLLs aren't copied, as Visual Studio installs them.

    build\release\imageviewer.exe

The tests also need Qt on `PATH`, for Qt Test:

    set PATH=C:\Qt\6.12.0\msvc2022_64\bin;%PATH%
    ctest --test-dir build\release -j 8 --timeout 120 --output-on-failure

The GPU shader tests are skipped under `ctest`, which uses the software renderer. They pass on
`d3d11`, `opengl` and `vulkan`. To run them by hand, opening real windows:

    set QSG_RHI_BACKEND=d3d11
    build\release\iv_ui_tests --gtest_filter=ResampledView.*:ColorPreview.*

## Making the installer

Needs [Inno Setup 6](https://jrsoftware.org/isinfo.php) (`winget install JRSoftware.InnoSetup`).
After building as above, from the repository root:

    powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1

It writes `build\installer\ImageViewer-<version>-setup.exe`, about 24 MB. The version is the one
in the top-level `CMakeLists.txt`. Build first, as the script only packages what is already built.

The script runs `cmake --install` into `build\stage`, which is the exact content of the installer:
the app, a `windeployqt` copy of Qt without the plugins it doesn't need or the software OpenGL
renderer, the Visual C++ runtime DLLs, and the licence texts of the project, the Conan packages
and Qt (taken from Qt's `Src\LICENSES`, so the Qt sources must be installed).

The installer is per-user, so it needs no admin rights. It installs to
`%LOCALAPPDATA%\Programs\Image Viewer`, adds a Start Menu shortcut and optionally a desktop one,
and registers no file types. `AppId` in `installer/imageviewer.iss` must never change, as it's how
a new version finds the old one to upgrade.

Before distributing it: the app has no icon yet, the LGPL's source-availability obligation still
has to be met (see the Qt licensing section), and Inno Setup asks commercial users to buy a licence.

## Project licence

The project is MIT licensed (`LICENSE`). Qt stays under the LGPL as separate DLLs, and the other
libraries are permissive.

## Messages you can ignore when configuring

* `Could NOT find WrapVulkanHeaders`. The Vulkan headers matter only to code that uses Qt's Vulkan
  classes, and this project has none. To silence it, install the Vulkan SDK from
  <https://vulkan.lunarg.com/>, open a new shell so `VULKAN_SDK` is set, and configure again.
  Direct3D 11 is Qt Quick's default on Windows, so the app doesn't need the SDK.
* `Could NOT find Qt6TaskTree`. An optional dependency of one of Qt's own QML plugins, which this
  project doesn't use.

## Dependencies

All come from Conan Center, pinned in `conanfile.txt`: libjpeg-turbo, libtiff, SQLite 3, Little
CMS 2, libpng, libwebp, libavif, libjxl, GoogleTest, and pkgconf as a build tool.

* **No GPL libraries.** Don't add any, including as transitive dependencies of an optional feature.
  Exiv2 was ruled out for this reason. jbig-kit is GPL, so `conanfile.txt` turns it off in libtiff.
* **One libjpeg-turbo for everything.** libtiff and libyuv default to IJG libjpeg on Conan, so
  `conanfile.txt` points them at libjpeg-turbo.
* **AVIF needs an encoder**, because Save As writes AVIF. The Conan recipe uses libaom for encoding
  and dav1d for decoding. `conanfile.txt` turns on libaom's assembly.
* Google Benchmark is optional and not in `conanfile.txt`. Without it `iv_benchmarks` isn't built.
* lcms2, WebP, AVIF and JPEG XL are found through `pkg_check_modules`, as on Linux. On Windows the
  `.pc` files come from Conan's `PkgConfigDeps`, so the CMake files need no Windows branch.

## Test status

On 2026-11-08: 486 tests, of which 479 passed, 3 were skipped (the GPU tests) and 4 failed. Which
folder tree tests fail changes from run to run. The causes are not known yet.

| Tests | What happens |
| --- | --- |
| `MessageBox.TakesTheHeightOfAllItsText` | The box doesn't grow to fit several lines of text in time. |
| `Compare.TwoFilesZoomAndScrollTogether` | A simulated mouse drag doesn't scroll the image. |
| `BrowserPage.Tree*`, `BrowserPage.DoubleClickingAFolder*` | Rows aren't found in time. The tree shows the real path to the temporary folder, and many folders beside it may push rows out of view. An empty `TEMP` fixed only some of these. |

On Windows the tests can't confirm that a file reached the Recycle Bin, as it renames what it
receives, and those tests send small files to the real Recycle Bin. Tests that need a libtiff codec
skip themselves if it's missing.

## Compiler warnings

MSVC gives 41 distinct warnings at `/W4`, none of which stop the build. Most are harmless name
hiding (C4458) and padding (C4324). The one to review is C4611, in the JPEG and PNG code: `setjmp`
combined with C++ objects that have destructors, since a `longjmp` over those is non-portable.

## Parts never run on Windows

Expect bugs here first. The test failures above already touch several of them.

* File operations: rename, trash, move and delete on files that are open. The view may hold a
  TIFF open.
* Paths with drive letters and UNC paths: the path bar, the file chooser, typed paths.
* The file chooser's places (`FolderLists::places`), which name drives "Name (C:)".
* Drag and drop with Explorer, both ways: a plain drag should copy and Shift should move.
* The thumbnail cache location and the settings (QSettings).
* Telling SSDs from spinning disks for the disk gate (`src/core/disk_gate.cpp`): the Windows part
  has not even been compiled yet. A wrong answer only costs speed: an SSD taken for a spinning
  disk reads one file at a time.
* High-DPI scaling with fractional factors.

## Qt licensing for a commercial release

This is not legal advice. Have it checked before a commercial release.

* Using Qt under LGPLv3 in a closed-source commercial application is allowed, and nothing
  requires building Qt from source. The official prebuilt binaries are the same LGPL libraries.
  The obligations are, roughly:
  * link Qt dynamically (DLLs, as `windeployqt` ships it) so users can replace it;
  * ship the LGPL and GPL licence texts and say that Qt is used;
  * make the Qt source available, including any changes you make to Qt;
  * don't forbid, by licence or technical means, replacing the Qt libraries.

  Static linking or modifying Qt makes this harder. Either one is a reason to consider the
  commercial licence.
* Check the licence of each module used, as not every Qt module is LGPL (some are GPL-only).
  Core, Gui, Qml, Quick and QuickControls2 are LGPL. **Shader Tools** is used only at build time,
  by `qt_add_shaders`/`qsb`, to compile the shaders. The app doesn't link it or ship it, but
  confirm its licence on its documentation page anyway. Qt Test is used only by the tests.
* The Qt Online Installer asks open-source users to accept its terms. The LGPL obligations above
  apply either way.
* The other dependencies are permissive (BSD, MIT, IJG, libpng, public domain). Ship their licence
  texts with the app. AV1 (aom, dav1d) also comes with the Alliance for Open Media patent licence,
  which is royalty-free.
