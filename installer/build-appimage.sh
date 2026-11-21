#!/bin/bash
# Builds the Linux AppImage in a container, as doc/building-linux.md describes. Run from the
# repository root; the AppImage lands in dist/. The container image, the Conan cache and the
# build tree persist between runs, so only the first run builds the libraries.
#
#   installer/build-appimage.sh          # with Podman, or ENGINE=docker
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
engine=${ENGINE:-podman}
image=localhost/imageviewer-appimage

if [[ ${1:-} != --inside ]]; then
    "$engine" build -t "$image" -f "$root/installer/linux/Containerfile" "$root/installer/linux"
    "$engine" volume create imageviewer-conan >/dev/null 2>&1 || true
    "$engine" volume create imageviewer-build >/dev/null 2>&1 || true
    exec "$engine" run --rm -v "$root:/src:Z" -v imageviewer-conan:/root/.conan2 \
        -v imageviewer-build:/build "$image" /src/installer/build-appimage.sh --inside
fi

# Inside the container.
source /opt/rh/gcc-toolset-14/enable
git config --global --add safe.directory /src

conan profile detect --exist-ok >/dev/null
# No tests are built, so GoogleTest (a test_requires) is skipped.
conan install /src -of /build/conan -pr:a /src/installer/linux/conan-profile --build=missing \
    -c tools.graph:skip_test=True -f json > /build/graph.json
source /build/conan/conanbuild.sh

cmake -S /src -B /build/app -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=/build/conan/conan_toolchain.cmake \
    -DCMAKE_PREFIX_PATH="$QT_DIR/gcc_64" -DCMAKE_INSTALL_PREFIX=/usr \
    -DIV_BUILD_TESTS=OFF -DIV_REQUIRE_ALL_FORMATS=ON
cmake --build /build/app

appdir=/build/AppDir
rm -rf "$appdir"
DESTDIR="$appdir" cmake --install /build/app

# Licence texts the libraries require to be shipped, where the About box looks for them.
licences="$appdir/usr/share/imageviewer/licenses"
mkdir -p "$licences"/{image-viewer,inter,tabler-icons,qt}
cp /src/LICENSE "$licences/image-viewer/"
cp /src/assets/fonts/Inter-LICENSE.txt "$licences/inter/"
cp /src/assets/icons/tabler/LICENSE "$licences/tabler-icons/"
cp "$QT_DIR"/Src/qtbase/LICENSES/*GPL* "$licences/qt/"
python3 - /build/graph.json "$licences" <<'PY'
import json, os, shutil, sys
graph, target = sys.argv[1:3]
for node in json.load(open(graph))["graph"]["nodes"].values():
    folder = node.get("package_folder")
    if node.get("context") != "host" or not folder:
        continue
    source = os.path.join(folder, "licenses")
    if not os.path.isdir(source):
        sys.exit(f"{node['ref']} has no licence files")
    shutil.copytree(source, os.path.join(target, node["ref"].split("/")[0]), dirs_exist_ok=True)
PY

# Libraries, Qt's plugins and the QML modules the QML imports. Our own modules are compiled into
# the program; the scanner still wants them on disk, as built.
export QMAKE="$QT_DIR/gcc_64/bin/qmake" QML_SOURCES_PATHS=/src/src/ui/qml QML_MODULES_PATHS=/build/app/qml
export EXTRA_PLATFORM_PLUGINS="libqwayland.so"
export LD_LIBRARY_PATH="$QT_DIR/gcc_64/lib:${LD_LIBRARY_PATH:-}"
version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' /build/app/CMakeCache.txt)
export LDAI_OUTPUT="/src/dist/ImageViewer-$version-x86_64.AppImage"
mkdir -p /src/dist
cd /build
linuxdeploy-x86_64.AppImage --appdir "$appdir" --plugin qt

# Qt chooses Wayland or X11 when it starts, so both go in. xcb comes with the Qt plugin; Wayland's
# platform plugin needs its graphics integrations (wayland-egl, without which no window opens),
# shell integrations, and title bars (GNOME leaves them to the program). linuxdeploy does not
# gather those, so they are copied here, and their libraries gathered after. Qt's plugins find
# its libraries at $ORIGIN/../../lib, which in the AppDir is usr/lib.
plugins="$appdir/usr/plugins"
deps=()
for folder in wayland-graphics-integration-client wayland-shell-integration wayland-decoration-client; do
    cp -r "$QT_DIR/gcc_64/plugins/$folder" "$plugins/"
    deps+=(--deploy-deps-only="$plugins/$folder")
done
for needed in platforms/libqwayland.so platforms/libqxcb.so wayland-graphics-integration-client/libqt-plugin-wayland-egl.so; do
    [[ -f "$plugins/$needed" ]] || { echo "Missing from the AppDir: $needed" >&2; exit 1; }
done
linuxdeploy-x86_64.AppImage --appdir "$appdir" "${deps[@]}"

python3 /src/installer/linux/check_licences.py "$appdir" /build/graph.json "$QT_DIR"
linuxdeploy-x86_64.AppImage --appdir "$appdir" --output appimage
echo "Made $LDAI_OUTPUT"
