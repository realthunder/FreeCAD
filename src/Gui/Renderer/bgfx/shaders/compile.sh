#!/bin/sh
# Manually compile the FreeCAD bgfx shaders into a hot-reload root.
#
# Usage: sh compile.sh [path-to-shaderc] [out-root]
#
# NORMAL BUILDS DO NOT NEED THIS: the .bin files are build artifacts —
# the desktop build compiles them with the in-tree shaderc into
# <build>/share/Renderer/bgfx/assets/shaders/, and the Emscripten viewer
# build packs its own essl set (see ../../BGFXShaders.cmake). Nothing is
# committed under an assets directory anymore.
#
# This script serves the shader hot-reload loop (docs/RenderDebug.md §3):
# it compiles all three profiles into <out-root>/shaders/{glsl,spirv,essl}
# so a running FreeCAD started with FC_BGFX_SHADER_DIR=<out-root> picks
# the result up on view.reloadShaders() — without touching the build
# tree or waiting for a ninja run. Default out-root is
# <repo>/build/shaders-dev; default shaderc is the conda-debug tree's.

set -e

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../../.." && pwd)
shaderc=${1:-$repo/build/conda-debug/src/3rdParty/bgfx/cmake/bgfx/shaderc}
inc=$repo/src/3rdParty/bgfx/bgfx/src
out=${2:-$repo/build/shaders-dev}/shaders

[ -x "$shaderc" ] || { echo "shaderc not found: $shaderc" >&2; exit 1; }

mkdir -p "$out/glsl" "$out/spirv" "$out/essl"

for f in "$here"/vs_*.sc; do
    name=$(basename "$f" .sc)
    "$shaderc" -f "$f" -o "$out/glsl/$name.bin" --type v --platform linux \
        -p 140 -i "$inc" --varyingdef "$here/varying.def.sc"
    "$shaderc" -f "$f" -o "$out/spirv/$name.bin" --type v --platform linux \
        -p spirv -i "$inc" --varyingdef "$here/varying.def.sc"
    "$shaderc" -f "$f" -o "$out/essl/$name.bin" --type v --platform asm.js \
        -p 300_es -i "$inc" --varyingdef "$here/varying.def.sc"
    echo "compiled $name"
done

for f in "$here"/fs_*.sc; do
    name=$(basename "$f" .sc)
    "$shaderc" -f "$f" -o "$out/glsl/$name.bin" --type f --platform linux \
        -p 140 -i "$inc" --varyingdef "$here/varying.def.sc"
    "$shaderc" -f "$f" -o "$out/spirv/$name.bin" --type f --platform linux \
        -p spirv -i "$inc" --varyingdef "$here/varying.def.sc"
    "$shaderc" -f "$f" -o "$out/essl/$name.bin" --type f --platform asm.js \
        -p 300_es -i "$inc" --varyingdef "$here/varying.def.sc"
    echo "compiled $name"
done

echo "hot-reload root: $(dirname "$out") (FC_BGFX_SHADER_DIR)"
