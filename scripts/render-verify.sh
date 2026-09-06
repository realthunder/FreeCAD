#!/bin/bash
# Render-verification harness driver (docs/RenderDebug.md §5, phase 3).
#
# Captures a staged set of (camera × RenderDebug_ViewMode) frames from an
# isolated FreeCAD instance via render_verify.py, and/or diffs two capture
# directories stage-by-stage via render_diff.py. Never touches the live
# desktop session or a serving backend: private XDG dirs + user.cfg, own
# scene/http ports, no global pkill.
#
# Usage:
#   scripts/render-verify.sh capture <outdir> [options]
#   scripts/render-verify.sh diff <golden-dir> <current-dir> [render_diff.py args...]
#
# capture options:
#   --scene <s.py>   scene script (default scripts/demo-lights.py)
#   --golden <dir>   restage cameras+properties from this capture set's
#                    sidecars instead of the named-view manifest, then
#                    diff <dir> against the new captures when done
#   --cams a,b,c     named views (default iso,front,top; see render_verify.py)
#   --modes 0,1,..   debug view mode list (default 0,1,2,3,4; 0 = beauty)
#   --gpu            real-GPU leg: WSLg wayland + Mesa d3d12 (OPENS A WINDOW
#                    ON THE DESKTOP; default is headless xvfb = llvmpipe,
#                    which verifies logic but not device-GPU precision)
#   --settle N       extra frames to run before capturing (default 0).
#                    The harness first waits for the backend's own
#                    "complete frame" signal (view.waitFrameComplete:
#                    shaders compiled, deferred shapes arrived, frozen
#                    warm-up reached), so this is only for content that
#                    signal does not cover
#   --cycles         also path trace each staged camera with Cycles
#                    (CPU by default -- see --cycles-device). Needs a
#                    BUILD_CYCLES build; adds <prefix>--cycles--mode0.png
#   --cycles-samples N   samples per pixel for that leg (default 32)
#   --cycles-device D    Cycles device (default CPU; a golden blessed on
#                    one device does not compare against another)
#   --cycles-size WxH    size of the traced frame (default 320x240)
#   --viewer         also capture the browser leg: serves the scene
#                    (FC_BGFX_SERVE_SCENE) + the built WASM viewer, holds a
#                    headless-Chromium page (swiftshader) on it and drives
#                    the dumpFrame protocol. Needs build/wasm + puppeteer
#                    (PUPPETEER_PATH). A real device on the same URL gets
#                    captured too (extra -<n> files, real-GPU evidence).
#   --port N / --http N   scene / viewer-http ports (default 8123 / 8124)
#   --cam spec       viewer &cam= pin (default 0.6,0.3,70,0,0,5,0,0)
#   --timeout N      seconds to wait for the capture run (default 600)
#
# Typical golden workflow:
#   scripts/render-verify.sh capture /path/goldens              # bless once
#   ...hack on the renderer...
#   scripts/render-verify.sh capture /tmp/rv --golden /path/goldens
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
RUN="$REPO/.conda/run.sh"
# The standard build and the one every test run uses (CLAUDE.md,
# docs/DevEnvironment.md); FC_BUILD repoints the capture runs at another
# one. It used to default to build/conda-debug-occt801, which is the
# debugger tree and not what anything is measured on.
BUILD=${FC_BUILD:-"$REPO/build/conda-relwithdebinfo-801"}
FCBIN="$BUILD/bin/FreeCAD"
[ -x "$FCBIN" ] || {
    echo "no FreeCAD binary at $FCBIN (set FC_BUILD to another build tree)"
    exit 2
}

cmd=${1:-}
shift || true

if [ "$cmd" = diff ]; then
    exec "$RUN" python "$REPO/scripts/render_diff.py" "$@"
fi
if [ "$cmd" != capture ]; then
    sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi

OUT=${1:?capture needs an output dir}
shift
SCENE="$REPO/scripts/demo-lights.py"
GOLDEN= CAMS= MODES= GPU=0 VIEWER=0 PORT=8123 HTTP=8124 TIMEOUT=600
CYCLES=0 CYCLES_SAMPLES= CYCLES_DEVICE= CYCLES_SIZE= SETTLE=
CAM="0.6,0.3,70,0,0,5,0,0"
while [ $# -gt 0 ]; do
    case "$1" in
        --scene)   SCENE=$2; shift 2;;
        --golden)  GOLDEN=$(cd "$2" && pwd); shift 2;;
        --cams)    CAMS=$2; shift 2;;
        --modes)   MODES=$2; shift 2;;
        --gpu)     GPU=1; shift;;
        --viewer)  VIEWER=1; shift;;
        --settle)  SETTLE=$2; shift 2;;
        --cycles)  CYCLES=1; shift;;
        --cycles-samples) CYCLES_SAMPLES=$2; shift 2;;
        --cycles-device)  CYCLES_DEVICE=$2; shift 2;;
        --cycles-size)    CYCLES_SIZE=$2; shift 2;;
        --port)    PORT=$2; shift 2;;
        --http)    HTTP=$2; shift 2;;
        --cam)     CAM=$2; shift 2;;
        --timeout) TIMEOUT=$2; shift 2;;
        *) echo "unknown option: $1"; exit 2;;
    esac
done

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
ISO="$OUT/.iso"
mkdir -p "$ISO/cache" "$ISO/config"
# RV_CACHE shares one cache directory across runs instead of giving each
# its own. It matters more than it sounds: the generated MaterialX
# shaders are compiled by shaderc into $XDG_CACHE_HOME/FreeCAD/
# BGFXUserShaders, and a private cache means every run recompiles the
# whole material set from cold while the capture clock is running.
CACHE="$ISO/cache"
[ -n "${RV_CACHE:-}" ] && { mkdir -p "$RV_CACHE"; CACHE=$(cd "$RV_CACHE" && pwd); }
rm -f "$ISO/cache/FreeCAD/Cache/FreeCAD_"*.lock 2>/dev/null
RESULT="$OUT/result.txt"
: > "$RESULT"
LOG="$OUT/run.log"

# Scene id for capture filenames: demo-lights.py -> lights.
NAME=$(basename "$SCENE" .py); NAME=${NAME#demo-}

FC_PIDS=()
cleanup() {
    for pid in "${FC_PIDS[@]:-}"; do
        [ -n "$pid" ] && kill -- -"$pid" 2>/dev/null
    done
}
trap cleanup EXIT

COMMON_ENV=(
    XDG_CACHE_HOME="$CACHE" XDG_CONFIG_HOME="$ISO/config"
    RV_OUT="$OUT" RV_RESULT="$RESULT" RV_SCENE_NAME="$NAME"
    ${CAMS:+RV_CAMERAS="$CAMS"} ${MODES:+RV_MODES="$MODES"}
    ${GOLDEN:+RV_GOLDEN="$GOLDEN"}
    ${SETTLE:+RV_SETTLE="$SETTLE"}
)
[ "$VIEWER" = 1 ] && COMMON_ENV+=(RV_VIEWER=1 FC_BGFX_SERVE_SCENE=$PORT)
if [ "$CYCLES" = 1 ]; then
    COMMON_ENV+=(RV_CYCLES=1
        ${CYCLES_SAMPLES:+RV_CYCLES_SAMPLES="$CYCLES_SAMPLES"}
        ${CYCLES_DEVICE:+RV_CYCLES_DEVICE="$CYCLES_DEVICE"}
        ${CYCLES_SIZE:+RV_CYCLES_SIZE="$CYCLES_SIZE"})
fi

if [ "$GPU" = 1 ]; then
    echo "real-GPU leg: WSLg wayland + d3d12 (a FreeCAD window will appear)"
    setsid nohup env "${COMMON_ENV[@]}" \
        QT_QPA_PLATFORM=wayland WAYLAND_DISPLAY=wayland-0 \
        XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
        DISPLAY="${DISPLAY:-:0}" \
        LIBGL_ALWAYS_SOFTWARE=0 GALLIUM_DRIVER=d3d12 \
        MESA_LOADER_DRIVER_OVERRIDE=d3d12 __GLX_VENDOR_LIBRARY_NAME=mesa \
        MESA_D3D12_DEFAULT_ADAPTER_NAME="${FC_ADAPTER:-}" \
        "$RUN" "$FCBIN" \
        --user-cfg "$ISO/user.cfg" \
        "$SCENE" "$REPO/scripts/render_verify.py" \
        > "$LOG" 2>&1 </dev/null &
else
    setsid nohup env -u WAYLAND_DISPLAY "${COMMON_ENV[@]}" \
        QT_QPA_PLATFORM=xcb \
        xvfb-run -a -s "-screen 0 1280x1024x24" \
        "$RUN" "$FCBIN" \
        --user-cfg "$ISO/user.cfg" \
        "$SCENE" "$REPO/scripts/render_verify.py" \
        > "$LOG" 2>&1 </dev/null &
fi
FC_PIDS+=($!)
echo "FreeCAD capture run launched (log: $LOG)"

if [ "$VIEWER" = 1 ]; then
    [ -f "$REPO/build/wasm/fcviewer.html" ] || {
        echo "--viewer needs build/wasm/fcviewer.html"; exit 2; }
    # no-store like wasm-viewer.sh: never serve a stale cached bundle.
    setsid nohup python3 - "$HTTP" "$REPO/build/wasm" > "$OUT/http.log" 2>&1 <<'EOF' &
import http.server, os, sys
os.chdir(sys.argv[2])
class H(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()
http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), H).serve_forever()
EOF
    FC_PIDS+=($!)
    # Hold the page once the scene port answers.
    for _ in $(seq 60); do
        (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null && { exec 3>&-; break; }
        sleep 1
    done
    URL="http://127.0.0.1:$HTTP/fcviewer.html?scene=http://127.0.0.1:$PORT&cam=$CAM"
    setsid nohup node "$REPO/scripts/wasm-hold.js" "$URL" $((TIMEOUT * 1000)) \
        > "$OUT/hold.log" 2>&1 </dev/null &
    FC_PIDS+=($!)
    echo "viewer leg: $URL (hold log: $OUT/hold.log)"
fi

for _ in $(seq "$TIMEOUT"); do
    grep -q "^DONE$\|^ABORT" "$RESULT" 2>/dev/null && break
    sleep 1
done

echo "---- $RESULT"
cat "$RESULT"
cleanup; trap - EXIT

if ! grep -q "^DONE$" "$RESULT"; then
    echo "CAPTURE FAILED (no DONE within ${TIMEOUT}s)"; exit 1
fi
grep -q "^FAIL\|^ABORT" "$RESULT" && { echo "CAPTURE HAD FAILURES"; exit 1; }

if [ -n "$GOLDEN" ]; then
    echo "---- diff vs golden"
    "$RUN" python "$REPO/scripts/render_diff.py" "$GOLDEN" "$OUT" \
        --diffs "$OUT/diffs"
    exit $?
fi
echo "CAPTURE OK: $OUT"
