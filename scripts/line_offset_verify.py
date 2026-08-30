"""Does the fill still eat the face-side half of a thick line?

    FreeCAD scripts/line_offset_verify.py     (or run it over MCP)

THE ARTIFACT.  A thick line is a screen-space quad centred on the edge
that carries the EDGE's depth across its whole width -- the expansion
moves xy only.  So wherever the adjacent face rises toward the camera it
beats the line, and that half of the quad is never drawn.  A
preselection highlight has the depth test off, which is why it shows the
full width when the ordinary draw does not.

Polygon offset is what should clear that, and it points the right way:
at width 1 nothing is lost.  It is the MAGNITUDE that was wrong.  GL's
`factor` multiplies the depth slope in units of one pixel, so it reads
as "pixels of clearance", and 1 is right for an edge lying ON the
surface -- coincident, needing only a tie broken.  A thick line reaches
width/2 pixels sideways, so it needs width/2 cleared.  Measured before
the fix, on bgfx and on Coin's GL renderer alike, the drawn width came
up exactly `width/2 - 1` short -- that -1 being the one pixel the factor
was buying.

MEASURING IT.  Sample PERPENDICULAR to the edge's own screen direction,
never along a fixed image axis.  An earlier version scanned image rows
and reported a 1px line as 7px wide: that camera projected the edge
near-horizontally, so a row crossed it lengthwise.  The perpendicular is
computed per edge from two projected points, so the probe does not care
how the view is oriented.

Reported with the fill (Flat Lines) and without it (Wireframe), over a
range of widths.  PASS is the last column ~0 everywhere.
"""
import os
import time

import numpy as np
from PIL import Image

import FreeCAD
import FreeCADGui

OUT = os.environ.get("LV_OUT", os.path.join(os.path.expanduser("~"),
                                            "lv-probe"))
RESULT = os.path.join(OUT, "result.txt")
LOG = []


def say(m):
    LOG.append(m)
    FreeCAD.Console.PrintMessage("[lv] %s\n" % m)


def get_view():
    # ! ActiveView can be a bare Gui.MDIView with no redraw(); ask by
    # type and check the API is really there.
    for v in FreeCADGui.ActiveDocument.mdiViewsOfType("Gui::View3DInventor"):
        if hasattr(v, "redraw") and hasattr(v, "getPointOnScreen"):
            return v
    return None


def settle(view, rounds=12):
    for _ in range(rounds):
        time.sleep(0.05)
        for _ in range(3):
            view.redraw()
            FreeCADGui.updateGui()


def grab(view, name, framebuffer=False):
    p = os.path.join(OUT, name + ".png")
    if framebuffer:
        view.saveRenderDump(p, source="framebuffer", metadata=False)
    else:
        view.saveRenderDump(p, metadata=False)
    return np.asarray(Image.open(p).convert("RGB")).astype(np.float64)


def bilinear(lum, x, y):
    h, w = lum.shape[:2]
    x = np.clip(x, 0.0, w - 1.001)
    y = np.clip(y, 0.0, h - 1.001)
    x0 = np.floor(x).astype(int)
    y0 = np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    return (lum[y0, x0] * (1 - fx) * (1 - fy)
            + lum[y0, x0 + 1] * fx * (1 - fy)
            + lum[y0 + 1, x0] * (1 - fx) * fy
            + lum[y0 + 1, x0 + 1] * fx * fy)


def cross_width(lum, cx, cy, nx, ny, reach=25.0, step=0.1):
    """Width of the dark band along the perpendicular through (cx, cy).

    Half-max crossings located from the outside in, so a saturated core
    cannot bias the result the way centring on the darkest pixel did.
    """
    s = np.arange(-reach, reach + 1e-9, step)
    prof = bilinear(lum, cx + nx * s, cy + ny * s)
    n = len(prof)
    bgl = float(np.median(prof[:20]))
    bgr = float(np.median(prof[-20:]))
    dark = float(prof.min())
    if bgl - dark < 20 and bgr - dark < 20:
        return None
    hl, hr = 0.5 * (dark + bgl), 0.5 * (dark + bgr)
    li = ri = None
    for i in range(n - 1):
        if prof[i] >= hl > prof[i + 1]:
            li = i + (prof[i] - hl) / (prof[i] - prof[i + 1])
            break
    for i in range(n - 1, 0, -1):
        if prof[i] >= hr > prof[i - 1]:
            ri = i - (prof[i] - hr) / (prof[i] - prof[i - 1])
            break
    if li is None or ri is None or ri <= li:
        return None
    return (ri - li) * step


def edge_samples(view, edge, h, n=3):
    """Screen centres along the edge, plus its screen-space perpendicular."""
    p0, p1 = edge.Vertexes[0].Point, edge.Vertexes[-1].Point
    a = view.getPointOnScreen(p0.x, p0.y, p0.z)
    b = view.getPointOnScreen(p1.x, p1.y, p1.z)
    dx, dy = float(b[0] - a[0]), float(-(b[1] - a[1]))   # into image coords
    ln = (dx * dx + dy * dy) ** 0.5
    if ln < 1e-6:
        return None
    nx, ny = -dy / ln, dx / ln
    out = []
    for t in np.linspace(0.30, 0.70, n):
        q = p0 + (p1 - p0) * float(t)
        sx, sy = view.getPointOnScreen(q.x, q.y, q.z)
        out.append((float(sx), float(h - 1 - sy)))
    return out, nx, ny


def main():
    if not os.path.isdir(OUT):
        os.makedirs(OUT)
    doc = FreeCAD.ActiveDocument
    box = None
    for o in doc.Objects:
        if hasattr(o, "Shape") and len(getattr(o.Shape, "Edges", [])) >= 6:
            box = o
            break
    if box is None:
        say("!! no shape with edges in the active document")
        return
    view = get_view()
    if view is None:
        say("!! no Gui::View3DInventor with the camera API")
        return
    vo = box.ViewObject
    was_mode, was_width = vo.DisplayMode, vo.LineWidth

    settle(view)
    img = grab(view, "lv-probe")
    h, w = img.shape[:2]
    say("dump %dx%d, widget %s" % (w, h, view.getSize()))

    # Pick an edge that can show the artifact: VISIBLE (its own midpoint
    # picks the edge, not something in front of it) and with an adjacent
    # face rising toward the camera. An earlier version skipped the
    # visibility test and chose an edge hidden behind the whole solid.
    from pivy import coin
    cam = view.getCameraNode()
    cpos = FreeCAD.Vector(*cam.position.getValue().getValue())
    dz = cam.orientation.getValue().multVec(coin.SbVec3f(0, 0, -1))
    vdir = FreeCAD.Vector(dz[0], dz[1], dz[2])
    vdir.normalize()

    def depth(p):
        return (p - cpos).dot(vdir)

    best = None
    for i, ed in enumerate(box.Shape.Edges):
        a, b = ed.Vertexes[0].Point, ed.Vertexes[-1].Point
        mid = a + (b - a) * 0.5
        sx, sy = view.getPointOnScreen(mid.x, mid.y, mid.z)
        at = view.getObjectInfo((int(round(sx)), int(round(sy))))
        if not at or not str(at.get("Component", "")).startswith("Edge"):
            continue                       # hidden behind something
        de = depth(mid)
        margin = 0.0
        for d in (-4, -2, 2, 4):
            info = view.getObjectInfo((int(round(sx)) + d, int(round(sy))))
            if not info:
                continue
            if not str(info.get("Component", "")).startswith("Face"):
                continue
            q = FreeCAD.Vector(info["x"], info["y"], info["z"])
            margin = max(margin, de - depth(q))    # > 0 = face is nearer
        if margin > 0.0 and (best is None or margin > best[1]):
            best = (i, margin, ed)
    if best is None:
        say("!! no visible edge here has a face rising toward the camera,")
        say("   so the artifact cannot occur in this view.")
        return
    idx, margin, edge = best
    say("measuring Edge%d: visible, and its face is %.4f nearer than the"
        " edge" % (idx + 1, margin))

    def measure(tag, fb=False):
        lum = grab(view, tag, fb).mean(axis=2)
        got = edge_samples(view, edge, h)
        if got is None:
            return float("nan")
        pts, nx, ny = got
        vals = [cross_width(lum, cx, cy, nx, ny) for cx, cy in pts]
        vals = [v for v in vals if v is not None]
        return float(np.median(vals)) if vals else float("nan")

    widths = (1.0, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0)
    prm = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    was_cache = prm.GetInt("RenderCache", 3)
    for cache, name, fb in ((3, "bgfx", False), (2, "coin-gl", True)):
        prm.SetInt("RenderCache", cache)
        settle(view, 18)
        say("")
        say("RenderCache %d (%s)" % (cache, name))
        say("  width   with fill   no fill   the fill costs")
        rows = []
        for wd in widths:
            vo.LineWidth = wd
            vo.DisplayMode = "Flat Lines"
            settle(view)
            a = measure("%s-flat-%g" % (name, wd), fb)
            vo.DisplayMode = "Wireframe"
            settle(view)
            b = measure("%s-wire-%g" % (name, wd), fb)
            rows.append((wd, a, b))
            say("  %5.1f   %8.2f  %8.2f   %8.2f" % (wd, a, b, b - a))
        one = [r for r in rows if r[0] == 1.0]
        if not one or not (0.4 < one[0][2] < 2.6):
            say("  !! HARNESS BROKEN on this leg: a 1px line measured %s."
                % ("nothing" if not one else "%.2f" % one[0][2]))
            say("     Ignore this leg.")

    prm.SetInt("RenderCache", was_cache)
    vo.DisplayMode = was_mode
    vo.LineWidth = was_width
    settle(view, 6)

    say("")
    say("PASS if 'the fill costs' is ~0 at every width on the bgfx leg.")
    say("coin-gl is the untouched reference; the same artifact lives")
    say("there and this change does not reach it.")

    with open(RESULT, "w") as fh:
        fh.write("\n".join(LOG) + "\n")


try:
    main()
except Exception:
    import traceback
    tb = traceback.format_exc()
    FreeCAD.Console.PrintError("[lv] FAILED\n%s\n" % tb)
    try:
        if not os.path.isdir(OUT):
            os.makedirs(OUT)
        with open(RESULT, "w") as fh:
            fh.write("\n".join(LOG) + "\nFAILED\n" + tb)
    except Exception:
        pass
