#!/bin/bash
# Serve the built WASM viewer over HTTP and print the URLs to open it against
# a scene backend (see renderer-serve.sh / renderer-desktop.sh <port>).
#
# Usage:  scripts/wasm-viewer.sh [http_port] [scene_port]
#   defaults: 8000  8077
# Rebuild the viewer first after any renderer/shader change:
#   source ~/works/sw/emsdk/emsdk_env.sh && cmake --build build/wasm
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
HTTP=${1:-8000}
SCENE=${2:-8077}
WASM="$REPO/build/wasm"

[ -f "$WASM/fcviewer.html" ] || { echo "no $WASM/fcviewer.html - build the WASM viewer first"; exit 1; }

echo "local:  http://127.0.0.1:$HTTP/fcviewer.html?scene=http://127.0.0.1:$SCENE"
echo "  add &hud&debugpick for the HUD/pick log, &cam=yaw,pitch,dist,cx,cy,cz,panX,panY to reproduce a view"
echo "(for remote viewing, reverse-tunnel $HTTP and $SCENE to a public host)"
cd "$WASM" && exec python3 -m http.server "$HTTP" --bind 127.0.0.1
