#!/usr/bin/env bash
# Package the Linux x86-64 build into a self-contained FreeSCP-Linux-x86_64.AppImage.
#
# Bundles the Qt6 runtime + the xcb platform plugin (and their shared-lib closure) so the
# AppImage runs on a stock desktop without qt6-base installed. Core glibc and GPU/driver libs
# are intentionally NOT bundled (must come from the host).
#
# Prereqs: a finished build (native/scripts/build-linux.sh) and `appimagetool` on PATH
# (https://github.com/AppImage/appimagetool/releases). Usage: native/scripts/package-linux.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NATIVE="$ROOT/native"
BUILD="$NATIVE/build-linux"
BIN="$BUILD/ui-qt/winscp-qt"
QTLIB="/usr/lib/x86_64-linux-gnu"
QTPLUGINS="$QTLIB/qt6/plugins"
APPDIR="$BUILD/FreeSCP.AppDir"
OUT="$ROOT/FreeSCP-Linux-x86_64.AppImage"

[ -x "$BIN" ] || { echo "build first: native/scripts/build-linux.sh"; exit 1; }
command -v appimagetool >/dev/null || { echo "appimagetool not on PATH"; exit 1; }

# Libs that MUST come from the host (glibc core + GPU/X driver stack) — never bundle these.
is_excluded() {
  case "$1" in
    ld-linux*|libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libresolv.so*|\
    libGL.so*|libGLX.so*|libGLdispatch.so*|libEGL.so*|libOpenGL.so*|libdrm.so*|\
    libgbm.so*|libglapi.so*) return 0 ;;
    *) return 1 ;;
  esac
}

# Recursively collect the .so closure of a binary into APPDIR/usr/lib.
collect_libs() {
  local target="$1"
  ldd "$target" 2>/dev/null | awk '/=> \//{print $3}' | while read -r so; do
    [ -f "$so" ] || continue
    local base; base="$(basename "$so")"
    is_excluded "$base" && continue
    [ -f "$APPDIR/usr/lib/$base" ] && continue
    cp -L "$so" "$APPDIR/usr/lib/$base"
    collect_libs "$so"   # pull this lib's own deps too
  done
}

echo "Staging $APPDIR ..."
rm -rf "$APPDIR" "$OUT"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/plugins/platforms" \
         "$APPDIR/usr/plugins/imageformats"
cp "$BIN" "$APPDIR/usr/bin/winscp-qt"

# Qt platform plugin (xcb) + image plugins, and their lib closures.
cp "$QTPLUGINS/platforms/libqxcb.so" "$APPDIR/usr/plugins/platforms/"
for p in libqxcb-glx-integration.so; do
  [ -f "$QTPLUGINS/xcbglintegrations/$p" ] && { mkdir -p "$APPDIR/usr/plugins/xcbglintegrations"; \
    cp "$QTPLUGINS/xcbglintegrations/$p" "$APPDIR/usr/plugins/xcbglintegrations/"; }
done
for img in libqjpeg.so libqgif.so libqico.so libqsvg.so; do
  [ -f "$QTPLUGINS/imageformats/$img" ] && cp "$QTPLUGINS/imageformats/$img" "$APPDIR/usr/plugins/imageformats/"
done

collect_libs "$APPDIR/usr/bin/winscp-qt"
collect_libs "$APPDIR/usr/plugins/platforms/libqxcb.so"
for so in "$APPDIR"/usr/plugins/*/*.so; do collect_libs "$so"; done

# Icon (reuse the website/app icon if present).
ICON_SRC="$NATIVE/ui-qt/icon.png"
[ -f "$ICON_SRC" ] || ICON_SRC="$ROOT/icon.png"
cp "$ICON_SRC" "$APPDIR/freescp.png" 2>/dev/null || :
cp "$APPDIR/freescp.png" "$APPDIR/.DirIcon" 2>/dev/null || :

cat > "$APPDIR/FreeSCP.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=FreeSCP
Exec=winscp-qt
Icon=freescp
Categories=Network;FileTransfer;Utility;
Terminal=false
EOF

cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/usr/plugins/platforms"
exec "$HERE/usr/bin/winscp-qt" "$@"
EOF
chmod +x "$APPDIR/AppRun"

echo "Building AppImage ..."
ARCH=x86_64 appimagetool "$APPDIR" "$OUT"
echo
echo "Built: $OUT"
ls -la "$OUT"
