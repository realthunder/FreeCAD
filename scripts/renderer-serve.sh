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
# The MCP debug console (freecad.mcp_console, streamable-HTTP on
# 127.0.0.1:8765) starts alongside the serve so an agent can drive the
# live process (run_python — e.g. saveRenderDump(source='viewer') to
# capture connected browsers). FC_MCP_PORT overrides the port,
# FC_MCP_PORT=0 disables it.
#
# Relaunching a port replaces ONLY the backend holding that port (its
# whole setsid process group, Xvfb included). Backends on other ports
# are never touched, so several can serve side by side. Each port logs
# to its own file (/tmp/fc-serve-<port>.log, FC_LOG overrides); the
# previous log is kept as <log>.1, and a wrapper trailer line records
# how FreeCAD exited — a log that just stops mid-line means SIGKILL.
#
# Usage:  scripts/renderer-serve.sh [scene.py] [serve_port]
#   defaults: scripts/demo-water.py  8077
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
SCENE=${1:-"$REPO/scripts/demo-water.py"}
PORT=${2:-8077}
LOG=${FC_LOG:-/tmp/fc-serve-$PORT.log}
MCP_PORT=${FC_MCP_PORT:-8765}
EXTRA=()
[ "$MCP_PORT" != 0 ] && EXTRA=("$REPO/scripts/mcp-console.py")

# Replace only the backend that owns this port: kill its setsid process
# group (FreeCAD + xvfb-run + its Xvfb), never other ports' backends.
oldpid=$(ss -tlnpH "sport = :$PORT" 2>/dev/null | grep -oP 'pid=\K[0-9]+' | head -1)
if [ -n "${oldpid:-}" ]; then
    pgid=$(ps -o pgid= -p "$oldpid" 2>/dev/null | tr -d ' ')
    echo "replacing serve on :$PORT (pid $oldpid, pgid ${pgid:-?})"
    [ -n "$pgid" ] && kill -TERM -"$pgid" 2>/dev/null || kill -TERM "$oldpid" 2>/dev/null
    for _ in $(seq 20); do
        ss -tlnH "sport = :$PORT" 2>/dev/null | grep -q . || break
        sleep 0.5
    done
    if ss -tlnH "sport = :$PORT" 2>/dev/null | grep -q .; then
        echo "port :$PORT still held, escalating to SIGKILL"
        [ -n "$pgid" ] && kill -KILL -"$pgid" 2>/dev/null || kill -KILL "$oldpid" 2>/dev/null
        sleep 1
    fi
fi
rm -f "$HOME/.cache/FreeCAD/Cache/FreeCAD_"*.lock 2>/dev/null

[ -f "$LOG" ] && mv -f "$LOG" "$LOG.1"

setsid nohup env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb \
  FC_BGFX_SERVE_SCENE="$PORT" FC_MCP_PORT="$MCP_PORT" \
  FC_BGFX_VIEWER_BUILD="${FC_BGFX_VIEWER_BUILD:-$REPO/build/wasm}" \
  bash -c 'xvfb-run -a -s "-screen 0 1280x1024x24" "$@"; s=$?;
           echo "serve wrapper: FreeCAD (:$FC_BGFX_SERVE_SCENE) exited status $s at $(date -Is)"' \
  -- "$REPO/.conda/run.sh" "$REPO/build/conda-debug/bin/FreeCAD" \
  "$SCENE" ${EXTRA[@]+"${EXTRA[@]}"} > "$LOG" 2>&1 </dev/null &
disown
echo "launched headless serve on :$PORT -> $LOG"
[ "$MCP_PORT" != 0 ] && echo "mcp console (once up): http://127.0.0.1:$MCP_PORT/mcp"
echo "wait: until ss -tln | grep -q :$PORT; do sleep 2; done"
