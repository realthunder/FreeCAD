#!/usr/bin/env python
"""Judge show-on-top hidden-edge rendering in screenshots of the standard
box(hidden, on-top) + cylinder repro (scripts/ontop_edge_repro.py).

Per-edge expectations, verified against the plain-GL reference pipeline:
  - HIDDEN edges (the three back edges through the center vertex, and the
    left box edge occluded by the cylinder) must be DIMMED: a line is
    present but clearly lighter than solid lines (TransparencyOnTop).
  - FRONT/silhouette edges must be SOLID dark.
  - The selected edge (Edge2, the top-left hexagon edge) must be GREEN.

Judging rule learned the hard way: never trust a fixed-pixel luminance
probe — a dimmed line (a half-blend with its local background) is
indistinguishable from plain background at one point, and background
gradients sit exactly in "dimmed" luminance range. A sample point is only
accepted as a *line* if the darkest pixel in a small scan window is
clearly darker than the local background measured 12-30 px to each side,
and each edge is sampled at many points along its known path.

Usage:
  python ontop_edge_judge.py judge <img.png> [more.png ...]
      Judge captures; exit code = number of failing images.
  python ontop_edge_judge.py trace <img.png> [more.png ...]
      Dump detected line runs per scanline (x:lum pairs, G = green) —
      use this to re-derive the EDGES table when the scene or camera
      changes.

The EDGES coordinates assume the repro script's canonical grab: main
window resized to 1600x837, viewAxonometric + fitAll with both objects
visible. Anything else: re-derive with `trace`.
"""

import os
import statistics
import sys

from PIL import Image

SOLID_MAX = 55      # median line lum <= this -> solid
DIM_MIN = 60        # median line lum >= this (and a line was found) -> dimmed
CONTRAST = 25       # line pixel must be this much darker than local bg
FOUND_FRAC = 0.5    # fraction of sample points that must contain a line


def span(y0, y1, fx, step=8):
    return [(int(round(fx(y))), y) for y in range(y0, y1 + 1, step)]


# Edge sample paths in full-image coords of the canonical 1600x837 grab.
EDGES = [
    # hidden edges: behind the box's own front faces
    ("back vertical (upper)",        "dim",   span(155, 335, lambda y: 916)),
    ("back-bottom diag left",        "dim",   span(355, 438, lambda y: 916 - (y - 345) * 1.72)),
    ("back-bottom diag right",       "dim",   span(355, 393, lambda y: 916 + (y - 345) * 1.72)),
    # hidden edge: behind the cylinder (which is in front of the box)
    ("left box edge over cylinder",  "dim",   span(260, 460, lambda y: 743)),
    # front edges: must stay solid
    ("front vertical (lower)",       "solid", span(352, 535, lambda y: 916)),
    ("top-face back-left diag",      "solid", span(266, 338, lambda y: 916 - (345 - y) * 1.729)),
    ("top-face back-right diag",     "solid", span(302, 338, lambda y: 916 + (345 - y) * 1.72)),
    ("top-right hexagon edge",       "solid", span(152, 190, lambda y: 916 + (y - 142) * 1.72)),
    ("bottom hexagon edge left",     "solid", span(502, 538, lambda y: 838 + (y - 500) * 1.725)),
    ("bottom hexagon edge right",    "solid", span(502, 538, lambda y: 995 - (y - 500) * 1.75)),
    # the selected edge (top-left hexagon edge): must be green, never dimmed
    ("selected edge (green)",        "green", span(152, 248, lambda y: 909 - (y - 150) * 1.716)),
]


def judge(path):
    im = Image.open(path).convert("RGB")
    px = im.load()

    def lum(x, y):
        p = px[x, y]
        return (p[0] + p[1] + p[2]) // 3

    print(f"\n== {os.path.basename(path)}  ({im.width}x{im.height})")
    ok = True
    for name, expect, pts in EDGES:
        lums, greens, found = [], 0, 0
        for (xc, y) in pts:
            best = min(range(xc - 8, xc + 9), key=lambda x: lum(x, y))
            v = lum(best, y)
            p = px[best, y]
            side = [lum(x, y)
                    for x in list(range(xc - 30, xc - 11)) + list(range(xc + 12, xc + 31))]
            bg = statistics.median(side)
            isgreen = p[1] > 120 and p[1] > p[0] + 60 and p[1] > p[2] + 60
            if v < bg - CONTRAST or isgreen:
                found += 1
                lums.append(v)
                greens += isgreen
        frac = found / len(pts)
        med = statistics.median(lums) if lums else None
        if frac < FOUND_FRAC:
            verdict, good = "MISSING (no line found)", False
        elif expect == "green":
            good = greens / max(found, 1) > 0.7
            verdict = "GREEN" if good else f"NOT GREEN (lum {med})"
        elif med <= SOLID_MAX:
            verdict, good = f"SOLID (lum {med})", expect == "solid"
        elif med >= DIM_MIN:
            verdict, good = f"DIMMED (lum {med})", expect == "dim"
        else:
            verdict, good = f"AMBIGUOUS (lum {med})", False
        mark = "ok  " if good else "FAIL"
        print(f"  [{mark}] {name:30s} expect {expect:5s} -> {verdict}"
              f"  ({found}/{len(pts)} pts)")
        ok = ok and good
    print(f"  => {'PASS' if ok else 'FAIL: on-top edge rendering deviates'}")
    return ok


def trace(path, x0=660, x1=1010, y0=150, y1=545, ystep=10):
    im = Image.open(path).convert("RGB")
    px = im.load()
    print(f"= {os.path.basename(path)}  ({im.width}x{im.height})")
    for y in range(y0, y1, ystep):
        lums = [(px[x, y][0] + px[x, y][1] + px[x, y][2]) // 3
                for x in range(x0 - 25, x1 + 25)]
        runs, cur = [], None
        for x in range(x0, x1):
            i = x - (x0 - 25)
            win = lums[i - 25:i - 6] + lums[i + 7:i + 26]
            bg = statistics.median(win)
            v = lums[i]
            r, g, b = px[x, y]
            isgreen = g > 120 and g > r + 60 and g > b + 60
            if v < bg - CONTRAST or isgreen:
                if cur is None:
                    cur = [x, x, v, isgreen]
                else:
                    cur[1], cur[2], cur[3] = x, min(cur[2], v), cur[3] or isgreen
            elif cur is not None:
                runs.append(cur)
                cur = None
        if cur:
            runs.append(cur)
        txt = "  ".join(f"{(a+b)//2}:{v}{'G' if g else ''}"
                        for a, b, v, g in runs if b - a <= 12)
        print(f"y{y:3d}  {txt}")


if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[1] not in ("judge", "trace"):
        sys.exit(__doc__)
    if sys.argv[1] == "trace":
        for p in sys.argv[2:]:
            trace(p)
        sys.exit(0)
    results = {p: judge(p) for p in sys.argv[2:]}
    print("\nSummary:")
    for p, r in results.items():
        print(f"  {os.path.basename(p):28s} {'PASS' if r else 'FAIL'}")
    sys.exit(sum(not r for r in results.values()))
