#!/bin/bash
# Included-file blob verification harness (docs/FileBlobsManager.md §11).
#
# Two legs, both run from isolated FreeCAD instances (private XDG dirs +
# user.cfg, no global pkill — never touches the live desktop session):
#
#   headless  src/Mod/Test/FileBlobs.py via FreeCADCmd -t FileBlobs:
#             content addressing and sharing, refcount lifetime,
#             undo/redo, persistence round-trips including the
#             forward-only-merge ordering regression, archive shape,
#             schema-4 fallback, saveAs/saveCopy/revert, and the
#             export/import path via copyObject.
#   desktop   scripts/file_blob_gui.py under xvfb: the view tier —
#             embedded environment image round-trip, hash-not-base64,
#             view/App blob sharing, view-provider file properties.
#   all       both legs.
#
# Usage:
#   scripts/file-blob-verify.sh headless|desktop|all <outdir> [--timeout N]
#
# A leg passes when its result file ends with DONE and contains no
# FAIL/ABORT/EXCEPTION lines; the exit code reflects all legs run.
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
RUN="$REPO/.conda/run.sh"
BIN="$REPO/build/conda-debug/bin"

cmd=${1:-}
case "$cmd" in headless|desktop|all) ;; *)
    sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 2;;
esac
shift
OUT=${1:?needs an output dir}
shift
TIMEOUT=600
while [ $# -gt 0 ]; do
    case "$1" in
        --timeout) TIMEOUT=$2; shift 2;;
        *) echo "unknown option: $1"; exit 2;;
    esac
done

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
FAILED=0

judge() { # <result-file> <name>
    echo "---- $2 ($1)"
    cat "$1" 2>/dev/null
    if ! grep -q "^DONE$" "$1" 2>/dev/null; then
        echo "== $2 FAILED (no DONE)"; FAILED=1
    elif grep -q "FAIL\|^ABORT\|EXCEPTION" "$1"; then
        echo "== $2 HAD FAILURES"; FAILED=1
    else
        echo "== $2 OK"
    fi
}

run_headless() {
    local sub="$OUT/headless" iso
    iso="$sub/.iso"
    mkdir -p "$iso/cache" "$iso/config"
    echo "headless suite (log: $sub/run.log)"
    env XDG_CACHE_HOME="$iso/cache" XDG_CONFIG_HOME="$iso/config" \
        timeout -k 5 "$TIMEOUT" \
        "$RUN" "$BIN/FreeCADCmd" --user-cfg "$iso/user.cfg" -t FileBlobs \
        > "$sub/run.log" 2>&1
    # unittest reports on stderr; normalise it into the DONE/FAIL contract.
    {
        grep -E "^(FAIL|ERROR):" "$sub/run.log" || true
        if grep -qE "^OK( \(skipped=[0-9]+\))?$" "$sub/run.log"; then
            echo DONE
        fi
    } > "$sub/result.txt"
    judge "$sub/result.txt" "headless"
}

run_desktop() {
    local sub="$OUT/desktop" iso
    iso="$sub/.iso"
    mkdir -p "$iso/cache" "$iso/config"
    rm -f "$iso/cache/FreeCAD/Cache/FreeCAD_"*.lock 2>/dev/null
    echo "desktop suite (log: $sub/run.log)"
    env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb \
        XDG_CACHE_HOME="$iso/cache" XDG_CONFIG_HOME="$iso/config" \
        US_OUT="$sub" US_RESULT="$sub/result.txt" \
        xvfb-run -a -s "-screen 0 1280x1024x24" \
        timeout -k 5 "$TIMEOUT" \
        "$RUN" "$BIN/FreeCAD" --user-cfg "$iso/user.cfg" \
        "$REPO/scripts/file_blob_gui.py" > "$sub/run.log" 2>&1
    judge "$sub/result.txt" "desktop"
}

case "$cmd" in
    headless) run_headless;;
    desktop)  run_desktop;;
    all)      run_headless; run_desktop;;
esac

exit $FAILED
