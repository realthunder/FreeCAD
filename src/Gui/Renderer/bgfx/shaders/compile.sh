#!/bin/sh
# Compile the FreeCAD bgfx shaders into the runtime asset directory.
#
# Usage: sh compile.sh [path-to-shaderc]
#
# Default shaderc is the one built in the conda-debug tree. Output goes to
# ../assets/shaders/{glsl,spirv}/ which is installed/copied as a runtime
# resource (see src/Gui/Renderer/CMakeLists.txt).

set -e

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../../.." && pwd)
shaderc=${1:-$repo/build/conda-debug/src/3rdParty/bgfx/cmake/bgfx/shaderc}
inc=$repo/src/3rdParty/bgfx/bgfx/src
out=$here/../assets/shaders

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
