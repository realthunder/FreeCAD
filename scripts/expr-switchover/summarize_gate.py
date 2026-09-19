#!/usr/bin/env python3
"""Summarize a run_gate.sh output directory into the gap list."""
import collections
import json
import os
import re
import sys

out = sys.argv[1] if len(sys.argv) > 1 else "."
counts = collections.Counter()
verdicts = collections.Counter()
gaps = collections.Counter()
differs = collections.Counter()

# per-file summaries carry the totals (including the "same" rows, which
# the detail file deliberately does not record)
for line in open(os.path.join(out, "summaries.txt"), errors="replace"):
    m = re.match(r"\s+(\w+)\s+(\d+)$", line)
    if m:
        counts[m.group(1)] += int(m.group(2))

for line in open(os.path.join(out, "all.jsonl"), errors="replace"):
    try:
        rec = json.loads(line)
    except ValueError:
        continue
    v = rec.get("verdict", "open_error")
    verdicts[v] += 1
    if v == "image_only_error":
        msg = (rec.get("image_error") or "").split("\n")[0]
        m = re.search(r"'sErrMsg': ['\"](.*?)['\"],", msg)
        gaps[(m.group(1) if m else msg)[:120]] += 1
    elif v == "differ":
        differs["%s -> %s" % (str(rec.get("native"))[:40],
                              str(rec.get("image"))[:40])] += 1

print("=== corpus gate totals (from per-file summaries) ===")
for key in ("files", "files_failed", "expressions", "same", "differ",
            "both_error", "both_error_text_differs", "image_only_error",
            "native_only_error"):
    print("  %-18s %d" % (key, counts[key]))
timeouts = os.path.join(out, "timeouts.txt")
if os.path.exists(timeouts):
    n = sum(1 for _ in open(timeouts))
    print("  %-18s %d" % ("timed_out_files", n))

if gaps:
    print("\nimage-only failures (the gap list):")
    for msg, n in gaps.most_common(25):
        print("  %5d  %s" % (n, msg))
if differs:
    print("\nvalue divergences:")
    for msg, n in differs.most_common(25):
        print("  %5d  %s" % (n, msg))
