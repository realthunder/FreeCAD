#!/bin/bash
# GUI test driver: runs one FreeCAD-driven test script under xvfb in an
# isolated configuration, with an external timeout, and judges it by the
# result file the script writes.
#
# Usage:
#   scripts/gui-test.sh <test.py> <outdir> [--timeout N]
#
# The script gets GT_OUT (the output directory) and GT_RESULT (the result
# file) in its environment and is expected to append PASS/FAIL lines and
# end with DONE; anything else -- no DONE, a FAIL line, an ABORT line --
# fails the test. The exit status of the process is reported but is not
# the verdict: the work the test does is what it writes, and a process
# that died after DONE is reported as that, on its own line.
#
# Never the live desktop session: private XDG dirs + user.cfg, xvfb with
# WAYLAND_DISPLAY unset (WSLg's survives into xvfb-run's child otherwise
# and the window lands on the desktop), and `timeout` around the whole
# process group so a modal dialog or a hung teardown cannot leak a
# FreeCAD + Xvfb pair (timeout signals its process group, which is
# xvfb-run, Xvfb and FreeCAD together).
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
while [ $# -gt 0 ]; do
    case "$1" in
        --timeout) TIMEOUT=$2; shift 2;;
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
env -u WAYLAND_DISPLAY \
    XDG_CACHE_HOME="$ISO/cache" XDG_CONFIG_HOME="$ISO/config" \
    GT_OUT="$OUT" GT_RESULT="$RESULT" \
    QT_QPA_PLATFORM=xcb \
    timeout -k 15 "$TIMEOUT" \
    xvfb-run -a -s "-screen 0 1280x1024x24" \
    "$RUN" "$FCBIN" --user-cfg "$ISO/user.cfg" "$SCRIPT" \
    > "$LOG" 2>&1 </dev/null
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
