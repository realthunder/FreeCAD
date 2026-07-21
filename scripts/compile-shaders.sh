#!/bin/bash
# Recompile the bgfx shaders and refresh the copies the running/next build
# loads, so a shader edit takes effect without a full ninja.
#
#   shaders/compile.sh writes assets/shaders/{glsl,spirv,essl}/*.bin (the
#   committed source assets). The desktop backend loads from the build tree's
#   share/Renderer copy; the WASM viewer embeds them at link time. This copies
#   the fresh bins into the build-tree share dir. Rebuild the WASM viewer
#   separately (cmake --build build/wasm) to re-embed for the browser.
#
# Usage:  scripts/compile-shaders.sh [build_dir]
#   build_dir  default: build/conda-debug
set -eu
REPO=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-"$REPO/build/conda-debug"}
SHADERS="$REPO/src/Gui/Renderer/bgfx/shaders"
SRC="$REPO/src/Gui/Renderer/bgfx/assets/shaders"
DST="$BUILD/share/Renderer/bgfx/assets/shaders"

sh "$SHADERS/compile.sh" "$BUILD/src/3rdParty/bgfx/shaderc"

if [ -d "$DST" ]; then
    for api in glsl spirv essl; do
        cp -f "$SRC/$api/"*.bin "$DST/$api/" 2>/dev/null || true
    done
    echo "refreshed desktop shader bins in $DST"
else
    echo "note: $DST not found (no build-tree copy to refresh)"
fi
echo "for the WASM viewer: source ~/works/sw/emsdk/emsdk_env.sh && cmake --build build/wasm"
