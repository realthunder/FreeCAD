#!/usr/bin/env bash
#
# Drive scripts/composite_cost_probe.py once per composite route and
# reduce the renderer's own report lines into one table.
#
# The route is read from the environment once, at startup, so a leg has
# to be a process. That is the whole reason this driver exists.
#
# Legs:
#   blit           FC_BGFX_READBACK=0  -- BGFXView::blit, GL only
#   readback       FC_BGFX_READBACK=2  -- pipelined readback composite
#   readback-sync  + FC_BGFX_READBACK_SYNC=1, the fully serialized form
#                  docs/DeviceAdoption.md section 2 costed
#
# On a non-GL backend the blit leg draws nothing (blit stands aside), so
# it is skipped unless the type says OpenGL.
#
# Usage: scripts/composite-cost.sh <outdir> [-- <extra FreeCAD args>]
# Env:   COMP_COST_BIN     FreeCAD binary (default: the build tree's)
#        COMP_COST_TYPE    renderer type (default: per platform)
#        COMP_COST_SECS    seconds per leg (default 12)
#        COMP_COST_OBJECTS Part::Box count (default 192)
#        COMP_COST_LEGS    comma-separated subset of the legs above

set -u

OUT=${1:?usage: composite-cost.sh <outdir>}
shift || true
[ "${1:-}" = "--" ] && shift
# Whatever is left is passed through to FreeCAD, once per leg.
EXTRA=("$@")
mkdir -p "$OUT"

here=$(cd "$(dirname "$0")/.." && pwd)
BIN=${COMP_COST_BIN:-$here/build/mac-relwithdebinfo-801/bin/FreeCAD}
if [ ! -x "$BIN" ]; then
    echo "no FreeCAD binary at $BIN (set COMP_COST_BIN)" >&2
    exit 2
fi
TYPE=${COMP_COST_TYPE:-}
if [ -z "$TYPE" ]; then
    case "$(uname -s)" in
        Darwin) TYPE="bgfx - Metal" ;;
        *)      TYPE="bgfx - OpenGL" ;;
    esac
fi

case "$TYPE" in
    *OpenGL*) DEFAULT_LEGS="blit,readback,readback-sync" ;;
    *)        DEFAULT_LEGS="readback,readback-sync" ;;
esac
LEGS=${COMP_COST_LEGS:-$DEFAULT_LEGS}

run_leg() {
    local leg=$1 log="$OUT/$leg.log"
    local -a env_args
    env_args=(FC_SWAP_INTERVAL=0 "COMP_COST_TYPE=$TYPE")
    case "$leg" in
        blit)          env_args+=(FC_BGFX_READBACK=0) ;;
        readback)      env_args+=(FC_BGFX_READBACK=2) ;;
        readback-sync) env_args+=(FC_BGFX_READBACK=2 FC_BGFX_READBACK_SYNC=1) ;;
        *) echo "unknown leg $leg" >&2; return 1 ;;
    esac
    # Metal is opt-in in the backend list; a leg that asked for it and
    # silently got the render cache instead would report the wrong
    # route's cost.
    case "$TYPE" in *Metal*) env_args+=(FC_BGFX_METAL=1) ;; esac
    case "$TYPE" in *Vulkan*) env_args+=(FC_BGFX_VULKAN=1) ;; esac
    echo "== $leg"
    env "${env_args[@]}" "$BIN" --log-file "$log" \
        "$here/scripts/composite_cost_probe.py" ${EXTRA+"${EXTRA[@]}"} \
        >"$OUT/$leg.out" 2>"$OUT/$leg.err"
    echo "   log $log"
}

for leg in ${LEGS//,/ }; do
    run_leg "$leg"
done

echo
echo "== composite cost, ${TYPE}"
for leg in ${LEGS//,/ }; do
    log="$OUT/$leg.log"
    [ -f "$log" ] || continue
    echo "-- $leg"
    # The last line of each kind: the first report of a run includes
    # the startup frames, which are nobody's steady state.
    grep "render readback composite" "$log" | tail -1
    grep "render cpu phases" "$log" | tail -1
    grep "render frame" "$log" | tail -1
done
