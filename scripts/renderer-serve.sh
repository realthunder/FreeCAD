#!/bin/bash
# Headless (Xvfb, software GL) FreeCAD serving a scene over
# FC_BGFX_SERVE_SCENE for the WASM viewer to stream. For agents/CI or when
# no display is available; rendering is llvmpipe/swiftshader, not the GPU
# (for real-GPU desktop rendering use renderer-desktop.sh instead).
#
# The GUI process group is SIGSTKFLT-killed if launched in the foreground of
# a sandboxed shell, so this detaches and returns immediately; poll the port
# afterwards:  until ss -tln | grep -q :PORT; do sleep 2; done
#
# Usage:  scripts/renderer-serve.sh [scene.py] [serve_port]
#   defaults: scripts/demo-water.py  8077
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
SCENE=${1:-"$REPO/scripts/demo-water.py"}
PORT=${2:-8077}
LOG=${FC_LOG:-/tmp/fc-renderer-serve.log}

pkill -f '[c]onda-debug/bin/FreeCAD' 2>/dev/null
pkill -f '[X]vfb' 2>/dev/null
rm -f "$HOME/.cache/FreeCAD/Cache/FreeCAD_"*.lock 2>/dev/null
sleep 1

setsid nohup env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb \
  FC_BGFX_SERVE_SCENE="$PORT" \
  xvfb-run -a -s "-screen 0 1280x1024x24" \
  "$REPO/.conda/run.sh" "$REPO/build/conda-debug/bin/FreeCAD" \
  "$SCENE" > "$LOG" 2>&1 </dev/null &
disown
echo "launched headless serve on :$PORT -> $LOG"
echo "wait: until ss -tln | grep -q :$PORT; do sleep 2; done"
