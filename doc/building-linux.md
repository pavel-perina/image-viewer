# Building the Linux AppImage

Day-to-day Linux builds use the system's `-devel` packages (see `README.md`). The AppImage is
built differently, in a container, so that it runs on other distributions and ships only
libraries whose licences allow it.

    installer/build-appimage.sh            # Podman; ENGINE=docker for Docker

The AppImage lands in `dist/ImageViewer-<version>-x86_64.AppImage`. The first run builds the
container image and the libraries from source; later runs rebuild only the program.

## What the container holds

`installer/linux/Containerfile`:

| What | Version | Why |
| --- | --- | --- |
| AlmaLinux 9 | glibc 2.34 | Qt 6.12's official binaries need glibc 2.34, so this adds no floor of its own. The AppImage runs on Fedora 35+, openSUSE Leap 16 and Tumbleweed, Ubuntu 22.04+, Debian 12+. |
| gcc-toolset-14 | GCC 14 | C++23. The toolset links what the newer library adds into the program, so it needs only the system's libstdc++ (`GLIBCXX_3.4.29`). `std::stacktrace` is not available, as the toolset has no `libstdc++exp`. |
| Qt | 6.12.1 | Official binaries via `aqtinstall`, with Shader Tools and Wayland, and the qtbase sources' licence texts. Not built from source. |
| Conan | 2.34.0 | The libraries, from the same `conanfile.txt` as Windows, with `installer/linux/conan-profile`. jbig-kit (GPL) stays off. |
| uv | 0.13.9 | Installs aqtinstall, Conan, CMake and Ninja at fixed versions, each in its own environment. |
| linuxdeploy and its Qt plugin | continuous | Gather the libraries, Qt's plugins and QML modules, and make the AppImage. |

Two named volumes keep the Conan cache (`imageviewer-conan`) and the build tree
(`imageviewer-build`) between runs. About 6 to 8 GB in all.

## What the script does

1. Builds the libraries with Conan, then the program with every format required
   (`-DIV_REQUIRE_ALL_FORMATS=ON`), without tests.
2. Installs into an AppDir: the program, `imageviewer.desktop` and the icon (a placeholder).
3. Copies the licence texts to `usr/share/imageviewer/licenses`, where the About box finds them:
   ours, Inter's, Tabler's, Qt's and each Conan package's.
4. Runs linuxdeploy with the Qt plugin. Qt chooses Wayland or X11 when it starts
   (`QT_QPA_PLATFORM=xcb` or `wayland` forces one), so both go in. linuxdeploy brings xcb and
   Wayland's platform plugin, but not Wayland's graphics integrations (without `wayland-egl` no
   window opens), shell integrations or title bars (which GNOME leaves to the program); the
   script copies those from Qt, has linuxdeploy gather their libraries, and fails if any is
   missing.
5. Checks every bundled library's licence (`installer/linux/check_licences.py`) and fails on
   anything GPL; the LGPL and the GCC runtime's exception are fine.
6. Makes the AppImage.

## Why not the system's libraries

The distributions build their libraries with options of their own. openSUSE's libtiff links
jbig-kit and its Qt Network links libproxy, both GPL, which an AppImage would bundle. Conan builds
the same versions with the same options as on Windows, where the licences were already checked.
