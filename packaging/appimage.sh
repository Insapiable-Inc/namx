#!/usr/bin/env bash
# Build NAMPlayer-x86_64.AppImage from a configured+built tree.
#
#   gui/packaging/appimage.sh [build-dir]      (default: gui/build-release)
#
# Needs linuxdeploy + its Qt plugin (downloaded into gui/build-appimage/tools
# on first run) and a Release build configured with -DNAM_NATIVE_OPT=OFF so
# the binary runs on any AVX2 machine. JACK is NOT bundled on purpose: the
# AppImage links the host's libjack (jack2 or pipewire-jack), which is the
# only way to reach the host's audio server.
set -euo pipefail

GUI_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$GUI_DIR/build-release}"
OUT_DIR="$GUI_DIR/build-appimage"
TOOLS="$OUT_DIR/tools"
APPDIR="$OUT_DIR/AppDir"

[ -x "$BUILD_DIR/NAMPlayer" ] || { echo "no binary at $BUILD_DIR/NAMPlayer — build first"; exit 1; }

mkdir -p "$TOOLS"
fetch() { [ -x "$TOOLS/$1" ] || { curl -fsSL "$2" -o "$TOOLS/$1" && chmod +x "$TOOLS/$1"; }; }
fetch linuxdeploy-x86_64.AppImage \
      https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt-x86_64.AppImage \
      https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage

rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr

# The host provides libjack: exclude it from the bundle so the AppImage talks
# to whatever server the user runs (jackd or PipeWire).
export LINUXDEPLOY_EXCLUDE_LIBS="libjack.so.0"
# linuxdeploy's bundled strip predates .relr.dyn sections (any 2024+ distro
# library); distro Qt libraries are stripped already, so skip it.
export NO_STRIP="${NO_STRIP:-true}"

# linuxdeploy-plugin-qt bundles every plugin under QT_INSTALL_PLUGINS, which
# on a desktop distro drags in third-party image/platform plugins (KDE's
# kimageformats, ibus…) and their dependencies. Hand it a curated plugin tree
# through a qmake wrapper instead: Qt's own platforms, themes, image formats,
# icon engines, TLS and network-information backends.
REAL_QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
QT_PLUGINS="$("$REAL_QMAKE" -query QT_INSTALL_PLUGINS)"
CURATED="$OUT_DIR/qtplugins"
rm -rf "$CURATED"
curate() { # curate <subdir> <glob>…
    local sub="$1"; shift
    [ -d "$QT_PLUGINS/$sub" ] || return 0
    mkdir -p "$CURATED/$sub"
    for pat in "$@"; do
        for f in "$QT_PLUGINS/$sub"/$pat; do [ -e "$f" ] && cp "$f" "$CURATED/$sub/"; done
    done
}
curate platforms            libqxcb.so libqwayland-generic.so libqwayland-egl.so libqoffscreen.so
curate platformthemes       libqgtk3.so libqxdgdesktopportal.so
curate wayland-shell-integration libxdg-shell.so
curate wayland-decoration-client libbradient.so
curate wayland-graphics-integration-client libqt-plugin-wayland-egl.so
curate xcbglintegrations    libqxcb-egl-integration.so libqxcb-glx-integration.so
curate iconengines          libqsvgicon.so
curate tls                  libqopensslbackend.so libqcertonlybackend.so
curate networkinformation   libqnetworkmanager.so libqglib.so
curate platforminputcontexts libcomposeplatforminputcontextplugin.so libibusplatforminputcontextplugin.so
curate imageformats         libqgif.so libqico.so libqjpeg.so libqsvg.so libqwebp.so
# Beyond the plugin's own default (xcb): Wayland when the build box has
# qt6-wayland, and the headless platform (CI smoke test, boxes without a
# display). Only what exists is requested.
extra_platforms=""
for f in "$CURATED"/platforms/*.so; do
    b="$(basename "$f")"; [ "$b" = "libqxcb.so" ] && continue
    extra_platforms="${extra_platforms:+$extra_platforms;}$b"
done
export EXTRA_PLATFORM_PLUGINS="$extra_platforms"
extra_modules=""
for d in "$CURATED"/*/; do
    b="$(basename "$d")"
    case "$b" in platforms|imageformats|iconengines) continue;; esac
    [ -n "$(ls -A "$d")" ] || continue
    extra_modules="${extra_modules:+$extra_modules;}$b"
done
export EXTRA_QT_MODULES="$extra_modules"
cat > "$OUT_DIR/qmake-wrapper" <<EOS
#!/usr/bin/env bash
# linuxdeploy-plugin-qt parses a bare "qmake -query" listing.
if [ "\$1" = "-query" ] && [ -z "\$2" ]; then
    "$REAL_QMAKE" -query | sed "s|^QT_INSTALL_PLUGINS:.*|QT_INSTALL_PLUGINS:$CURATED|"
    exit 0
fi
if [ "\$1" = "-query" ] && [ "\$2" = "QT_INSTALL_PLUGINS" ]; then echo "$CURATED"; exit 0; fi
exec "$REAL_QMAKE" "\$@"
EOS
chmod +x "$OUT_DIR/qmake-wrapper"
export QMAKE="$OUT_DIR/qmake-wrapper"
export OUTPUT="$OUT_DIR/NAMPlayer-x86_64.AppImage"
export VERSION="${VERSION:-$(git -C "$GUI_DIR" describe --tags --always 2>/dev/null | sed 's/^v//')}"

"$TOOLS/linuxdeploy-x86_64.AppImage" \
    --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/cl.pichulero.NAMPlayer.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/cl.pichulero.NAMPlayer.png" \
    --plugin qt \
    --output appimage

echo "==> $OUTPUT"
