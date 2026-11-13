#!/usr/bin/env bash
# Sign + notarize a staged FreeSCP.app and produce the release zips.
#
# Input: the UNSIGNED universal bundle made by build-macos-universal.sh - locally
# (native/build-universal/dist/FreeSCP.app) or by the "macOS" GitHub Actions workflow
# (artifact FreeSCP-macOS-unsigned -> untar it and pass the path).
#
# Prereqs:
#   - A "Developer ID Application" cert in the keychain (see `security find-identity -v -p codesigning`).
#   - Notarization creds in env or an env file (default: ../roBrowserLegacy/applications/electron/.env.notarize):
#       APPLE_ID, APPLE_APP_SPECIFIC_PASSWORD, APPLE_TEAM_ID
#
# Usage: native/scripts/release-macos.sh <version> [path/to/unsigned/FreeSCP.app]
# Output: native/build/release/FreeSCP-macOS-universal.zip (+ a copy under the pre-0.2 asset name
#         FreeSCP-macOS-arm64.zip, which old links and the 0.1 website point at).
set -euo pipefail

VERSION="${1:?usage: release-macos.sh <version> [FreeSCP.app]}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP_SRC="${2:-$ROOT/native/build-universal/dist/FreeSCP.app}"
STAGE="$ROOT/native/build/release"
APP="$STAGE/FreeSCP.app"
ENTITLEMENTS="$ROOT/native/ui-qt/entitlements.mac.plist"
# Signing identity — override with SIGN_IDENTITY env. (A Team ID is not secret; it's embedded in every
# signed/notarized binary. Set your own to build your own signed copy.)
IDENTITY="${SIGN_IDENTITY:-Developer ID Application: Oriol Egea (3GZ8VZ8D2Y)}"
ENVFILE="${ENVFILE:-$ROOT/../roBrowserLegacy/applications/electron/.env.notarize}"

[ -d "$APP_SRC" ] || { echo "no staged app at $APP_SRC — run build-macos-universal.sh (or fetch the CI artifact)"; exit 1; }
[ -d "$APP_SRC/Contents/Frameworks/QtCore.framework" ] || { echo "$APP_SRC has no bundled Qt (not staged with macdeployqt)"; exit 1; }
HAVE_VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP_SRC/Contents/Info.plist")"
[ "$HAVE_VERSION" = "$VERSION" ] || { echo "bundle is version $HAVE_VERSION, expected $VERSION"; exit 1; }
[ -f "$ENVFILE" ] && set -a && . "$ENVFILE" && set +a || true
: "${APPLE_ID:?APPLE_ID not set}" "${APPLE_APP_SPECIFIC_PASSWORD:?}" "${APPLE_TEAM_ID:?}"

echo "==> staging FreeSCP.app $VERSION ($(lipo -archs "$APP_SRC/Contents/MacOS/winscp-qt"))"
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp -R "$APP_SRC" "$APP"
xattr -cr "$APP"

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
# Stable asset name — the website links to releases/latest/download/FreeSCP-macOS-universal.zip,
# so the filename must NOT carry the version (else the download 404s).
ZIP="$STAGE/FreeSCP-macOS-universal.zip"
/usr/bin/ditto -c -k --keepParent "$APP" "$ZIP"
xcrun notarytool submit "$ZIP" \
  --apple-id "$APPLE_ID" --password "$APPLE_APP_SPECIFIC_PASSWORD" --team-id "$APPLE_TEAM_ID" --wait

echo "==> stapling + repackaging"
xcrun stapler staple "$APP"
spctl -a -vvv "$APP" 2>&1 | head -3 || true
rm -f "$ZIP"; /usr/bin/ditto -c -k --keepParent "$APP" "$ZIP"
cp "$ZIP" "$STAGE/FreeSCP-macOS-arm64.zip"   # legacy asset name (same universal app)
echo "==> artifacts: $ZIP  (+ FreeSCP-macOS-arm64.zip)"
