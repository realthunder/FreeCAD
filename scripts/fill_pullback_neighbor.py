"""How deep does a thick edge's polygon offset sink its own fill?

    FreeCAD scripts/fill_pullback_neighbor.py      (or run it over MCP)

THE RISK.  34461d03b7 raised a fill's polygon-offset `factor` from 1 to
the REACH of the decoration drawn over it -- half a line width plus the
analytic feather -- so a thick edge is no longer half-eaten by the face
it straddles.  The slope term is `factor * gradient`, the gradient is
clamped at kPolyOffsetMaxSlope = 4, and the commit left one case
unmeasured: where two solids touch and one carries very thick edges, its
fill could sink far enough to lose to its neighbour.

WHAT IS MEASURED.  Not "is it broken" but "how deep does it sink", in
model units, so the answer is a number even when it is zero.  A 20x20x10
box (red) carries no decoration; a 12x12 plate (green) sits on it with
its own top surface a controlled GAP above the box's top face, and
carries the edges.  BISECT the gap: the gap at which the box stops
showing through the plate IS the sink.  Line width 1 is the control --
the pre-fix reach, the same factor the box itself gets, so it should
sink nothing.

Then the same measurement over three configurations, because a
polygon-offset slope is expressed in NDC and the question is what it
means in a model:

  near-edge-on   the camera a few degrees above the plate, where the
                 gradient is past the ceiling -- the only condition
                 where the push is large, and the one the clamp is for
  ten times      the same scene and camera scaled 10x, to show the sink
                 is a fixed slice of the DEPTH RANGE, not a length
  face-on        the camera looking down the plate's normal, where the
                 gradient is ~0 and nothing should move

! THE NUMBERS HERE ARE VERTICAL GAPS, NOT DEPTHS.  Two horizontal faces
a gap apart are separated in depth by `gap * cos(angle from the view
axis to z)`, which collapses as the camera flattens -- so the grazing
rows understate the sink by that cosine, and at the flattest cameras
the plate's top face thins to a few pixels the thick edges then cover,
which understates it again.  This probe answers "does the symptom show
up in a scene someone would actually build".  For the law itself use
scripts/fill_pullback_slope.py, which separates the two solids ALONG
the view axis instead, keeps the face fully resolved, and reproduces
the shader's own arithmetic at ratio 1.00.

A control built out of two identical shapes used to measure 0.0005
model units where a 20x20x3 neighbour gave 0.25 -- the effect vanished.
Identical geometry hashes together, so the bgfx backend batched the two
fills into one instanced submit, and an instanced submit binds ONE
polygon offset: its prototype's.  buildInstanceGroups now keys on the
resolved offset factor, so the two land in different batches.  The
plate and the box here still differ in shape; the identical-shape case
is exercised deliberately by fill_pullback_slope.py's FP_RED_SIDE=20.
"""
import os
import time

import numpy as np
from PIL import Image

import FreeCAD
import FreeCADGui

OUT = os.environ.get("FP_OUT", os.path.join(os.path.expanduser("~"),
                                            "fp-probe"))
RESULT = os.path.join(OUT, "result.txt")
LOG = []

WIDTHS = (1.0, 2.0, 4.0, 6.0, 8.0, 12.0)
BISECT = 9
PLATE_FRAC = 0.6          # plate side as a fraction of the box side
THICK_FRAC = 0.2

# (label, box side, box height, camera elevation, gap ceiling)
CASES = (
    ("near-edge-on", 20.0, 10.0, 0.12, 2.0),
    ("ten times", 200.0, 100.0, 0.12, 20.0),
    ("face-on", 20.0, 10.0, 8.0, 2.0),
)


def say(m):
    LOG.append(m)
    FreeCAD.Console.PrintMessage("[fp] %s\n" % m)


def get_view():
    # ! ActiveView can be a bare Gui::MDIView with no redraw(); ask by
    # type and check the API is really there.
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
    # ! The dump is served by the NEXT staged frame, so the first one
    # back can still be the previous state.  Burn one, keep the second.
    p = os.path.join(OUT, name + ".png")
    view.saveRenderDump(p, source="renderer", metadata=False)
    view.saveRenderDump(p, source="renderer", metadata=False)
    return np.asarray(Image.open(p).convert("RGB")).astype(np.float64)


def quad_samples(view, pts, n=24, inset=0.15):
    """Screen positions on a grid inside the projected quad."""
    sp = [view.getPointOnScreen(p[0], p[1], p[2]) for p in pts]
    xs = np.array([s[0] for s in sp], dtype=np.float64)
    ys = np.array([s[1] for s in sp], dtype=np.float64)
    out = []
    for i in range(n):
        u = inset + (1.0 - 2 * inset) * (i + 0.5) / n
        for j in range(n):
            v = inset + (1.0 - 2 * inset) * (j + 0.5) / n
            x = ((1 - u) * (1 - v) * xs[0] + u * (1 - v) * xs[1]
                 + u * v * xs[2] + (1 - u) * v * xs[3])
            y = ((1 - u) * (1 - v) * ys[0] + u * (1 - v) * ys[1]
                 + u * v * ys[2] + (1 - u) * v * ys[3])
            out.append((x, y))
    return out


def red_share(img, samples, h):
    """Share of the samples the RED neighbour owns, over the two colors."""
    ih, iw = img.shape[:2]
    green = red = 0
    for (x, sy) in samples:
        # getPointOnScreen is GL-style (origin bottom-left); the PNG is
        # top-down.  h is the viewer height the projection used.
        px = int(round(x))
        py = int(round((h - 1) - sy))
        if px < 0 or py < 0 or px >= iw or py >= ih:
            continue
        r, g, b = img[py, px]
        if g > r + 12 and g > b + 12:
            green += 1
        elif r > g + 12 and r > b + 12:
            red += 1
    return red / float(max(1, red + green))


def predicted(view, elevation):
    """The push the shader's own arithmetic says, per unit of `factor`.

    Returns (gradient, ndc_per_factor, mm_per_factor) for the plate's
    top face, or None for a perspective camera.
    """
    cam = view.getCameraNode()
    if not hasattr(cam, "height"):
        return None
    d = FreeCAD.Vector(0.0, 1.0, -elevation)
    d.normalize()
    n = FreeCAD.Vector(0.0, 0.0, 1.0)
    zaxis = -d
    up = n - zaxis * (n * zaxis)
    if up.Length < 1e-9:
        return None
    up.normalize()
    near = cam.nearDistance.getValue()
    far = cam.farDistance.getValue()
    ry = cam.height.getValue() / 2.0
    rz = (far - near) / 2.0
    grad = abs((n * up) * ry) / max(abs((n * zaxis) * rz), 1e-9)
    _, h = view.getSize()
    ndc = min(grad, 4.0) * 2.0 / h
    return grad, ndc, ndc / (2.0 / (far - near))


def build(doc, side, height):
    for o in list(doc.Objects):
        doc.removeObject(o.Name)
    box = doc.addObject("Part::Box", "Base")
    box.Length, box.Width, box.Height = side, side, height
    plate = doc.addObject("Part::Box", "Plate")
    plate.Length = plate.Width = side * PLATE_FRAC
    plate.Height = side * THICK_FRAC
    doc.recompute()
    box.ViewObject.ShapeColor = (0.85, 0.10, 0.10)
    box.ViewObject.DisplayMode = "Flat Lines"
    box.ViewObject.LineWidth = 1.0
    plate.ViewObject.ShapeColor = (0.10, 0.85, 0.10)
    plate.ViewObject.DisplayMode = "Flat Lines"
    return box, plate


def place(doc, plate, side, height, gap):
    off = side * (1.0 - PLATE_FRAC) / 2.0
    plate.Placement = FreeCAD.Placement(
        FreeCAD.Vector(off, off, height + gap - side * THICK_FRAC),
        FreeCAD.Rotation())
    doc.recompute()


def top_face(side, height, gap):
    off = side * (1.0 - PLATE_FRAC) / 2.0
    p = side * PLATE_FRAC
    z = height + gap
    return [(off, off, z), (off + p, off, z),
            (off + p, off + p, z), (off, off + p, z)]


def sunk(view, doc, plate, side, height, gap, tag):
    """Does the red neighbour win the plate's own top face at this gap?"""
    place(doc, plate, side, height, gap)
    settle(view)
    _, h = view.getSize()
    img = grab(view, tag)
    return red_share(img, quad_samples(view, top_face(side, height, gap)),
                     h) > 0.5


def main():
    if not os.path.isdir(OUT):
        os.makedirs(OUT)
    # No recovery data from a scratch document: it would put a modal
    # over the very window under test on the next start.
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document") \
        .SetBool("AutoSaveEnabled", False)
    for name in list(FreeCAD.listDocuments()):
        if name.startswith("FillPullback"):
            FreeCAD.closeDocument(name)
    doc = FreeCAD.newDocument("FillPullback")

    view = get_view()
    if view is None:
        say("NO VIEW")
        return

    table = {}
    for (label, side, height, elev, ceiling) in CASES:
        box, plate = build(doc, side, height)
        place(doc, plate, side, height, ceiling)
        view.setViewDirection((0.0, 1.0, -elev))
        view.fitAll()
        settle(view)
        pred = predicted(view, elev)
        cam = view.getCameraNode()
        w, h = view.getSize()
        say("--- %s: box %gx%gx%g, elevation %g, viewport %dx%d"
            % (label, side, side, height, elev, w, h))
        say("    camera near %.3f far %.3f height %.3f"
            % (cam.nearDistance.getValue(), cam.farDistance.getValue(),
               cam.height.getValue()))
        if pred:
            say("    plate-top gradient %.2f (ceiling 4.0 %s); predicted"
                " push %.5f NDC = %.4f model units per unit of factor"
                % (pred[0], "engages" if pred[0] > 4.0 else "does not",
                   pred[1], pred[2]))
        for lw in WIDTHS:
            plate.ViewObject.LineWidth = lw
            settle(view)
            tag = "fp_%s_w%g" % (label.replace(" ", ""), lw)
            if not sunk(view, doc, plate, side, height, 0.0, tag + "_zero"):
                # Nothing lost even with the faces coplanar.
                table[(label, lw)] = 0.0
                say("    width %4.1f  sink 0 (wins even coplanar)" % lw)
                continue
            if sunk(view, doc, plate, side, height, ceiling, tag + "_hi"):
                table[(label, lw)] = float("inf")
                say("    width %4.1f  sink > %g (still lost at the ceiling)"
                    % (lw, ceiling))
                continue
            lo, hi = 0.0, ceiling
            for _ in range(BISECT):
                mid = 0.5 * (lo + hi)
                if sunk(view, doc, plate, side, height, mid, tag + "_b"):
                    lo = mid
                else:
                    hi = mid
            table[(label, lw)] = 0.5 * (lo + hi)
            say("    width %4.1f  sink %.4f model units  (factor %.1f -> "
                "%.4f per unit)"
                % (lw, table[(label, lw)], 0.5 * lw + 0.5,
                   table[(label, lw)] / max(1e-9, 0.5 * lw + 0.5 - 1.0)))

    say("")
    say("SINK of the thick-edged fill, in model units, by line width")
    say("case            " + "".join(" w=%-5.0f" % lw for lw in WIDTHS))
    for (label, side, height, elev, ceiling) in CASES:
        say("%-15s " % label
            + "".join(" %6.3f" % table[(label, lw)] for lw in WIDTHS))

    with open(RESULT, "w") as f:
        f.write("\n".join(LOG) + "\n")
    say("wrote %s" % RESULT)


main()
