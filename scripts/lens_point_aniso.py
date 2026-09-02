"""How wrong is a point sprite's SHAPE where a lens warps anisotropically?

    FreeCAD scripts/lens_point_aniso.py       (or over MCP)

THE LIMIT.  fs_fc_glass.sc rebuilds a decoration's coverage by turning
its stored field distance into post-lens pixels through the Jacobian of
the refraction mapping.  A LINE carries its own perpendicular axis in
aux.xy, so it is scaled along the one direction its field falls in.  A
POINT stores a zero axis -- its box distance has no single fall
direction -- and takes the mean of the two instead:

    s = 0.5 * (|grad u| + |grad v|)

The note that shipped with it called the residual "a corner, not a
size".  This probe measured it, and it was a size.  The writer now
sends the box distance's own dominant axis instead of a zero vector,
so a sprite takes the same per-axis path a line does; the mean branch
is left as the degenerate fallback.

WHAT THE MEAN COSTS, predicted.  The sprite's edge sits where the field
distance reaches `s * halfw`.  A screen step of one pixel moves the
sample by |grad u| in the field's u and |grad v| in its v, so the drawn
half extents are `s * halfw / |grad u|` and `s * halfw / |grad v|` --
and with s the MEAN of the two, a sprite that should be square on
screen comes out with

    aspect  =  |grad v| / |grad u|  =  the local warp anisotropy A

stretched by (1 + A) / 2 along one axis and squeezed by (1 + A) / (2A)
along the other.  At A = 2 that is 50% too long one way and 25% too
short the other -- not a corner.  So the question is not whether the
shape is wrong, it is what A a lens actually reaches where sprites are
still visible.

THE MEASUREMENT.  One scene answers both halves, which is why there is
no cross-hatch here: a GRID of points is its own ground truth.  Each
imaged sprite gives its own shape, and the spacing to its neighbours
gives the mapping's two scales at that same place -- radially (toward
the disc centre) and tangentially.  Comparing the measured aspect
against the measured anisotropy tests the prediction rather than
assuming it.

Frames are differenced against the same scene with the points hidden,
so the sphere's limb darkening, its environment specular and its
shading cancel exactly and what is left is sprite ink alone.  (See
scripts/lens_rim_fade.py, where that trick was worked out, and its two
framing traps -- both apply here.)

MEASURED 2026-08-30, ior 1.6, sphere r=46, disc 282px, 9px vertices.
Radial x tangential extent of one sprite, at matched radii, before and
after the writer sent an axis:

    r/R        with the mean branch        per-axis
    0.57        6.8 x 11.2                 10.1 x  9.0
    0.57        7.0 x 11.0                  9.5 x  8.0
    0.58        7.9 x 10.0                  8.7 x  9.0
    0.58        8.2 x  9.9                 10.2 x  9.9
    0.81        5.6 x 14.5                  9.3 x 10.8
    0.81        5.3 x 16.9                  8.4 x 10.9
    0.82        6.5 x 15.9                  9.0 x  9.8

Control, sprites clear of the glass: 8.4 x 8.3 px for an asked-for 9.
Inside r/R 0.7, where this probe's own ground truth is trustworthy
(see the verdict window below), at a warp anisotropy of 1.7:1 the
sprite is 8% out of square on average and 15% at worst, against 72%
and 73% for the isotropic mean -- 9.2x closer to square.

The pre-fix column also confirms the arithmetic that predicted it: at
r/R 0.81 the mean branch gave 5.6 x 14.5 where `s * halfw / |grad|`
predicts 6.3 x 15.8.

! Knobs come from os.environ, which is FREECAD'S, not the calling
shell's -- this runs inside the app.  Drive it from a wrapper that sets
os.environ and then exec()s this file.
"""
import os
import time

import numpy as np
from PIL import Image

import FreeCAD
import FreeCADGui
import Part

OUT = os.environ.get("LL_OUT", os.path.join(os.path.expanduser("~"),
                                            "ll-probe"))
IOR = float(os.environ.get("LP_IOR", "1.6"))
RADIUS = float(os.environ.get("LP_RADIUS", "46"))
PSIZE = float(os.environ.get("LP_PSIZE", "9"))
PITCH = float(os.environ.get("LP_PITCH", "8"))
SPAN = 2.0
RESULT = os.path.join(OUT, "aniso_ior%s.txt" % os.environ.get("LP_IOR", "1.6"))
LOG = []


def say(m):
    LOG.append(m)
    FreeCAD.Console.PrintMessage("[agn] %s\n" % m)


def get_view():
    for v in FreeCADGui.ActiveDocument.mdiViewsOfType("Gui::View3DInventor"):
        if hasattr(v, "redraw") and hasattr(v, "getPointOnScreen"):
            return v
    return None


def settle(view, rounds=14):
    for _ in range(rounds):
        time.sleep(0.05)
        for _ in range(3):
            view.redraw()
            FreeCADGui.updateGui()


def build(doc):
    n = int(SPAN * RADIUS / PITCH)
    verts = []
    for i in range(-n, n + 1):
        for j in range(-n, n + 1):
            verts.append(Part.Vertex(i * PITCH, j * PITCH, 0.0))
    grid = doc.addObject("Part::Feature", "Grid")
    grid.Shape = Part.makeCompound(verts)
    lens = doc.addObject("Part::Sphere", "Lens")
    lens.Radius = RADIUS
    lens.Placement.Base = FreeCAD.Vector(0.0, 0.0, RADIUS)
    doc.recompute()
    gv = grid.ViewObject
    gv.PointColor = (0.0, 0.0, 0.0)
    gv.PointSize = PSIZE
    if "Points" in gv.listDisplayModes():
        gv.DisplayMode = "Points"
    lvo = lens.ViewObject
    lvo.Transparency = 60
    # The sphere's own seam edge and pole vertices are black decorations
    # of its own; Shaded drops them so every blob found below is a grid
    # point and only a grid point.
    if "Shaded" in lvo.listDisplayModes():
        lvo.DisplayMode = "Shaded"
    for prop, val, kind in (("Render_Glass", True, "App::PropertyBool"),
                            ("Render_GlassIOR", IOR, "App::PropertyFloat"),
                            ("Render_GlassRoughness", 0.0,
                             "App::PropertyFloat")):
        if not hasattr(lvo, prop):
            lvo.addProperty(kind, prop)
        setattr(lvo, prop, val)
    return grid, lens


def grab(view, path):
    view.saveRenderDump(path, metadata=False)
    view.saveRenderDump(path, metadata=False)
    img = np.asarray(Image.open(path).convert("RGB")).astype(float)
    return img.mean(axis=2)


def blobs(ink, cx, cy, rpx, floor, sep):
    """Isolated sprite centres: local maxima of ink, non-max suppressed."""
    h, w = ink.shape
    out = []
    ys, xs = np.where(ink > floor)
    order = np.argsort(-ink[ys, xs])
    taken = []
    for k in order:
        y, x = int(ys[k]), int(xs[k])
        r = np.hypot(x - cx, y - cy)
        if r > 0.97 * rpx:
            continue
        if any(abs(x - px) < sep and abs(y - py) < sep
               for px, py in taken):
            continue
        taken.append((x, y))
        out.append((x, y, float(ink[y, x])))
    return out


def extent(prof, peak_i, peak):
    """Full width where the profile crosses half its peak."""
    half = 0.5 * peak
    n = len(prof)
    a = b = None
    for i in range(peak_i, 0, -1):
        if prof[i] >= half > prof[i - 1]:
            a = i - (half - prof[i - 1]) / max(prof[i] - prof[i - 1], 1e-6)
            break
    for i in range(peak_i, n - 1):
        if prof[i] >= half > prof[i + 1]:
            b = i + (prof[i] - half) / max(prof[i] - prof[i + 1], 1e-6)
            break
    if a is None or b is None:
        return None
    return b - a


def main():
    if not os.path.isdir(OUT):
        os.makedirs(OUT)
    prm = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    prm.SetInt("RenderCache", 3)
    prm.SetBool("ShowNaviCube", False)
    prm.SetBool("ShowAxisCross", False)
    prm.SetInt("AntiAliasing", 0)
    FreeCAD.ParamGet(
        "User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
    d = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
    d.SetInt("AutoSaveTimeout", 0)
    d.SetBool("AutoSaveEnabled", False)

    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    doc = FreeCAD.newDocument("lensaniso")
    grid, lens = build(doc)

    view = get_view()
    if view is None:
        say("NO VIEW")
        return
    view.setCameraType("Orthographic")
    view.viewTop()
    view.fitAll()
    # Never fitAll alone: the grid is two radii wide, so it frames the
    # GRID and leaves the sphere a fraction of the height.
    view.getCameraNode().height.setValue(2.2 * RADIUS)
    settle(view)

    c = lens.Placement.Base
    cs = view.getPointOnScreen(c.x, c.y, c.z)
    es = view.getPointOnScreen(c.x + lens.Radius.Value, c.y, c.z)
    cxs, cys = float(cs[0]), float(cs[1])
    rpx = abs(float(es[0]) - cxs)
    say("sphere r=%.0f ior=%.2f  point size %.0f px  grid pitch %.0fmm"
        % (RADIUS, IOR, PSIZE, PITCH))
    say("disc radius %.0f px on screen" % rpx)
    if rpx < 150:
        say("!! disc too small; check the framing")
        return

    grid.ViewObject.Visibility = False
    settle(view, 8)
    base = grab(view, os.path.join(OUT, "aniso_base.png"))
    grid.ViewObject.Visibility = True
    settle(view, 8)
    shown = grab(view, os.path.join(OUT, "aniso_ior%.2f.png" % IOR))
    ink = base - shown
    h, w = ink.shape
    cy = (h - 1) - cys

    # Sprites clear of the glass: the control. They never went through
    # the lens, so they say what the asked-for size actually rasterizes
    # to, which is what the warped ones have to come back to.
    ctrl = []
    for (x, y, pk) in blobs(ink, cxs, cy, 1e9, floor=20.0,
                            sep=int(PSIZE) + 3):
        if np.hypot(x - cxs, y - cy) < 1.25 * rpx:
            continue
        half = int(PSIZE) + 6
        x0, x1 = max(0, x - half), min(w, x + half + 1)
        y0, y1 = max(0, y - half), min(h, y + half + 1)
        a = extent(ink[y, x0:x1], x - x0, pk)
        b = extent(ink[y0:y1, x], y - y0, pk)
        if a and b:
            ctrl.append((a, b))
    if ctrl:
        ca = np.array(ctrl)
        say("control, %d sprites clear of the glass: %.1f x %.1f px"
            " for an asked-for %.0f"
            % (len(ctrl), ca[:, 0].mean(), ca[:, 1].mean(), PSIZE))

    say("")
    found = blobs(ink, cxs, cy, rpx, floor=20.0, sep=int(PSIZE) + 3)
    say("%d sprites found inside the disc" % len(found))
    if len(found) < 6:
        say("!! too few sprites resolved to fit the anisotropy")
        return

    pts = np.array([[b[0], b[1]] for b in found], dtype=float)
    rows = []
    for (x, y, pk) in found:
        ix, iy = int(x), int(y)
        half = int(PSIZE) + 6
        x0, x1 = max(0, ix - half), min(w, ix + half + 1)
        y0, y1 = max(0, iy - half), min(h, iy + half + 1)
        wx = extent(ink[iy, x0:x1], ix - x0, pk)
        wy = extent(ink[y0:y1, ix], iy - y0, pk)
        if wx is None or wy is None or wx <= 0 or wy <= 0:
            continue
        rx, ry = x - cxs, y - cy
        r = np.hypot(rx, ry)
        if r < 1e-6:
            continue
        rr = r / rpx
        # Radial and tangential unit directions at this sprite.
        ur = np.array([rx, ry]) / r
        ut = np.array([-ur[1], ur[0]])
        # Nearest neighbour along each: the mapping's own scale bar.
        d = pts - np.array([x, y])
        dist = np.hypot(d[:, 0], d[:, 1])
        good = dist > 1e-6
        if good.sum() < 4:
            continue
        proj_r = np.abs(d[:, 0] * ur[0] + d[:, 1] * ur[1])
        proj_t = np.abs(d[:, 0] * ut[0] + d[:, 1] * ut[1])
        # A radial neighbour is one whose offset is mostly radial.
        mr = good & (proj_r > 2.0 * proj_t)
        mt = good & (proj_t > 2.0 * proj_r)
        if not mr.any() or not mt.any():
            continue
        stepr = float(proj_r[mr].min())
        stept = float(proj_t[mt].min())
        if stepr < 1.0 or stept < 1.0:
            continue
        # One grid pitch spans stepr px radially and stept px
        # tangentially, so the mapping's gradients go as 1/step and the
        # anisotropy is the ratio.
        aniso = stepr / stept
        # The sprite's aspect along those same two directions. wx/wy are
        # measured on the screen axes, so only compare them where the
        # radial direction IS a screen axis; otherwise skip.
        # wx/wy are on the screen axes, so radial and tangential are
        # only separable where the radial direction IS a screen axis.
        if abs(ur[0]) > 0.97:
            wr, wt = wx, wy
        elif abs(ur[1]) > 0.97:
            wr, wt = wy, wx
        else:
            continue
        rows.append((rr, wr, wt, wr / wt, aniso, stepr, stept))

    if not rows:
        say("!! no sprite sat on a screen axis with resolvable"
            " neighbours; raise the grid density")
        return
    rows.sort()
    say("")
    say("sprites lying on a screen axis, where radial and tangential"
        " ARE the measured axes")
    say("'mean-branch' is the aspect the old isotropic scaling would"
        " give: the anisotropy itself")
    say(" r/R   radial tangential  aspect   anisotropy  mean-branch")
    say("        (px)     (px)     (r/t)    (step r/t)    would be")
    for (rr, wr, wt, asp, an, sr, st) in rows:
        say("%5.2f %7.1f %9.1f %8.2f %11.2f %11.2f"
            % (rr, wr, wt, asp, an, an))

    # The rim is excluded from the verdict, not from the table. Past
    # r/R 0.9 two separate things spoil it: the support radius starts
    # truncating the sprite (scripts/lens_rim_fade.py), and the grid
    # crowds hard enough that the nearest-neighbour step -- this
    # probe's own ground truth -- picks the wrong neighbour, which
    # shows up as the anisotropy jumping between 0.29 and 2.4 at one
    # radius.
    # The verdict window stops at r/R 0.7, and the limit is this
    # probe's GROUND TRUTH, not the renderer. Further out the grid
    # crowds until the nearest "radial" neighbour is a diagonal one,
    # and the anisotropy comes back as its own reciprocal -- at r/R
    # 0.81 four sprites read 0.43, 0.45, 2.39 and 2.47, which cannot
    # all be true of a radially symmetric lens. The sprite SHAPES out
    # there are still in the table and still near square; it is only
    # the reference they would be judged against that is unusable.
    keep = [r for r in rows if r[0] <= 0.7]
    say("")
    if not keep:
        say("!! no sprite inside r/R 0.7 to judge")
        return
    # The lens is radially symmetric, so anisotropy is a function of
    # r/R alone -- which makes a disagreement AT one radius a
    # measurement failure, not a result. It happens: at r/R 0.81 three
    # sprites read anisotropy 0.43-0.45 and a fourth reads 2.47,
    # because its nearest "radial" neighbour was not one grid pitch
    # away. Bin by radius and take medians so one bad blob cannot set
    # the verdict.
    say("binned by radius, median over each bin (the lens is radially"
        " symmetric, so anisotropy depends on r/R alone)")
    say("  r/R bin   n   median aspect   median anisotropy")
    binned = {}
    for r in keep:
        binned.setdefault(round(r[0] / 0.05) * 0.05, []).append(r)
    asp, an = [], []
    for b in sorted(binned):
        grp = binned[b]
        ma = float(np.median([g[3] for g in grp]))
        mn = float(np.median([g[4] for g in grp]))
        asp.append(ma)
        an.append(mn)
        say("     %.2f  %3d %14.2f %19.2f" % (b, len(grp), ma, mn))
    asp = np.array(asp)
    an = np.array(an)
    dev = np.abs(asp - 1.0)
    say("")
    # What the isotropic mean branch would have cost over the same
    # sprites: aspect = anisotropy, so its deviation from square is
    # |A - 1| expressed the same way.
    olddev = np.abs(np.where(an > 1.0, an, 1.0 / np.maximum(an, 1e-6))
                    - 1.0)
    newdev = np.abs(np.where(asp > 1.0, asp, 1.0 / np.maximum(asp, 1e-6))
                    - 1.0)
    say("inside r/R 0.7, over %d sprites in %d radius bins, anisotropy"
        " %.2f to %.2f:" % (len(keep), len(an), an.min(), an.max()))
    say("  a square sprite is aspect 1.00. measured mean %.2f,"
        " worst %.2f" % (float(asp.mean()), float(asp[dev.argmax()])))
    say("  out-of-square: measured %.0f%% mean / %.0f%% worst,"
        " against %.0f%% / %.0f%% for the isotropic mean branch"
        % (100.0 * newdev.mean(), 100.0 * newdev.max(),
           100.0 * olddev.mean(), 100.0 * olddev.max()))
    if newdev.mean() < 0.5 * olddev.mean():
        say("VERDICT: per-axis scaling holds the sprite square through"
            " the warp; the mean branch would be %.1fx further out."
            % (olddev.mean() / max(newdev.mean(), 1e-6)))
    else:
        say("VERDICT: the sprite is still going out of square with the"
            " warp -- per-axis scaling is not doing its job.")

    with open(RESULT, "w") as f:
        f.write("\n".join(LOG) + "\n")
    say("wrote %s" % RESULT)


main()
