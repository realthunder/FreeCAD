#!/bin/bash
# Recompile the bgfx shaders the running/next desktop build loads.
#
# The .bin files are BUILD ARTIFACTS (nothing is committed): the desktop
# build compiles shader source with the in-tree shaderc straight into
# <build>/share/Renderer/bgfx/assets/shaders/ (see
# src/Gui/Renderer/BGFXShaders.cmake). This script is just the shortcut
# for that one target — ninja recompiles only the shaders whose source
# changed. A running FreeCAD picks the result up on view.reloadShaders().
#
# The WASM viewer compiles/packs its own essl set at link time — rebuild
# it separately: source ~/works/sw/emsdk/emsdk_env.sh && cmake --build build/wasm
#
# For an out-of-build hot-reload root (FC_BGFX_SHADER_DIR), use
# src/Gui/Renderer/bgfx/shaders/compile.sh instead.
#
# Usage:  scripts/compile-shaders.sh [build_dir]
#   build_dir  default: build/conda-debug
set -eu
REPO=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-"$REPO/build/conda-debug"}

"$REPO/.conda/run.sh" ninja -C "$BUILD" Renderer_assets
echo "for the WASM viewer: source ~/works/sw/emsdk/emsdk_env.sh && cmake --build build/wasm"
