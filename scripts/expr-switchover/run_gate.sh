#!/bin/bash
# Drive corpus_regression.py one FILE at a time, each in its own
# FreeCADCmd with a hard timeout, and merge the results.
#
# Why per-file subprocesses: a .FCStd saved by OCCT 7.7.2 gets a forced
# geometry recompute when 8.0.1 opens it, and on a big assembly that can
# run for tens of minutes -- unrelated to expressions, but it stalls a
# single-process sweep with no way to interrupt it.  A per-file timeout
# turns that into one skipped row instead of a dead run.
#
# Usage: scripts/expr-switchover/run_gate.sh <outdir> [max-mb] [timeout-s]
set -u
OUT=${1:?usage: run_gate.sh <outdir> [max-mb] [timeout-s]}
MAXMB=${2:-8}
TMO=${3:-180}
REPO=$(cd "$(dirname "$0")/../.." && pwd)
FCCMD=$REPO/build/conda-relwithdebinfo-801/bin/FreeCADCmd
RIG=$REPO/scripts/expr-switchover/corpus_regression.py
mkdir -p "$OUT"
export QT_QPA_PLATFORM=offscreen
export FREECAD_USER_HOME=${FREECAD_USER_HOME:-$OUT/fchome}
mkdir -p "$FREECAD_USER_HOME"

"$REPO/.conda/limited.sh" "$REPO/.conda/run.sh" "$FCCMD" -c \
    "import sys; sys.argv=['gate','--max-mb','$MAXMB','--list-only']; exec(open('$RIG').read())" \
    2>/dev/null | grep -E "^ *[0-9]+ +[0-9]+ +/" > "$OUT/files.txt"
total=$(wc -l < "$OUT/files.txt")
echo "gate: $total files, ${TMO}s each"

: > "$OUT/all.jsonl"
: > "$OUT/timeouts.txt"
: > "$OUT/summaries.txt"
i=0
while read -r idx size path; do
    i=$((i + 1))
    printf "[%d/%d] %s\n" "$i" "$total" "$(basename "$path")"
    # the path goes through the ENVIRONMENT, never into the -c string:
    # real corpus paths contain quotes and backslashes
    # (.../FREE|\'CAD_CNC/...), which would end the Python literal.
    rm -f "$OUT/one.jsonl" "$OUT/one.jsonl.summary"
    FCX_GATE_ONLY="$path" timeout "$TMO" "$REPO/.conda/limited.sh" \
        "$REPO/.conda/run.sh" "$FCCMD" -c \
        "import sys; sys.argv=['gate','--max-mb','$MAXMB','--out','$OUT/one.jsonl']; exec(open('$RIG').read())" \
        > "$OUT/one.log" 2>&1
    rc=$?
    # FreeCADCmd exits 0 even when the script raised, so a missing
    # summary is the real failure signal, not the exit code
    if [ $rc -ne 0 ] || [ ! -f "$OUT/one.jsonl.summary" ]; then
        echo "$path rc=$rc" >> "$OUT/timeouts.txt"
        cp "$OUT/one.log" "$OUT/fail-$(basename "$path").log" 2>/dev/null
        continue
    fi
    [ -f "$OUT/one.jsonl" ] && cat "$OUT/one.jsonl" >> "$OUT/all.jsonl"
    [ -f "$OUT/one.jsonl.summary" ] && cat "$OUT/one.jsonl.summary" >> "$OUT/summaries.txt"
    rm -f "$OUT/one.jsonl" "$OUT/one.jsonl.summary"
done < "$OUT/files.txt"

echo "skipped/timed out: $(wc -l < "$OUT/timeouts.txt")"
python3 "$REPO/scripts/expr-switchover/summarize_gate.py" "$OUT"
