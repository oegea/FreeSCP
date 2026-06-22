#!/usr/bin/env bash
# Build a distributable, signed + notarized FreeSCP.app for macOS and a zip artifact.
#
# Prereqs:
#   - A "Developer ID Application" cert in the keychain (see `security find-identity -v -p codesigning`).
#   - Notarization creds in env or an env file (default: ../roBrowserLegacy/applications/electron/.env.notarize):
#       APPLE_ID, APPLE_APP_SPECIFIC_PASSWORD, APPLE_TEAM_ID
#   - Qt (macdeployqt) on PATH via Homebrew.
#
# Usage: native/scripts/release-macos.sh [version]
set -euo pipefail

VERSION="${1:-0.1.0}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP_SRC="$ROOT/native/build/ui-qt/winscp-qt.app"
STAGE="$ROOT/native/build/release"
APP="$STAGE/FreeSCP.app"
ENTITLEMENTS="$ROOT/native/ui-qt/entitlements.mac.plist"
# Signing identity — override with SIGN_IDENTITY env. (A Team ID is not secret; it's embedded in every
# signed/notarized binary. Set your own to build your own signed copy.)
IDENTITY="${SIGN_IDENTITY:-Developer ID Application: Oriol Egea (3GZ8VZ8D2Y)}"
ENVFILE="${ENVFILE:-$ROOT/../roBrowserLegacy/applications/electron/.env.notarize}"
MACDEPLOYQT="$(brew --prefix qt)/bin/macdeployqt"

[ -d "$APP_SRC" ] || { echo "build the app first: cmake --build native/build --target winscp-qt"; exit 1; }
[ -f "$ENVFILE" ] && set -a && . "$ENVFILE" && set +a || true
: "${APPLE_ID:?APPLE_ID not set}" "${APPLE_APP_SPECIFIC_PASSWORD:?}" "${APPLE_TEAM_ID:?}"

echo "==> staging a fresh FreeSCP.app"
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp -R "$APP_SRC" "$APP"

echo "==> bundling Qt (macdeployqt)"
"$MACDEPLOYQT" "$APP" -no-strip

echo "==> deep codesign (hardened runtime + timestamp)"
# Sign nested code first (frameworks, plugins, dylibs), then the app bundle.
find "$APP/Contents" \( -name "*.dylib" -o -name "*.framework" -o -perm -111 -type f \) -print0 2>/dev/null \
  | while IFS= read -r -d '' f; do
      codesign --force --options runtime --timestamp -s "$IDENTITY" "$f" 2>/dev/null || true
    done
codesign --force --deep --options runtime --timestamp \
  --entitlements "$ENTITLEMENTS" -s "$IDENTITY" "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"

echo "==> notarizing"
ZIP="$STAGE/FreeSCP-macos-arm64-$VERSION.zip"
/usr/bin/ditto -c -k --keepParent "$APP" "$ZIP"
xcrun notarytool submit "$ZIP" \
  --apple-id "$APPLE_ID" --password "$APPLE_APP_SPECIFIC_PASSWORD" --team-id "$APPLE_TEAM_ID" --wait

echo "==> stapling + repackaging"
xcrun stapler staple "$APP"
spctl -a -vvv "$APP" 2>&1 | head -3 || true
rm -f "$ZIP"; /usr/bin/ditto -c -k --keepParent "$APP" "$ZIP"
echo "==> artifact: $ZIP"
