# Building on Windows

Written on 2026-11-08 from the Linux build, before anyone has built on Windows. Treat it as a plan
and correct it where reality differs.

## Toolchain

| Tool | Version | Notes |
| --- | --- | --- |
| Visual Studio 2022 (MSVC v143), "Desktop development with C++" | latest | C++23 (`/std:c++latest` comes from `CMAKE_CXX_STANDARD 23`). The project already sets `/W4 /permissive- /utf-8`. |
| CMake | 3.28 or newer | Developed with 4.4. |
| Ninja | any recent | Use the generator `-G Ninja` from a "x64 Native Tools" prompt. |
| Git | any | vcpkg needs it. |

Clang-cl should also work, but MSVC is the first target.

## Dependencies

The versions in the table are the ones the Linux build uses and the tests pass with. Older ones
will probably do, but these are known to work.

| Library | Used for | Linux version | Licence | vcpkg port | Conan Center |
| --- | --- | --- | --- | --- | --- |
| Qt 6 (Core, Gui, Qml, Quick, QuickControls2, ShaderTools; Test for the tests) | everything UI | 6.11.2, needs 6.8+ | LGPLv3 / commercial | see below | see below |
| libjpeg-turbo | JPEG read/write, lossless JPEG tools | 3.2.1 | IJG + BSD-3 | `libjpeg-turbo` | `libjpeg-turbo` |
| libtiff | TIFF incl. BigTIFF, pyramids | 4.7.2 | libtiff (BSD-like) | `tiff` | `libtiff` |
| SQLite 3 | thumbnail cache on disk | 3.53.2 | public domain | `sqlite3` | `sqlite3` |
| Little CMS 2 | colour management | 2.19 | MIT | `lcms` | `lcms` |
| libpng (optional) | PNG | 1.6.59 | libpng | `libpng` | `libpng` |
| libwebp with demux and mux (optional) | WebP | 1.6.0 | BSD-3 | `libwebp` (check the mux/demux features) | `libwebp` |
| libavif (optional) | AVIF read and **write** | 1.4.2 | BSD-2 | `libavif[aom,dav1d]` | `libavif` (check codec options) |
| libjxl with libjxl_threads (optional) | JPEG XL, also lossless JPEG repacking | 0.12.0 | BSD-3 | `libjxl` | `libjxl` |
| GoogleTest (tests only) | tests | 1.18.0 | BSD-3 | `gtest` | `gtest` |
| Google Benchmark (optional) | `iv_benchmarks` | — | Apache-2.0 | `benchmark` | `benchmark` |
| pkgconf | see "CMake changes" | — | ISC | `pkgconf` | `pkgconf` |

Notes on them:

* **No GPL libraries.** Don't add any, including as transitive dependencies of a port's optional
  features. Exiv2 was ruled out for this reason.
* **AVIF needs an encoder**, because Save As writes AVIF. Use aom for encoding (and decoding);
  add dav1d for faster decoding. rav1e and SVT-AV1 also work but aren't needed. On Linux all four
  happen to be present.
* **One libjpeg-turbo for everything.** On Linux, libtiff links a different libjpeg than the app
  does, and the linker warns about it. On Windows, make sure libtiff is built against the same
  libjpeg-turbo, which vcpkg and Conan do by default.
* libtiff pulls in zlib, liblzma, zstd, libdeflate, Lerc and jbig depending on its features. The
  defaults are fine. jbig-kit is GPL, so leave it out if a port offers it as a feature.
* libjxl pulls in brotli and highway. libavif pulls in libyuv and sharpyuv.

## Recommended way to get them

**Qt: the official binaries, not a package manager.** Building Qt through vcpkg or Conan builds
it from source, which takes hours and is easy to get wrong for Qt Quick and Shader Tools. Get the
prebuilt MSVC 2022 64-bit binaries in one of two ways:

* the Qt Online Installer (needs a Qt account). Select "MSVC 2022 64-bit" for 6.8 or newer. The
  extra modules aren't needed: Shader Tools, Qt Quick and Controls are in the base set.
* or [aqtinstall](https://github.com/miurahr/aqtinstall), which fetches the same binaries without
  an account. It's handy for CI:
  `aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtshadertools`
  (pick the newest 6.x it lists; `aqt list-qt windows desktop` shows them, and some versions name
  Shader Tools differently).

Then pass `-DCMAKE_PREFIX_PATH=C:/Qt/6.x.y/msvc2022_64`.

**Everything else: vcpkg in manifest mode.** It's the least work for a plain CMake project. Put
this `vcpkg.json` in the repository root:

```json
{
  "name": "image-viewer",
  "version-string": "0.1.0",
  "dependencies": [
    "libjpeg-turbo",
    "tiff",
    "sqlite3",
    "lcms",
    "libpng",
    { "name": "libwebp", "features": ["libwebpmux"] },
    { "name": "libavif", "features": ["aom", "dav1d"] },
    "libjxl",
    "gtest",
    "pkgconf"
  ]
}
```

Port feature names change over time. Check them with `vcpkg search <port>`, or the port's
`vcpkg.json` in the vcpkg repository, and fix the list above. Conan works too: all the libraries
are on Conan Center under the names in the table. Use the `CMakeDeps` and `PkgConfigDeps`
generators.

Configure and build from an "x64 Native Tools Command Prompt for VS 2022":

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
          -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake ^
          -DCMAKE_PREFIX_PATH=C:/Qt/6.x.y/msvc2022_64
    cmake --build build
    ctest --test-dir build

## CMake changes that will probably be needed

`src/core/CMakeLists.txt` and `tests/CMakeLists.txt` find lcms2, WebP, AVIF and JPEG XL with
`pkg_check_modules`. A stock Windows setup has no pkg-config. There are two ways to deal with it:

1. Quick: vcpkg's `pkgconf` port (or Conan's `PkgConfigDeps`) provides `pkg-config` and the `.pc`
   files. Point CMake at it if it isn't found, with
   `-DPKG_CONFIG_EXECUTABLE=<vcpkg_installed>/x64-windows/tools/pkgconf/pkgconf.exe`.
2. Better, and it keeps Linux working: try the libraries' own CMake packages first and fall back
   to pkg-config:
   * WebP: `find_package(WebP CONFIG)`, which gives `WebP::webp`, `WebP::webpdemux` and
     `WebP::libwebpmux`.
   * libavif: `find_package(libavif CONFIG)`, which gives `avif`.
   * lcms2 and libjxl ship no CMake package upstream (vcpkg may add one; check the port's
     `usage` file). Keep pkg-config for them, or write small find modules.

   Wrap each one in an `INTERFACE` target with the current names, so the rest of the CMake files
   don't change.

Also check:

* `JPEG::JPEG`, `TIFF::TIFF`, `PNG::PNG` and `SQLite::SQLite3` come from CMake's built-in find
  modules and should work as they are.
* Tests discover with `gtest_discover_tests`. Running them needs the Qt and vcpkg DLLs on `PATH`,
  or `windeployqt` run on `iv_ui_tests.exe`.

## Running and deploying

* The app has to find the Qt DLLs and QML modules. For running from the build tree, add
  `C:/Qt/6.x.y/msvc2022_64/bin` to `PATH`. vcpkg copies its own DLLs next to the executables.
* For a folder to ship:
  `windeployqt --qmldir src/ui/qml --release build/src/app/imageviewer.exe`. The QML imports used
  are QtQuick, QtQuick.Controls (Fusion style), QtQuick.Layouts, QtQuick.Window and QtCore.
  QtQuick.Dialogs is no longer used.
* The UI tests in `ctest` run on the software renderer. To run the GPU shader tests by hand:
  `set QSG_RHI_BACKEND=d3d11` (or `vulkan`), then
  `build\tests\iv_ui_tests --gtest_filter=ResampledView.*:ColorPreview.*`.

## Parts never run on Windows

Expect bugs here first:

* File operations: rename, trash, move and delete on files that are open. The view may hold a
  TIFF open.
* Paths with drive letters and UNC paths: the path bar, the file chooser, typed paths.
* The file chooser's places (`FolderLists::places`), which name drives "Name (C:)".
* The thumbnail cache location and the settings (QSettings).
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
* The Qt Online Installer asks open-source users to accept its terms. aqtinstall downloads the
  same LGPL binaries. Either way the LGPL obligations above apply, not the installer.
* The other dependencies are permissive (BSD, MIT, IJG, libpng, public domain). Ship their licence
  texts with the app. AV1 (aom, dav1d) also comes with the Alliance for Open Media patent licence,
  which is royalty-free.
