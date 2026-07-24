#!/usr/bin/env python3
"""Per-stage capture diffing (docs/RenderDebug.md §5, phase 3).

Compares two capture directories produced by render_verify.py, pairing
images by basename and grouping by (scene, camera, leg). Within a group
the stages are checked in *pipeline order* — depth, normal, AO, shadow,
then the composited beauty — and the report names the **first divergent
stage**, which localizes a regression to the pipeline stage that
introduced it rather than just flagging the final image.

A stage diverges when more than --frac of its pixels differ by more than
--tol on any channel (defaults absorb tiny driver/precision noise while
catching real changes; use --tol 0 --frac 0 for byte-exactness on a
frozen same-GPU A/B). Divergent stages get an amplified difference
heatmap written next to the report for eyeballing.

Usage:
  render_diff.py <golden-dir> <current-dir> [--tol 3] [--frac 0.001]
                 [--diffs <dir>] [--quiet]

Exit status: 0 = all groups clean, 1 = divergence, 2 = nothing to compare.
Needs numpy + PIL (run via .conda/run.sh python).
"""
import argparse
import glob
import os
import re
import sys

import numpy as np
from PIL import Image

# mode number -> (pipeline order, stage name); beauty last: it composites
# everything, so an upstream buffer diverging first is the real culprit.
STAGES = {1: (0, "depth"), 2: (1, "normal"), 3: (2, "ao"), 4: (3, "shadow"),
          0: (99, "beauty")}
CAPTURE_RE = re.compile(r"^(?P<group>.+)--mode(?P<mode>\d+)(?P<leg>--viewer(?:-\d+)?)?\.png$")


def scan(directory):
    """{(group, leg): {mode: path}} for every capture PNG in a dir."""
    out = {}
    for path in sorted(glob.glob(os.path.join(directory, "*.png"))):
        m = CAPTURE_RE.match(os.path.basename(path))
        if not m:
            continue
        key = (m.group("group"), m.group("leg") or "")
        out.setdefault(key, {})[int(m.group("mode"))] = path
    return out


def compare(path_a, path_b, tol):
    """(divergence fraction at tol, max delta, mean delta) or a str error."""
    a = np.asarray(Image.open(path_a).convert("RGB"), dtype=np.int16)
    b = np.asarray(Image.open(path_b).convert("RGB"), dtype=np.int16)
    if a.shape != b.shape:
        return "size %sx%s vs %sx%s" % (a.shape[1], a.shape[0], b.shape[1], b.shape[0])
    delta = np.abs(a - b).max(axis=2)
    frac = float((delta > tol).mean())
    return frac, int(delta.max()), float(delta.mean())


def write_heatmap(path_a, path_b, out_path):
    a = np.asarray(Image.open(path_a).convert("RGB"), dtype=np.int16)
    b = np.asarray(Image.open(path_b).convert("RGB"), dtype=np.int16)
    delta = np.abs(a - b).max(axis=2)
    amplified = np.clip(delta * 8, 0, 255).astype(np.uint8)
    heat = np.zeros(delta.shape + (3,), dtype=np.uint8)
    heat[..., 0] = amplified                       # red = difference (x8)
    heat[..., 1] = (a.max(axis=2) // 4).astype(np.uint8)  # faint scene context
    Image.fromarray(heat).save(out_path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("golden")
    ap.add_argument("current")
    ap.add_argument("--tol", type=int, default=3,
                    help="per-channel delta a pixel may have (default 3)")
    ap.add_argument("--frac", type=float, default=0.001,
                    help="fraction of pixels allowed past --tol (default 0.001)")
    ap.add_argument("--diffs", default="",
                    help="write difference heatmaps for divergent stages here")
    ap.add_argument("--quiet", action="store_true",
                    help="only print divergent groups")
    args = ap.parse_args()

    golden, current = scan(args.golden), scan(args.current)
    if not golden:
        print("no captures in golden dir", args.golden)
        return 2
    if args.diffs:
        os.makedirs(args.diffs, exist_ok=True)

    any_divergence = compared = 0
    for key in sorted(golden):
        group, leg = key
        label = group + (leg or "")
        if key not in current:
            print("MISSING  %s: no captures in %s" % (label, args.current))
            any_divergence = 1
            continue
        modes = sorted(set(golden[key]) & set(current[key]),
                       key=lambda m: STAGES.get(m, (50, ""))[0])
        skipped = sorted(set(golden[key]) ^ set(current[key]))
        first_divergent, lines = None, []
        for m in modes:
            stage = STAGES.get(m, (0, "mode%d" % m))[1]
            r = compare(golden[key][m], current[key][m], args.tol)
            compared += 1
            if isinstance(r, str):
                diverged, detail = True, r
            else:
                frac, dmax, dmean = r
                diverged = frac > args.frac
                detail = "%.4f%% px > tol, max %d, mean %.2f" % (frac * 100, dmax, dmean)
            lines.append("  %-8s %-9s %s" % (stage, "DIVERGED" if diverged else "ok", detail))
            if diverged:
                first_divergent = first_divergent or stage
                if args.diffs and not isinstance(r, str):
                    write_heatmap(golden[key][m], current[key][m], os.path.join(
                        args.diffs, os.path.basename(golden[key][m])
                        .replace(".png", "--diff.png")))
        if first_divergent:
            any_divergence = 1
            print("DIVERGED %s (first divergent stage: %s)" % (label, first_divergent))
            print("\n".join(lines))
        elif not args.quiet:
            print("OK       %s (%d stages)" % (label, len(modes)))
        if skipped and not args.quiet:
            print("  note: modes only on one side: %s" % skipped)

    if not compared:
        print("no comparable capture pairs")
        return 2
    return any_divergence


if __name__ == "__main__":
    sys.exit(main())
