#!/usr/bin/env bash
# Build FreeSCP for Linux x86-64 (dev build — not packaged).
#
# Requires clang (the engine needs -fshort-wchar / -fms-extensions / __declspec(property);
# gcc cannot build it) plus cmake, ninja, Qt6, OpenSSL. On Debian/Ubuntu:
#   sudo apt-get install -y clang lld cmake ninja-build qt6-base-dev libssl-dev \
#       nasm autoconf automake libtool pkg-config zlib1g-dev libgl1-mesa-dev
#
# Usage: native/scripts/build-linux.sh [Release|Debug|RelWithDebInfo]   (default Release)
set -euo pipefail

BUILD_TYPE="${1:-Release}"
NATIVE="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$NATIVE/build-linux"

command -v clang   >/dev/null || { echo "clang not found — install it (see header)."; exit 1; }
command -v ninja   >/dev/null || { echo "ninja not found — apt-get install ninja-build."; exit 1; }

cmake -B "$BUILD" -S "$NATIVE" -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD"

echo
echo "Built ($BUILD_TYPE):"
echo "  GUI:     $BUILD/ui-qt/winscp-qt"
echo "  harness: $BUILD/harness/winscp-harness"
echo "Run the GUI:  $BUILD/ui-qt/winscp-qt"
