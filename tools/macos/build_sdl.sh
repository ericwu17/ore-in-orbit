#!/bin/sh
# build_sdl.sh — build SDL3 and sdl2-compat from source for an older macOS.
#
# Homebrew's SDL is built for the macOS version of the machine it was built
# on, so an app bundling it only runs on that version and newer. This builds
# both libraries with MACOSX_DEPLOYMENT_TARGET (default 11.0, the first macOS
# on Apple Silicon) and installs them into PREFIX. Put PREFIX/bin first on
# PATH and the game's build picks them up through sdl2-config.
#
# We link against SDL2's API, but sdl2-compat is SDL2 implemented on top of
# SDL3 (it loads SDL3 at runtime), so both are needed. It's what Homebrew's
# sdl2 is these days too.
#
# Usage: tools/macos/build_sdl.sh PREFIX
# Used by .github/workflows/macos-app.yml; works locally too (needs cmake).

set -eu

SDL3_VERSION=3.4.20
SDL2_COMPAT_VERSION=2.32.74

PREFIX=${1:?usage: $0 PREFIX}
export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"

mkdir -p "$PREFIX"
PREFIX=$(cd "$PREFIX" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# build NAME URL [extra cmake args...]
build() {
    name=$1 url=$2
    shift 2
    echo "==> $name (macOS $MACOSX_DEPLOYMENT_TARGET+)"
    curl -fsSL "$url" | tar -xz -C "$WORK"
    cmake -S "$WORK/$name" -B "$WORK/$name/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
        -DCMAKE_PREFIX_PATH="$PREFIX" \
        "$@"
    cmake --build "$WORK/$name/build" --parallel "$(sysctl -n hw.ncpu)"
    cmake --install "$WORK/$name/build"
}

build "SDL3-$SDL3_VERSION" \
    "https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/SDL3-$SDL3_VERSION.tar.gz" \
    -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_STATIC=OFF

build "sdl2-compat-$SDL2_COMPAT_VERSION" \
    "https://github.com/libsdl-org/sdl2-compat/releases/download/release-$SDL2_COMPAT_VERSION/sdl2-compat-$SDL2_COMPAT_VERSION.tar.gz" \
    -DSDL2COMPAT_TESTS=OFF -DSDL2COMPAT_STATIC=OFF
