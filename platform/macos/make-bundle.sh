#!/usr/bin/env bash
# Assemble igi1ed.app from build-macos/bin and (optionally) install it.
#
#   bash platform/macos/make-bundle.sh                 # build build-macos/igi1ed.app
#   bash platform/macos/make-bundle.sh --install DIR   # also copy to DIR/igi1ed.app
#   bash platform/macos/make-bundle.sh --game DIR      # write the game path the app uses
#
# Layout (Utils::GetExeDirectory resolves Contents/MacOS -> Contents/Resources):
#   igi1ed.app/Contents/MacOS/igi1ed          the native arm64 binary
#   igi1ed.app/Contents/Resources/editor/...  asset tree, shaders/21, tools
#   igi1ed.app/Contents/Resources/version
#   igi1ed.app/Contents/Resources/igi-game-path.txt
set -uo pipefail
REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BIN="$REPO/build-macos/bin"
APP="$REPO/build-macos/igi1ed.app"
GAME="${IGI_GAME_PATH:-/Users/haseeb-mir/Downloads/IGI}"
INSTALL=""

while [ $# -gt 0 ]; do
  case "$1" in
    --install) shift; INSTALL="${1:?--install needs a directory}"; shift ;;
    --game)    shift; GAME="${1:?--game needs a directory}"; shift ;;
    *) echo "usage: $0 [--install DIR] [--game DIR]" >&2; exit 2 ;;
  esac
done

if [ ! -x "$BIN/igi1ed" ]; then
  echo "ERROR: $BIN/igi1ed missing; run: cmake --build build-macos --parallel" >&2
  exit 1
fi
if [ ! -d "$BIN/editor" ]; then
  echo "ERROR: $BIN/editor missing; the POST_BUILD asset staging did not run" >&2
  exit 1
fi

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN/igi1ed" "$APP/Contents/MacOS/igi1ed"
cp -R "$BIN/editor" "$APP/Contents/Resources/editor"
cp "$BIN/version" "$APP/Contents/Resources/version" 2>/dev/null || true
printf '%s\n' "$GAME" > "$APP/Contents/Resources/igi-game-path.txt"

VERSION=$(head -1 "$REPO/version" | tr -d '[:space:]')
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>IGI Editor</string>
  <key>CFBundleDisplayName</key><string>IGI Editor</string>
  <key>CFBundleIdentifier</key><string>org.heaven-hm.igi1ed</string>
  <key>CFBundleVersion</key><string>${VERSION}</string>
  <key>CFBundleShortVersionString</key><string>${VERSION}</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleExecutable</key><string>igi1ed</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSPrincipalClass</key><string>NSApplication</string>
</dict>
</plist>
PLIST

chmod +x "$APP/Contents/MacOS/igi1ed"
# Ad-hoc signature: not notarised, but a stable identity so Gatekeeper/TCC does
# not treat every rebuild as a different application.
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || \
  echo "WARN: ad-hoc codesign failed; the app still runs locally" >&2

echo "built $APP"
if [ -n "$INSTALL" ]; then
  mkdir -p "$INSTALL"
  rm -rf "$INSTALL/igi1ed.app"
  cp -R "$APP" "$INSTALL/igi1ed.app"
  echo "installed $INSTALL/igi1ed.app"
fi
