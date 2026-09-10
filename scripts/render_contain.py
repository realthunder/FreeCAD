#!/usr/bin/env python3
"""Geometry-mask containment between two captures (docs/RenderDebug.md 5.2).

The question this answers is not "do these two frames match" -- render_diff.py
does that -- but "is the SAME geometry here, moved". It builds a binary
geometry mask from the depth stage (mode 1) of each capture and reports what
fraction of the current mask falls inside the golden's, under each candidate
transform. A backend whose frame is mirrored, shifted or clipped says so in
one number, before anyone reads a pixel value.

Read the transforms in this order, and do not skip ahead:

  identity  -> containment at zero shift, as captured
  flip      -> the same with the current mask flipped vertically
  shift     -> the best integer offset found by search (--shift N)

**Try the flip before the shift.** A shift search will happily align two
lobes of a symmetric silhouette and read as confirmation: on the scene this
was written for, a 119 px translation matched at 99.90% while the vertical
flip that is the actual answer matched at 100.00% and went untested for a
round. Origin conventions differ per backend (bgfx's V origin for a render
target); a camera that is merely translated does not.

Containment is deliberately asymmetric -- |current AND golden| / |current| --
because that is what distinguishes a moved frame from a clipped one. A frame
missing its far half is 100% contained (everything drawn is in the right
place, there is just less of it) while a mirrored frame is not; --both prints
the reverse direction too, and the pair separates the two cases.

Usage:
  render_contain.py <golden-dir-or-png> <current-dir-or-png>
                    [--mode 1] [--thresh 0] [--shift 0] [--both] [--quiet]

Exit status: 0 = identity is the best transform, 1 = something else fits
better (read the report), 2 = nothing to compare.
Needs numpy + PIL (run via .conda/run.sh python).
"""
import argparse
import glob
import os
import sys

import numpy as np
from PIL import Image


def mask_of(path, thresh):
    """Geometry mask: depth-stage pixels above the background level.

    Mode 1 is the normalized prepass depth on a black background, so
    "not background" is the mask. It is close to, but not identical
    with, the sidecar's own geometryPixels -- the engine counts what it
    shaded, this counts what survived quantization into the PNG (108191
    against 106114 on refs/raster). Use it for placement, not as a
    pixel budget.
    """
    a = np.asarray(Image.open(path).convert("RGB"))
    return (a.max(axis=-1) > thresh)


def contain(cur, gold):
    n = int(cur.sum())
    if n == 0:
        return 0.0, 0
    return 100.0 * float(np.logical_and(cur, gold).sum()) / n, n


def shifted(m, dy, dx):
    out = np.zeros_like(m)
    h, w = m.shape
    ys, ye = max(0, dy), min(h, h + dy)
    xs, xe = max(0, dx), min(w, w + dx)
    if ys >= ye or xs >= xe:
        return out
    out[ys:ye, xs:xe] = m[ys - dy:ye - dy, xs - dx:xe - dx]
    return out


def best_shift(cur, gold, radius):
    best = (-1.0, 0, 0)
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            pct, _ = contain(shifted(cur, dy, dx), gold)
            if pct > best[0]:
                best = (pct, dy, dx)
    return best


def pick(directory, mode):
    if os.path.isfile(directory):
        return directory
    hits = sorted(glob.glob(os.path.join(directory, "*--mode%d.png" % mode)))
    return hits[0] if hits else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("golden")
    ap.add_argument("current")
    ap.add_argument("--mode", type=int, default=1,
                    help="capture stage to build the mask from (default 1, "
                         "the depth stage)")
    ap.add_argument("--thresh", type=int, default=0,
                    help="a pixel is geometry when its brightest channel "
                         "exceeds this (default 0)")
    ap.add_argument("--shift", type=int, default=0,
                    help="search integer offsets up to +/-N px; 0 skips it")
    ap.add_argument("--both", action="store_true",
                    help="also report the reverse direction, which separates "
                         "a clipped frame from a merely moved one")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    gp = pick(args.golden, args.mode)
    cp = pick(args.current, args.mode)
    if not gp or not cp:
        print("nothing to compare (no mode%d capture)" % args.mode)
        return 2
    gold = mask_of(gp, args.thresh)
    cur = mask_of(cp, args.thresh)
    if gold.shape != cur.shape:
        print("viewport differs: golden %s, current %s -- restage the camera "
              "before reading containment" % (gold.shape, cur.shape))
        return 2

    if not args.quiet:
        print("golden  %s  mask %d" % (gp, int(gold.sum())))
        print("current %s  mask %d" % (cp, int(cur.sum())))

    ident, n = contain(cur, gold)
    flip, _ = contain(np.flipud(cur), gold)
    print("identity  %6.2f%%  of %d current pixels" % (ident, n))
    print("flip      %6.2f%%" % flip)
    if args.both:
        rid, gn = contain(gold, cur)
        rfl, _ = contain(gold, np.flipud(cur))
        print("  reverse identity %6.2f%% of %d golden pixels" % (rid, gn))
        print("  reverse flip     %6.2f%%" % rfl)

    winner, why = ident, "identity"
    if flip > winner:
        winner, why = flip, "flip"
    if args.shift:
        pct, dy, dx = best_shift(cur, gold, args.shift)
        print("shift     %6.2f%%  at dy %+d dx %+d" % (pct, dy, dx))
        fpct, fdy, fdx = best_shift(np.flipud(cur), gold, args.shift)
        print("flip+shift %5.2f%%  at dy %+d dx %+d" % (fpct, fdy, fdx))
        if pct > winner + 0.005:
            winner, why = pct, "shift dy %+d dx %+d" % (dy, dx)
        if fpct > winner + 0.005:
            winner, why = fpct, "flip+shift dy %+d dx %+d" % (fdy, fdx)

    print("best: %s at %.2f%%" % (why, winner))
    if why != "identity":
        print("NOTE: a transform other than identity fits better. If that is "
              "the flip, it is an origin-convention fault and not a camera "
              "one -- see docs/RenderDebug.md 5.2b.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
