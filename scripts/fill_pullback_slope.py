"""How far does a thick edge's polygon offset sink its own fill, against
the depth gradient of the face it is drawn on?

    FreeCAD scripts/fill_pullback_slope.py        (or run it over MCP)

Companion to fill_pullback_neighbor.py.  That probe stacked a plate on a
box and swept the CAMERA toward edge-on, which confounds two things: a
vertical gap between two horizontal faces separates them in depth by
only `gap * cos(angle to z)`, which collapses as the camera flattens,
and the face thins to a few pixels the thick edges then cover.

This probe removes both.  Two plates are separated ALONG THE VIEW AXIS,
which under an orthographic camera moves nothing on screen -- so the gap
IS a depth, with no projection factor, and both faces stay fully
resolved.  The gradient is swept by TILTING the plates with the camera
fixed, so the face is still hundreds of pixels tall at gradients well
past the ceiling.

The green plate (thick edges, reach 6.5) sits `gap` NEARER the camera
than the red one (width 1, reach 1 -- the pre-fix behaviour).  Red
showing through green means green's fill sank more than gap; bisecting
the gap measures that differential.

! THE TWO PLATES MUST NOT BE THE SAME SHAPE.  Two identical shapes
measure 0.0005 model units where a 20x20x3 neighbour measures 0.25 --
the effect disappears entirely.  Something downstream merges draws of
identical shapes (the draw count drops by two), and a merged pair shares
one fill offset, so an identical-shape control silently measures zero
and reads as "no bug here".  The red plate is deliberately wider.
"""
import os
import time

import numpy as np
from PIL import Image

import FreeCAD
import FreeCADGui

OUT = os.environ.get("FP_OUT", os.path.join(os.path.expanduser("~"),
                                            "fp-probe"))
RESULT = os.path.join(OUT, "slope.txt")
LOG = []

WIDTH = 12.0              # reach 6.5, against the red plate's 1
# Tilt 45 is face-on to this camera; the gradient is tan(tilt - 45)
# times the camera's own ry/rz, so the ceiling is only reached past
# about 111 degrees.
TILTS = (45.0, 65.0, 80.0, 95.0, 105.0, 111.0, 118.0, 125.0, 131.0)
BISECT = 11
SIDE, THICK = 20.0, 2.0
RED_SIDE = 26.0           # different shape on purpose -- see the header
GAP_MAX = 4.0
ELEV = 1.0                # camera looks along (0, 1, -ELEV)
REACH = 0.5 * WIDTH + 0.5


def say(m):
    LOG.append(m)
    FreeCAD.Console.PrintMessage("[fs] %s\n" % m)


def get_view():
    for v in FreeCADGui.ActiveDocument.mdiViewsOfType("Gui::View3DInventor"):
        if hasattr(v, "redraw") and hasattr(v, "getPointOnScreen"):
            return v
    return None


def settle(view, rounds=6):
    for _ in range(rounds):
        time.sleep(0.04)
        for _ in range(3):
            view.redraw()
            FreeCADGui.updateGui()


def grab(view, name):
    p = os.path.join(OUT, name + ".png")
    view.saveRenderDump(p, source="renderer", metadata=False)
    view.saveRenderDump(p, source="renderer", metadata=False)
    return np.asarray(Image.open(p).convert("RGB")).astype(np.float64)


def screen(view, pts):
    return [view.getPointOnScreen(p.x, p.y, p.z) for p in pts]


def counts(img, sp, h, n=20, inset=0.28):
    ih, iw = img.shape[:2]
    green = red = 0
    for i in range(n):
        u = inset + (1.0 - 2 * inset) * (i + 0.5) / n
        for j in range(n):
            v = inset + (1.0 - 2 * inset) * (j + 0.5) / n
            x = ((1 - u) * (1 - v) * sp[0][0] + u * (1 - v) * sp[1][0]
                 + u * v * sp[2][0] + (1 - u) * v * sp[3][0])
            y = ((1 - u) * (1 - v) * sp[0][1] + u * (1 - v) * sp[1][1]
                 + u * v * sp[2][1] + (1 - u) * v * sp[3][1])
            px, py = int(round(x)), int(round((h - 1) - y))
            if px < 0 or py < 0 or px >= iw or py >= ih:
                continue
            r, g, b = img[py, px]
            if g > r + 12 and g > b + 12:
                green += 1
            elif r > g + 12 and r > b + 12:
                red += 1
    return green, red


def main():
    if not os.path.isdir(OUT):
        os.makedirs(OUT)
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document") \
        .SetBool("AutoSaveEnabled", False)
    for name in list(FreeCAD.listDocuments()):
        if name.startswith("FillSlope"):
            FreeCAD.closeDocument(name)
    doc = FreeCAD.newDocument("FillSlope")
    red = doc.addObject("Part::Box", "Red")
    red.Length = red.Width = RED_SIDE
    red.Height = THICK
    green = doc.addObject("Part::Box", "Green")
    green.Length = green.Width = SIDE
    green.Height = THICK
    doc.recompute()
    red.ViewObject.ShapeColor = (0.85, 0.10, 0.10)
    red.ViewObject.DisplayMode = "Flat Lines"
    red.ViewObject.LineWidth = 1.0
    green.ViewObject.ShapeColor = (0.10, 0.85, 0.10)
    green.ViewObject.DisplayMode = "Flat Lines"
    green.ViewObject.LineWidth = WIDTH

    view = get_view()
    if view is None:
        say("NO VIEW")
        return

    d = FreeCAD.Vector(0.0, 1.0, -ELEV)
    d.normalize()
    centre = FreeCAD.Vector(SIDE / 2.0, SIDE / 2.0, THICK / 2.0)
    redbase = FreeCAD.Vector((SIDE - RED_SIDE) / 2.0,
                             (SIDE - RED_SIDE) / 2.0, 0.0)

    def place(tilt, gap):
        rot = FreeCAD.Rotation(FreeCAD.Vector(1, 0, 0), tilt)
        red.Placement = FreeCAD.Placement(redbase, rot, centre - redbase)
        green.Placement = FreeCAD.Placement(d * (-gap), rot, centre)
        doc.recompute()

    def top(tilt, gap):
        rot = FreeCAD.Rotation(FreeCAD.Vector(1, 0, 0), tilt)
        return [rot.multVec(FreeCAD.Vector(x, y, THICK) - centre) + centre
                + d * (-gap)
                for (x, y) in ((0, 0), (SIDE, 0), (SIDE, SIDE), (0, SIDE))]

    def sunk(tilt, gap, tag):
        place(tilt, gap)
        settle(view)
        _, h = view.getSize()
        sp = screen(view, top(tilt, gap))
        g, r = counts(grab(view, tag), sp, h)
        return r > g

    rows = []
    for tilt in TILTS:
        place(tilt, 0.0)
        view.setViewDirection((0.0, 1.0, -ELEV))
        view.fitAll()
        settle(view)
        cam = view.getCameraNode()
        near = cam.nearDistance.getValue()
        far = cam.farDistance.getValue()
        w, h = view.getSize()
        ry = cam.height.getValue() / 2.0
        rz = (far - near) / 2.0

        # The gradient fc_mesh_vs.sh forms for the plate's top face,
        # from the same view-space plane it uses.
        rot = FreeCAD.Rotation(FreeCAD.Vector(1, 0, 0), tilt)
        n = rot.multVec(FreeCAD.Vector(0, 0, 1))
        zaxis = -d
        up = FreeCAD.Vector(0, 0, 1)
        up = up - zaxis * (up * zaxis)
        up.normalize()
        gy = abs((n * up) * ry) / max(abs((n * zaxis) * rz), 1e-9)
        pred = (REACH - 1.0) * min(gy, 4.0) * 2.0 / h

        sp = screen(view, top(tilt, 0.0))
        span = max(abs(sp[0][1] - sp[3][1]), abs(sp[1][1] - sp[2][1]))

        tag = "fs_t%g" % tilt
        if not sunk(tilt, 0.0, tag + "_zero"):
            say("tilt %5.1f  face %4.0f px  -- green wins even coincident"
                % (tilt, span))
            continue
        if sunk(tilt, GAP_MAX, tag + "_hi"):
            say("tilt %5.1f  face %4.0f px  -- still lost at gap %g"
                % (tilt, span, GAP_MAX))
            continue
        lo, hi = 0.0, GAP_MAX
        for _ in range(BISECT):
            mid = 0.5 * (lo + hi)
            if sunk(tilt, mid, tag + "_b"):
                lo = mid
            else:
                hi = mid
        sink = 0.5 * (lo + hi)
        ndc = sink * 2.0 / (far - near)
        rows.append((tilt, span, gy, pred, sink, ndc))
        say("tilt %5.1f  face %4.0f px  gradient %7.3f  sink %7.4f mu"
            " = %8.5f NDC  (predicted %8.5f)"
            % (tilt, span, gy, sink, ndc, pred))

    say("")
    say("tilt  face px  gradient  predicted NDC  measured NDC  ratio")
    for (tilt, span, gy, pred, sink, ndc) in rows:
        say("%5.1f %8.0f %9.3f %14.5f %13.5f %6.2f"
            % (tilt, span, gy, pred, ndc, ndc / max(pred, 1e-9)))
    say("")
    say("kPolyOffsetMaxSlope = 4.0 caps the gradient; a 12px line is"
        " reach %.1f against the neighbour's 1." % REACH)

    with open(RESULT, "w") as f:
        f.write("\n".join(LOG) + "\n")
    say("wrote %s" % RESULT)


main()
