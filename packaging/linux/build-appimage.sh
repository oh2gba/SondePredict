#!/usr/bin/env bash
# Builds SondePredict-<version>-x86_64.AppImage with linuxdeploy and its Qt plugin.
#
#   packaging/linux/build-appimage.sh <qt-prefix> [extra-cmake-prefix]
#
# <qt-prefix> is the Qt installation, e.g. .toolchain/Qt/6.11.3/gcc_64. The optional second
# argument is added to CMAKE_PREFIX_PATH (e.g. .toolchain/sysroot/usr for local GL headers).
# linuxdeploy is downloaded to packaging/linux/tools on first use.
set -euo pipefail

QT_PREFIX=$(realpath "${1:?usage: $0 <qt-prefix> [extra-cmake-prefix]}")
EXTRA_PREFIX=${2:+$(realpath "$2")}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD="$ROOT/build-appimage"
APPDIR="$BUILD/AppDir"
TOOLS="$ROOT/packaging/linux/tools"

mkdir -p "$TOOLS"
for tool in linuxdeploy-x86_64.AppImage linuxdeploy-plugin-qt-x86_64.AppImage; do
    if [ ! -x "$TOOLS/$tool" ]; then
        case $tool in
            linuxdeploy-x86_64.AppImage) repo=linuxdeploy ;;
            *) repo=linuxdeploy-plugin-qt ;;
        esac
        curl -fsSL -o "$TOOLS/$tool" "https://github.com/linuxdeploy/$repo/releases/download/continuous/$tool"
        chmod +x "$TOOLS/$tool"
    fi
done

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSP_BUILD_TESTS=OFF \
      -DCMAKE_PREFIX_PATH="$QT_PREFIX${EXTRA_PREFIX:+;$EXTRA_PREFIX}"
cmake --build "$BUILD"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD" --prefix /usr

VERSION=$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$ROOT/CMakeLists.txt" | head -1)
export VERSION
# Show linuxdeploy a copy of the Qt plugin folder with SQLite as the only SQL driver; the
# others (MySQL, PostgreSQL, ...) need client libraries we neither have nor want to bundle.
# The position plugins (GPS sources such as NMEA, which needs QtSerialPort) are left out too:
# Sonde Predict never reads a GPS.
PLUGINS="$BUILD/qt-plugins"
rm -rf "$PLUGINS" && mkdir -p "$PLUGINS/sqldrivers"
for dir in "$QT_PREFIX"/plugins/*; do
    case "$(basename "$dir")" in
        sqldrivers|position) ;;
        *) ln -s "$dir" "$PLUGINS/" ;;
    esac
done
ln -s "$QT_PREFIX/plugins/sqldrivers/libqsqlite.so" "$PLUGINS/sqldrivers/"
printf '#!/bin/sh\n"%s" "$@" | sed "s|^QT_INSTALL_PLUGINS\\([^:]*\\):.*|QT_INSTALL_PLUGINS\\1:%s|"\n' \
    "$QT_PREFIX/bin/qmake" "$PLUGINS" > "$BUILD/qmake"
chmod +x "$BUILD/qmake"
export QMAKE="$BUILD/qmake"
export LD_LIBRARY_PATH="$QT_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QML_SOURCES_PATHS="$ROOT/src/app/qml"
# Qt modules whose plugins must be bundled even though nothing links them directly:
# map tiles (location → geoservices), the database (sql → sqlite) and HTTPS (network → tls).
export EXTRA_QT_MODULES="location;positioning;sql;network;svg"
export EXTRA_PLATFORM_PLUGINS="libqwayland.so;libqoffscreen.so" # offscreen: headless smoke tests
export PATH="$TOOLS:$PATH"

cd "$BUILD"
LINUXDEPLOY=("$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR")
APPIMAGE_EXTRACT_AND_RUN=1 "${LINUXDEPLOY[@]}" \
    --desktop-file "$APPDIR/usr/share/applications/sondepredict.desktop" \
    --icon-file "$ROOT/packaging/icons/sondepredict-256.png" --icon-filename sondepredict \
    --plugin qt

# The Qt plugin bundles the Wayland platform plugin but not its EGL client integration;
# without it the window cannot draw on Wayland desktops ("Failed to load client buffer
# integration: wayland-egl"). Add it, with its libraries, before packing.
GRAPHICS="$APPDIR/usr/plugins/wayland-graphics-integration-client"
mkdir -p "$GRAPHICS"
cp "$QT_PREFIX/plugins/wayland-graphics-integration-client/libqt-plugin-wayland-egl.so" "$GRAPHICS/"

# Headless tests cannot notice a missing display or graphics plugin, so check for them here.
for plugin in platforms/libqxcb.so platforms/libqwayland.so \
              wayland-graphics-integration-client/libqt-plugin-wayland-egl.so \
              xcbglintegrations/libqxcb-egl-integration.so geoservices/libqtgeoservices_osm.so \
              sqldrivers/libqsqlite.so tls/libqopensslbackend.so; do
    [ -f "$APPDIR/usr/plugins/$plugin" ] || { echo "error: $plugin is missing from the AppImage" >&2; exit 1; }
done

APPIMAGE_EXTRACT_AND_RUN=1 "${LINUXDEPLOY[@]}" --deploy-deps-only "$GRAPHICS" --output appimage
ls -l "$BUILD"/Sonde_Predict-*.AppImage
