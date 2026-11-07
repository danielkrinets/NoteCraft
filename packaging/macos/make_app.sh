#!/bin/sh
# Builds NoteCraft.app around a compiled binary.
#   packaging/macos/make_app.sh <path/to/notecraft> <version> <output-dir>
set -e
cd "$(dirname "$0")/../.."
BIN="$1"; VER="$2"; OUT="$3"
[ -x "$BIN" ] && [ -n "$VER" ] && [ -n "$OUT" ] || { echo "usage: $0 <binary> <version> <outdir>" >&2; exit 2; }
APP="$OUT/NoteCraft.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/notecraft"
cp assets/NoteCraft.icns "$APP/Contents/Resources/NoteCraft.icns"
sed "s/@VERSION@/$VER/g" packaging/macos/Info.plist.in > "$APP/Contents/Info.plist"
# Ad-hoc signature: required for arm64 binaries to run; not a notarised developer signature.
codesign --force --deep --sign - "$APP"
echo "built $APP"
