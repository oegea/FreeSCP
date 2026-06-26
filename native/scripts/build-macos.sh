#!/usr/bin/env bash
# Build FreeSCP for macOS (Apple Silicon, dev build — not signed/notarized).
# For a distributable signed+notarized .app use release-macos.sh instead.
#
# Requires Xcode CLT (clang), cmake, ninja, and Qt6 via Homebrew:
#   brew install qt cmake ninja nasm autoconf automake libtool pkg-config
#
# Usage: native/scripts/build-macos.sh [Release|Debug|RelWithDebInfo]   (default Release)
set -euo pipefail

BUILD_TYPE="${1:-Release}"
NATIVE="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$NATIVE/build"

command -v cmake >/dev/null || { echo "cmake not found — brew install cmake."; exit 1; }
command -v brew  >/dev/null || { echo "Homebrew not found — needed to locate Qt."; exit 1; }
QT_PREFIX="$(brew --prefix qt)"

cmake -B "$BUILD" -S "$NATIVE" -G Ninja \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD"

echo
echo "Built ($BUILD_TYPE):"
echo "  GUI:     $BUILD/ui-qt/winscp-qt.app"
echo "  harness: $BUILD/harness/winscp-harness"
echo "Run the GUI:  open $BUILD/ui-qt/winscp-qt.app"
