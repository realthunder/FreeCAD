#!/bin/bash
# GUI test driver: runs one FreeCAD-driven test script under xvfb in an
# isolated configuration, with an external timeout, and judges it by the
# result file the script writes.
#
# Usage:
#   scripts/gui-test.sh <test.py> <outdir> [--timeout N] [--window]
#                       [--gl nvidia|radeon|sw]
#
# The script gets GT_OUT (the output directory) and GT_RESULT (the result
# file) in its environment and is expected to append PASS/FAIL lines and
# end with DONE; anything else -- no DONE, a FAIL line, an ABORT line --
# fails the test. The exit status of the process is reported but is not
# the verdict: the work the test does is what it writes, and a process
# that died after DONE is reported as that, on its own line.
#
# Before the test's script FreeCAD is handed scripts/gui-test-profile.py,
# which states what the tests start from where a default would otherwise
# decide it -- no multisampling, which the pictures they read were written
# against.
#
# Never the live desktop session: private XDG dirs + user.cfg, xvfb with
# WAYLAND_DISPLAY unset (WSLg's survives into xvfb-run's child otherwise
# and the window lands on the desktop), and `timeout` around the whole
# process group so a modal dialog or a hung teardown cannot leak a
# FreeCAD + Xvfb pair (timeout signals its process group, which is
# xvfb-run, Xvfb and FreeCAD together).
#
# --window is the exception, and only ever on request: the same isolated
# configuration and the same timeout, but in a window on the desktop's X
# server (GT_DISPLAY, :0 by default) and no Xvfb. For a measurement that
# has to be seen, or checked against what Xvfb says
# (docs/DocumentLoad.md sec 18.14). No ctest entry uses it.
#
# --gl picks the GL driver, under Xvfb or in a window: `sw` is what a
# plain run gets (llvmpipe), `radeon` and `nvidia` are Mesa's D3D12
# driver on WSL, on the default adapter and on the one named NVIDIA. A
# measurement names its display; the script should read GL_RENDERER back.
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
RUN="$REPO/.conda/run.sh"
# The standard build, the one every test run uses (CLAUDE.md,
# docs/DevEnvironment.md); FC_BUILD repoints at another tree.
BUILD=${FC_BUILD:-"$REPO/build/conda-relwithdebinfo-801"}
FCBIN="$BUILD/bin/FreeCAD"
[ -x "$FCBIN" ] || {
    echo "no FreeCAD binary at $FCBIN (set FC_BUILD to another build tree)"
    exit 2
}

SCRIPT=${1:?usage: gui-test.sh <test.py> <outdir> [--timeout N]}
OUT=${2:?usage: gui-test.sh <test.py> <outdir> [--timeout N]}
shift 2
TIMEOUT=300
WINDOW=0
GL=()
while [ $# -gt 0 ]; do
    case "$1" in
        --timeout) TIMEOUT=$2; shift 2;;
        --window) WINDOW=1; shift;;
        --gl)
            case "$2" in
                sw) GL=();;
                radeon|nvidia)
                    GL=(LIBGL_ALWAYS_SOFTWARE=0 GALLIUM_DRIVER=d3d12
                        MESA_LOADER_DRIVER_OVERRIDE=d3d12
                        __GLX_VENDOR_LIBRARY_NAME=mesa)
                    [ "$2" = nvidia ] && GL+=(MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA);;
                *) echo "unknown GL driver: $2 (sw, radeon, nvidia)"; exit 2;;
            esac
            shift 2;;
        *) echo "unknown option: $1"; exit 2;;
    esac
done
[ -f "$SCRIPT" ] || { echo "no test script at $SCRIPT"; exit 2; }
SCRIPT=$(cd "$(dirname "$SCRIPT")" && pwd)/$(basename "$SCRIPT")

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
ISO="$OUT/.iso"
mkdir -p "$ISO/cache" "$ISO/config"
rm -f "$ISO/cache/FreeCAD/Cache/FreeCAD_"*.lock 2>/dev/null
RESULT="$OUT/result.txt"
: > "$RESULT"
LOG="$OUT/run.log"

echo "GUI test $(basename "$SCRIPT") (log: $LOG)"
if [ "$WINDOW" = 1 ]; then
    env -u WAYLAND_DISPLAY DISPLAY="${GT_DISPLAY:-:0}" "${GL[@]}" \
        XDG_CACHE_HOME="$ISO/cache" XDG_CONFIG_HOME="$ISO/config" \
        GT_OUT="$OUT" GT_RESULT="$RESULT" \
        QT_QPA_PLATFORM=xcb \
        timeout -k 15 "$TIMEOUT" \
        "$RUN" "$FCBIN" --user-cfg "$ISO/user.cfg" \
        "$REPO/scripts/gui-test-profile.py" "$SCRIPT" \
        > "$LOG" 2>&1 </dev/null
else
    env -u WAYLAND_DISPLAY "${GL[@]}" \
        XDG_CACHE_HOME="$ISO/cache" XDG_CONFIG_HOME="$ISO/config" \
        GT_OUT="$OUT" GT_RESULT="$RESULT" \
        QT_QPA_PLATFORM=xcb \
        timeout -k 15 "$TIMEOUT" \
        xvfb-run -a -s "-screen 0 1280x1024x24" \
        "$RUN" "$FCBIN" --user-cfg "$ISO/user.cfg" \
        "$REPO/scripts/gui-test-profile.py" "$SCRIPT" \
        > "$LOG" 2>&1 </dev/null
fi
status=$?

echo "---- $RESULT"
cat "$RESULT"
echo "---- exit status $status"

if ! grep -q "^DONE$" "$RESULT"; then
    if [ "$status" = 124 ] || [ "$status" = 137 ]; then
        echo "GUI TEST FAILED: no DONE within ${TIMEOUT}s"
    else
        echo "GUI TEST FAILED: no DONE (exit status $status)"
    fi
    echo "---- tail of $LOG"
    tail -n 40 "$LOG"
    exit 1
fi
if grep -q "^FAIL\|^ABORT" "$RESULT"; then
    echo "GUI TEST HAD FAILURES"
    exit 1
fi
if [ "$status" != 0 ]; then
    echo "GUI TEST FAILED: the process wrote DONE but exited with status $status"
    echo "---- tail of $LOG"
    tail -n 40 "$LOG"
    exit 1
fi
echo "GUI TEST OK"
