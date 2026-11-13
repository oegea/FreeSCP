#!/usr/bin/env bash
# Build the DISTRIBUTABLE macOS app: one universal binary (Apple Silicon + Intel), macOS 12+,
# with the official Qt frameworks bundled and OpenSSL linked statically. Unsigned - sign and
# notarize the result with release-macos.sh.
#
# Output (native/build-universal/dist/):
#   FreeSCP.app                 the staged bundle (Qt deployed, not signed)
#   winscp-harness              the engine test harness (universal), for end-to-end checks
#   FreeSCP-macOS-unsigned.tar  both of the above (tar keeps the framework symlinks intact)
#
# Usage: native/scripts/build-macos-universal.sh
set -euo pipefail

MACOS_MIN="${MACOS_MIN:-12.0}"
NATIVE="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$NATIVE/build-deps"
BUILD="$NATIVE/build-universal"
DIST="$BUILD/dist"

MACOS_MIN="$MACOS_MIN" "$NATIVE/scripts/deps-macos.sh"
QT_PREFIX="$(ls -d "$DEPS"/qt/*/macos | sort | tail -1)"

GEN=(); command -v ninja >/dev/null && GEN=(-G Ninja)
cmake -B "$BUILD" -S "$NATIVE" ${GEN[@]+"${GEN[@]}"} \
  -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_MIN" \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
  -DOPENSSL_ROOT_DIR="$DEPS/openssl" -DOPENSSL_USE_STATIC_LIBS=TRUE
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"

echo "==> staging FreeSCP.app"
rm -rf "$DIST"; mkdir -p "$DIST"
cp -R "$BUILD/ui-qt/winscp-qt.app" "$DIST/FreeSCP.app"
rm -f "$DIST/FreeSCP.app"/Contents/MacOS/*.tmp "$DIST/FreeSCP.app"/Contents/MacOS/*.ini 2>/dev/null || true
"$QT_PREFIX/bin/macdeployqt" "$DIST/FreeSCP.app" -no-strip
cp "$BUILD/harness/winscp-harness" "$DIST/winscp-harness"

echo "==> checks"
BIN="$DIST/FreeSCP.app/Contents/MacOS/winscp-qt"
echo "architectures: $(lipo -archs "$BIN")"
[ "$(lipo -archs "$BIN" | tr ' ' '\n' | sort | tr '\n' ' ')" = "arm64 x86_64 " ] || { echo "not universal"; exit 1; }
for ARCH in arm64 x86_64; do
  MIN="$(otool -arch "$ARCH" -l "$BIN" | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print $2; exit}')"
  echo "minimum macOS ($ARCH): $MIN"
  [ "$MIN" = "$MACOS_MIN" ] || { echo "unexpected deployment target"; exit 1; }
done
# Nothing may point outside the bundle/system (a stray /opt/homebrew or build-deps path = broken app elsewhere).
if otool -L "$BIN" "$DIST/FreeSCP.app"/Contents/Frameworks/*.framework/Versions/A/Qt* 2>/dev/null \
     | grep -E '^\s+/' | grep -vE '^\s+(/usr/lib/|/System/Library/)' ; then
  echo "non-system absolute library reference found (above)"; exit 1
fi

( cd "$DIST" && tar -cf FreeSCP-macOS-unsigned.tar FreeSCP.app winscp-harness )
echo "==> $DIST/FreeSCP-macOS-unsigned.tar"
