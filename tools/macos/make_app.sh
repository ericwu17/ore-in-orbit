#!/bin/sh
# make_app.sh — package the game binary as a self-contained macOS .app.
#
# Usage: tools/macos/make_app.sh EXE APP_DIR      (normally via `make app`)
#
# Assets are compiled into the binary and saves live in SDL's pref path, so
# the bundle only needs the binary, the SDL libraries, an icon and an
# Info.plist. The SDL libraries come from wherever sdl2-config points (the
# one the game was linked against): Homebrew locally, or the from-source
# build in tools/macos/build_sdl.sh for a bundle that runs on older macOS.
#
# The bundle runs on the oldest macOS that every binary in it supports
# (their "minos"). That becomes LSMinimumSystemVersion, and the script prints it.
#
# Environment: APP_VERSION (default 0.1.0) sets the version in the Info.plist.

set -eu

EXE=${1:?usage: $0 EXE APP_DIR}
APP=${2:?usage: $0 EXE APP_DIR}
REPO=$(cd "$(dirname "$0")/../.." && pwd)

EXE_NAME=$(basename "$EXE")
CONTENTS="$APP/Contents"
FRAMEWORKS="$CONTENTS/Frameworks"

# --- locate SDL ------------------------------------------------------------
# sdl2-compat (libSDL2) loads SDL3 at runtime, so both ship. SDL3 sits next
# to libSDL2 in a from-source prefix; with Homebrew it's a separate keg.
SDL_LIB_DIR="$(sdl2-config --prefix)/lib"
SDL2_LIB="$SDL_LIB_DIR/libSDL2-2.0.0.dylib"
SDL3_LIB="$SDL_LIB_DIR/libSDL3.0.dylib"
if [ ! -e "$SDL3_LIB" ] && command -v brew >/dev/null; then
    SDL3_LIB="$(brew --prefix sdl3)/lib/libSDL3.0.dylib"
fi
for lib in "$SDL2_LIB" "$SDL3_LIB"; do
    [ -e "$lib" ] || { echo "make_app.sh: missing $lib" >&2; exit 1; }
done

# --- lay out the bundle ----------------------------------------------------
rm -rf "$APP"
mkdir -p "$CONTENTS/MacOS" "$FRAMEWORKS" "$CONTENTS/Resources"
cp "$EXE" "$CONTENTS/MacOS/$EXE_NAME"
cp -L "$SDL2_LIB" "$FRAMEWORKS/libSDL2-2.0.0.dylib"
# libSDL3.dylib: the name sdl2-compat looks for next to itself (@loader_path).
cp -L "$SDL3_LIB" "$FRAMEWORKS/libSDL3.dylib"
chmod u+w "$FRAMEWORKS"/*

# Point everything at the bundled copies. install_name_tool's "invalidates
# the code signature" warnings are expected: we re-sign below.
BIN="$CONTENTS/MacOS/$EXE_NAME"
install_name_tool -id @rpath/libSDL2-2.0.0.dylib "$FRAMEWORKS/libSDL2-2.0.0.dylib" 2>/dev/null
install_name_tool -id @rpath/libSDL3.dylib "$FRAMEWORKS/libSDL3.dylib" 2>/dev/null
linked_sdl2=$(otool -L "$BIN" | awk '/libSDL2/ { print $1; exit }')
install_name_tool -change "$linked_sdl2" @rpath/libSDL2-2.0.0.dylib "$BIN" 2>/dev/null
if ! otool -l "$BIN" | grep -q '@executable_path/../Frameworks'; then
    install_name_tool -add_rpath @executable_path/../Frameworks "$BIN" 2>/dev/null
fi

# --- check what we built ---------------------------------------------------
# Nothing may still reference a library outside the bundle and the OS.
stray=$(for f in "$BIN" "$FRAMEWORKS"/*; do otool -L "$f" | tail -n +2; done |
    awk '{ print $1 }' | grep -vE '^(@rpath/|/System/Library/|/usr/lib/)' || true)
if [ -n "$stray" ]; then
    echo "make_app.sh: the bundle still references libraries outside it:" >&2
    echo "$stray" >&2
    exit 1
fi

# The bundle's minimum macOS is the highest minos among its binaries.
min_macos=$(for f in "$BIN" "$FRAMEWORKS"/*; do
        otool -l "$f" | awk '/LC_BUILD_VERSION/ { b = 1 } b && $1 == "minos" { print $2; exit }'
    done | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)

# --- icon ------------------------------------------------------------------
# The logo isn't square: pad it to a transparent square rather than stretching.
ICONSET=$(mktemp -d)/AppIcon.iconset
mkdir -p "$ICONSET"
LOGO="$REPO/assets/logo.png"
w=$(sips -g pixelWidth "$LOGO" | awk '/pixelWidth/ { print $2 }')
h=$(sips -g pixelHeight "$LOGO" | awk '/pixelHeight/ { print $2 }')
side=$(( w > h ? w : h ))
sips -p "$side" "$side" "$LOGO" --out "$ICONSET/square.png" >/dev/null
for s in 16 32 128 256 512; do
    sips -z "$s" "$s" "$ICONSET/square.png" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    sips -z $((s * 2)) $((s * 2)) "$ICONSET/square.png" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
rm "$ICONSET/square.png"
iconutil -c icns "$ICONSET" -o "$CONTENTS/Resources/AppIcon.icns"
rm -rf "$(dirname "$ICONSET")"

# --- Info.plist ------------------------------------------------------------
version=${APP_VERSION:-0.1.0}
cat > "$CONTENTS/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>               <string>Ore in Orbit</string>
    <key>CFBundleDisplayName</key>        <string>Ore in Orbit</string>
    <key>CFBundleIdentifier</key>         <string>io.github.ericwu17.ore-in-orbit</string>
    <key>CFBundleExecutable</key>         <string>$EXE_NAME</string>
    <key>CFBundleIconFile</key>           <string>AppIcon</string>
    <key>CFBundlePackageType</key>        <string>APPL</string>
    <key>CFBundleShortVersionString</key> <string>$version</string>
    <key>CFBundleVersion</key>            <string>$version</string>
    <key>LSMinimumSystemVersion</key>     <string>$min_macos</string>
    <key>LSApplicationCategoryType</key>  <string>public.app-category.strategy-games</string>
    <key>NSHighResolutionCapable</key>    <true/>
</dict>
</plist>
EOF
plutil -lint "$CONTENTS/Info.plist" >/dev/null

# --- sign ------------------------------------------------------------------
# Ad-hoc ("-"): Apple Silicon refuses to run unsigned code. Libraries first,
# then the bundle. This is not a Developer ID signature, so Gatekeeper still
# warns on other Macs (right-click > Open the first time).
codesign --force --sign - "$FRAMEWORKS"/*.dylib
codesign --force --sign - "$APP"
codesign --verify --strict "$APP"

echo "Built $APP (macOS $min_macos or later, $(lipo -archs "$BIN"))"
