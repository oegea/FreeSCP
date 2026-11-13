#!/usr/bin/env bash
# Fetch/build the dependencies for a DISTRIBUTABLE universal macOS build (arm64 + x86_64) that runs
# on macOS 12+. Homebrew's Qt/OpenSSL are single-architecture and target the build machine's own
# macOS version (an app linked against them only starts on that version or newer), so releases use:
#
#   - the official Qt binaries (universal, macOS 12+), fetched with aqtinstall into a venv
#   - OpenSSL built from the official source tarball, static, once per architecture, lipo'ed
#
# Everything lands in native/build-deps/ (git-ignored). Idempotent: finished steps are skipped.
#
# Usage: native/scripts/deps-macos.sh
# Then:  native/scripts/release-macos.sh <version>
set -euo pipefail

QT_VERSION="${QT_VERSION:-6.8.3}"
OPENSSL_VERSION="${OPENSSL_VERSION:-3.5.9}"      # same series as the headers vendored in libs/openssl
MACOS_MIN="${MACOS_MIN:-12.0}"

NATIVE="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$NATIVE/build-deps"
mkdir -p "$DEPS"

#--- Qt ---------------------------------------------------------------------------------------
QT_PREFIX="$DEPS/qt/$QT_VERSION/macos"
if [ ! -x "$QT_PREFIX/bin/macdeployqt" ]; then
  echo "==> Qt $QT_VERSION (official universal binaries)"
  [ -d "$DEPS/venv" ] || python3 -m venv "$DEPS/venv"
  "$DEPS/venv/bin/pip" install --quiet --upgrade pip aqtinstall
  "$DEPS/venv/bin/aqt" install-qt mac desktop "$QT_VERSION" clang_64 --outputdir "$DEPS/qt" \
    --archives qtbase qtsvg
fi
echo "Qt: $QT_PREFIX ($(lipo -archs "$QT_PREFIX/lib/QtCore.framework/QtCore"))"

#--- OpenSSL ----------------------------------------------------------------------------------
SSL_PREFIX="$DEPS/openssl"
if [ ! -f "$SSL_PREFIX/lib/libssl.a" ]; then
  echo "==> OpenSSL $OPENSSL_VERSION (static, universal, macOS $MACOS_MIN+)"
  TARBALL="$DEPS/openssl-$OPENSSL_VERSION.tar.gz"
  URL="https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz"
  [ -f "$TARBALL" ] || curl -fL --retry 3 -o "$TARBALL" "$URL"
  curl -fL --retry 3 -o "$TARBALL.sha256" "$URL.sha256"
  WANT="$(awk '{print $1}' "$TARBALL.sha256")"
  HAVE="$(shasum -a 256 "$TARBALL" | awk '{print $1}')"
  [ "$WANT" = "$HAVE" ] || { echo "OpenSSL tarball checksum mismatch"; exit 1; }

  SRC="$DEPS/openssl-src"
  rm -rf "$SRC"; mkdir -p "$SRC"
  tar -xzf "$TARBALL" -C "$SRC" --strip-components 1
  for ARCH in arm64 x86_64; do
    B="$DEPS/openssl-build-$ARCH"
    rm -rf "$B"; mkdir -p "$B"
    ( cd "$B"
      "$SRC/Configure" "darwin64-$ARCH-cc" no-shared no-tests no-docs no-apps \
        "-mmacosx-version-min=$MACOS_MIN" --prefix="$B/install" --openssldir=/etc/ssl > configure.log
      make -j"$(sysctl -n hw.ncpu)" build_libs > build.log 2>&1
      make install_dev > install.log 2>&1 )
  done
  mkdir -p "$SSL_PREFIX/lib"
  for L in libssl.a libcrypto.a; do
    lipo -create "$DEPS/openssl-build-arm64/install/lib/$L" "$DEPS/openssl-build-x86_64/install/lib/$L" \
      -output "$SSL_PREFIX/lib/$L"
  done
  # Headers: identical for both architectures except the generated configuration.h, which only
  # differs in settings this build does not vary; the engine compiles against libs/openssl/include.
  rm -rf "$SSL_PREFIX/include"; cp -R "$DEPS/openssl-build-arm64/install/include" "$SSL_PREFIX/include"
  rm -rf "$SRC" "$DEPS"/openssl-build-*
fi
echo "OpenSSL: $SSL_PREFIX ($(lipo -archs "$SSL_PREFIX/lib/libssl.a"))"
