#!/bin/bash
# User-shader verification harness (docs/RenderDebug.md §6.4/§6.5).
#
# Runs the user-loadable-shader suites from isolated FreeCAD instances
# (private XDG dirs + user.cfg, own ports, no global pkill — never
# touches the live desktop session or a serving backend):
#
#   desktop  xvfb GUI suites over the document-object model:
#            user_shader_params.py (§6.4 Param_* property binding,
#            Appearance overrides, stale-uniform restore),
#            user_shader_post.py (§6.5 scene-level post activation,
#            TreeRank precedence, deactivation paths) and
#            user_shader_instancing.py (§6.5 LinkGroup Appearance:
#            Scope=Instance chains vs Scope=Object merge-down attachment)
#            user_shader_element.py (§6.5 Scope=Element face-level
#            overrides, element-over-whole coexistence) and
#            user_shader_lighting.py (fc_user_lighting.sh helper:
#            stock-identity reproduction + lit custom albedo) and
#            user_shader_motion.py (user vertex stage: identity,
#            Param-driven displacement, u_fcTime animation + freeze) and
#            user_shader_particles.py (Emitter seed quads + billboard
#            VS + additive state = stateless GPU particles) and
#            user_shader_particles_state.py (§5.8 stateful tier:
#            ping-pong state textures, warm-up determinism, stateless
#            fallback) and
#            user_shader_water.py ("water" stage: activation-by-binding,
#            fc_user_water.sh stock identity, tint, fallback) and
#            user_shader_volume.py ("volume" stage: medium-function
#            splice, identity fire, green ramp, fallback) and
#            user_shader_effects.py (bundled effect packages +
#            freecad.rendereffects factory + Enabled toggle).
#   viewer   browser-tier suites: backend serving demo-lights
#            (FC_BGFX_SERVE_SCENE) + no-store http on build/wasm + a
#            headless-Chromium holder (scripts/wasm-hold.js), driven
#            through saveRenderDump(source='viewer').
#            user_shader_viewer.py (scene-graph route: post + material
#            SoShaderProgram nodes), user_shader_viewer_appearance.py
#            (document-object route: empty-target Appearance) and
#            user_shader_volume_viewer.py (v24 volume-splice transport:
#            green medium visible in the browser).
#            Needs build/wasm and puppeteer (PUPPETEER_PATH).
#   all      both legs.
#
# Usage:
#   scripts/user-shader-verify.sh desktop|viewer|all <outdir> [options]
#
# options:
#   --port N / --http N   viewer leg scene / http ports (default 8177/8178)
#   --timeout N           per-suite timeout seconds (default 900)
#
# A suite passes when its result file ends with DONE and contains no
# FAIL/ABORT lines; the exit code reflects all suites run.
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
RUN="$REPO/.conda/run.sh"
# The OCCT 8.0.1 conda tree is what the fork builds against
# (docs/DevEnvironment.md); FC_BUILD repoints the suites at another one.
BUILD=${FC_BUILD:-"$REPO/build/conda-debug-occt801"}
FCBIN="$BUILD/bin/FreeCAD"
[ -x "$FCBIN" ] || {
    echo "no FreeCAD binary at $FCBIN (set FC_BUILD to another build tree)"
    exit 2
}

cmd=${1:-}
case "$cmd" in desktop|viewer|all) ;; *)
    sed -n '2,29p' "$0" | sed 's/^# \{0,1\}//'; exit 2;;
esac
shift
OUT=${1:?needs an output dir}
shift
PORT=8177 HTTP=8178 TIMEOUT=900
while [ $# -gt 0 ]; do
    case "$1" in
        --port)    PORT=$2; shift 2;;
        --http)    HTTP=$2; shift 2;;
        --timeout) TIMEOUT=$2; shift 2;;
        *) echo "unknown option: $1"; exit 2;;
    esac
done

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
FAILED=0

PIDS=()
cleanup() {
    for pid in "${PIDS[@]:-}"; do
        [ -n "$pid" ] && kill -- -"$pid" 2>/dev/null
    done
    PIDS=()
}
trap cleanup EXIT

judge() { # <result-file> <name>
    echo "---- $2 ($1)"
    cat "$1" 2>/dev/null
    if ! grep -q "^DONE$" "$1" 2>/dev/null; then
        echo "== $2 FAILED (no DONE)"; FAILED=1
    elif grep -qE "FAIL|^ABORT|EXCEPTION" "$1"; then
        echo "== $2 HAD FAILURES"; FAILED=1
    else
        echo "== $2 OK"
    fi
}

run_desktop() { # <driver.py> <name>
    local sub="$OUT/$2" iso
    iso="$sub/.iso"
    mkdir -p "$iso/cache" "$iso/config"
    rm -f "$iso/cache/FreeCAD/Cache/FreeCAD_"*.lock 2>/dev/null
    echo "desktop suite $2 (log: $sub/run.log)"
    env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb \
        XDG_CACHE_HOME="$iso/cache" XDG_CONFIG_HOME="$iso/config" \
        US_OUT="$sub" US_RESULT="$sub/result.txt" \
        xvfb-run -a -s "-screen 0 1920x1080x24" \
        timeout -k 5 "$TIMEOUT" \
        "$RUN" "$FCBIN" \
        --user-cfg "$iso/user.cfg" \
        "$REPO/scripts/$1" > "$sub/run.log" 2>&1
    judge "$sub/result.txt" "$2"
}

run_viewer() { # <driver.py> <name>
    local sub="$OUT/$2" iso
    iso="$sub/.iso"
    mkdir -p "$sub" "$iso/cache" "$iso/config"
    rm -f "$iso/cache/FreeCAD/Cache/FreeCAD_"*.lock 2>/dev/null
    : > "$sub/result.txt"
    echo "viewer suite $2 (log: $sub/run.log)"

    setsid nohup env -u WAYLAND_DISPLAY \
        XDG_CACHE_HOME="$iso/cache" XDG_CONFIG_HOME="$iso/config" \
        US_OUT="$sub" US_RESULT="$sub/result.txt" \
        FC_BGFX_SERVE_SCENE=$PORT \
        QT_QPA_PLATFORM=xcb \
        xvfb-run -a -s "-screen 0 1280x1024x24" \
        "$RUN" "$FCBIN" \
        --user-cfg "$iso/user.cfg" \
        "$REPO/scripts/demo-lights.py" "$REPO/scripts/$1" \
        > "$sub/run.log" 2>&1 </dev/null &
    PIDS+=($!)

    # no-store like wasm-viewer.sh: never serve a stale cached bundle.
    setsid nohup python3 - "$HTTP" "$REPO/build/wasm" > "$sub/http.log" 2>&1 <<'EOF' &
import http.server, os, sys
os.chdir(sys.argv[2])
class H(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()
http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), H).serve_forever()
EOF
    PIDS+=($!)

    for _ in $(seq 90); do
        (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null && { exec 3>&-; break; }
        sleep 1
    done
    local url="http://127.0.0.1:$HTTP/fcviewer.html?scene=http://127.0.0.1:$PORT&cam=0.6,0.3,70,0,0,5,0,0"
    setsid nohup env \
        LD_LIBRARY_PATH="$REPO/.conda/freecad/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        node "$REPO/scripts/wasm-hold.js" "$url" $((TIMEOUT * 1000)) \
        > "$sub/hold.log" 2>&1 </dev/null &
    PIDS+=($!)
    echo "viewer holder on $url"

    for _ in $(seq "$TIMEOUT"); do
        # -E: see render-verify.sh -- a mid-pattern `$` is a literal in BRE,
        # so this poll never broke early wherever grep is not GNU.
        grep -qE "^DONE$|^ABORT" "$sub/result.txt" 2>/dev/null && break
        sleep 1
    done
    cleanup
    judge "$sub/result.txt" "$2"
}

if [ "$cmd" = desktop ] || [ "$cmd" = all ]; then
    run_desktop user_shader_params.py params
    run_desktop user_shader_post.py post
    run_desktop user_shader_instancing.py instancing
    run_desktop user_shader_element.py element
    run_desktop user_shader_lighting.py lighting
    run_desktop user_shader_motion.py motion
    run_desktop user_shader_particles.py particles
    run_desktop user_shader_particles_state.py particles-state
    run_desktop user_shader_water.py water
    run_desktop user_shader_volume.py volume
    run_desktop user_shader_effects.py effects
fi

if [ "$cmd" = viewer ] || [ "$cmd" = all ]; then
    [ -f "$REPO/build/wasm/fcviewer.html" ] || {
        echo "viewer leg needs build/wasm/fcviewer.html"; exit 2; }
    [ -n "${PUPPETEER_PATH:-}" ] || {
        echo "viewer leg needs PUPPETEER_PATH (a node_modules/puppeteer)"
        exit 2; }
    run_viewer user_shader_viewer.py viewer
    run_viewer user_shader_viewer_appearance.py viewer-appearance
    run_viewer user_shader_volume_viewer.py viewer-volume
fi

trap - EXIT
[ "$FAILED" = 0 ] && echo "ALL SUITES OK" || echo "SUITES FAILED"
exit $FAILED
