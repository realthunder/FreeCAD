#!/bin/bash
# Launch the desktop FreeCAD GUI on the WSLg **real GPU** (Mesa d3d12 over
# /dev/dxg) with the bgfx render-cache backend, optionally serving the scene
# to the WASM viewer.
#
# The conda env ships no DRI drivers, so a plain xcb launch falls back to
# llvmpipe (software = slow). Launching on WSLg Wayland with the d3d12
# gallium driver uses the system driver and the real GPU instead. Confirm
# with:  grep 'BGFX     Renderer:' <log>
#
# Usage:  scripts/renderer-desktop.sh [scene.py] [serve_port]
#   scene.py    Python script run at startup (default: scripts/demo-water.py)
#   serve_port  if set, also stream the scene over FC_BGFX_SERVE_SCENE
#
# Env overrides:
#   FC_PLATFORM  qpa platform (default wayland; set xcb to force software)
#   FC_GALLIUM   gallium driver (default d3d12)
#   FC_ADAPTER   MESA_D3D12_DEFAULT_ADAPTER_NAME (e.g. NVIDIA to force dGPU)
#   FC_USER_CFG  --user-cfg file (default: a throwaway copy of the real cfg)
#   FC_LOG       log file (default: /tmp/fc-renderer-desktop.log)
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
SCENE=${1:-"$REPO/scripts/demo-water.py"}
PORT=${2:-}
LOG=${FC_LOG:-/tmp/fc-renderer-desktop.log}

# Throwaway user config so bgfx/effect prefs don't pollute the real one.
if [ -z "${FC_USER_CFG:-}" ]; then
    FC_USER_CFG=/tmp/fc-throwaway-user.cfg
    [ -f "$HOME/.config/FreeCAD/user.cfg" ] && cp -f "$HOME/.config/FreeCAD/user.cfg" "$FC_USER_CFG" 2>/dev/null
fi

# Replace only our previous instance, never serve backends on other
# ports: prefer the process holding our serve port (kill its whole
# setsid group), else match the desktop launch's --user-cfg signature.
oldpid=
[ -n "$PORT" ] && oldpid=$(ss -tlnpH "sport = :$PORT" 2>/dev/null | grep -oP 'pid=\K[0-9]+' | head -1)
[ -z "$oldpid" ] && oldpid=$(pgrep -f "[c]onda-debug/bin/FreeCAD.*--user-cfg $FC_USER_CFG" | head -1)
if [ -n "$oldpid" ]; then
    pgid=$(ps -o pgid= -p "$oldpid" 2>/dev/null | tr -d ' ')
    echo "replacing previous desktop instance (pid $oldpid, pgid ${pgid:-?})"
    [ -n "$pgid" ] && kill -TERM -"$pgid" 2>/dev/null || kill -TERM "$oldpid" 2>/dev/null
    for _ in $(seq 10); do kill -0 "$oldpid" 2>/dev/null || break; sleep 0.5; done
fi
rm -f "$HOME/.cache/FreeCAD/Cache/FreeCAD_"*.lock 2>/dev/null

[ -f "$LOG" ] && mv -f "$LOG" "$LOG.1"

setsid nohup env \
  QT_QPA_PLATFORM="${FC_PLATFORM:-wayland}" \
  WAYLAND_DISPLAY=wayland-0 \
  XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
  DISPLAY="${DISPLAY:-:0}" \
  LIBGL_ALWAYS_SOFTWARE=0 \
  GALLIUM_DRIVER="${FC_GALLIUM:-d3d12}" \
  MESA_LOADER_DRIVER_OVERRIDE="${FC_GALLIUM:-d3d12}" \
  MESA_D3D12_DEFAULT_ADAPTER_NAME="${FC_ADAPTER:-}" \
  __GLX_VENDOR_LIBRARY_NAME=mesa \
  ${PORT:+FC_BGFX_SERVE_SCENE=$PORT} \
  bash -c '"$@"; s=$?;
           echo "desktop wrapper: FreeCAD exited status $s at $(date -Is)"' \
  -- "$REPO/.conda/run.sh" "$REPO/build/conda-debug/bin/FreeCAD" \
  ${FC_USER_CFG:+--user-cfg "$FC_USER_CFG"} \
  "$SCENE" > "$LOG" 2>&1 </dev/null &
disown
echo "launched desktop FreeCAD (${FC_PLATFORM:-wayland}/${FC_GALLIUM:-d3d12}) -> $LOG"
[ -n "$PORT" ] && echo "serving scene on FC_BGFX_SERVE_SCENE=$PORT"
echo "GPU: grep 'BGFX     Renderer:' $LOG"
