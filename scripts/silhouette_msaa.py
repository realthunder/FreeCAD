"""How ragged is a curved silhouette now that MSAA defaults off?

    FreeCAD scripts/silhouette_msaa.py            (or run it over MCP)

12ad499101 turned multisampling off by default, on the grounds that
lines and points resolve their own coverage analytically now, and named
the price: "the TRIANGLE silhouette, which has no analytic coverage of
its own. In Flat Lines and Wireframe an edge is drawn along every
silhouette and hides it; in Shaded, with no edges, a curved silhouette
is bare and will show stair-stepping."

Two things that claim does not account for, and this probe measures
both.

! A SPHERE'S SILHOUETTE IS A LIMB, NOT AN EDGE. On a polyhedron every
silhouette is a topological edge, so Flat Lines really does draw a line
along it. Nothing lies along a sphere's or a cylinder's limb, so Flat
Lines has nothing to draw there and is no better off than Shaded.

! AND A PARKED VIEW ANTIALIASES ITSELF. The backend accumulates jittered
samples whenever nothing moves (Render_TemporalAccum, converging over
Render_TemporalAccumSamples = 32), so a settled frame is smooth whatever
MSAA is set to -- measured climbing 0 -> 32 over about 2.5 seconds after
a camera change. Any probe that settles the view before capturing is
therefore measuring the accumulation, not the rasterizer, and will
report that MSAA off costs nothing. The cost is real but lives in the
frames drawn WHILE THE CAMERA MOVES, so the accumulation is switched off
here to see them.

THE SCENE. A saturated red backdrop plate fills the frame behind a
neutral grey sphere, and the measured signal is the backdrop's chroma,
not the sphere's. Measuring the object's own chroma does not work: its
shading falls off toward the limb over many pixels, so a half-crossing
lands inside a shading ramp and every configuration reads as smooth.
Measuring luminance against the viewer's background does not work
either -- the detector locks onto the soft grey ground shadow, which
departs from the left margin near x=180 while the sphere starts near
x=440. A flat backdrop facing the camera is uniformly lit, so its chroma
holds constant right up to the limb and the crossing is pure geometric
coverage.

THE NUMBERS, per configuration:

  blended     share of rows whose limb pixel is a genuine partial
              coverage rather than a hard backdrop/object step -- what
              antialiasing IS. This is the discriminator; the residual
              is not, because at the sphere's widest point the limb is
              vertical and a cubic fits it whatever the sampling did.
  tread       longest run of rows sharing one integer limb column --
              the staircase step a user would see
"""
import os
import time

import numpy as np
from PIL import Image

import FreeCAD
import FreeCADGui

OUT = os.environ.get("SM_OUT", os.path.join(os.path.expanduser("~"),
                                            "sm-probe"))
RESULT = os.path.join(OUT, "result.txt")
LOG = []

NONE, MSAA4X = 0, 3
MODES = (("MSAA off", NONE), ("MSAA 4x", MSAA4X))
STYLES = ("Shaded", "Flat Lines")
ACCUM = ((False, "moving"), (True, "parked"))
BAND = 160


def say(m):
    LOG.append(m)
    FreeCAD.Console.PrintMessage("[sm] %s\n" % m)


def get_view():
    for v in FreeCADGui.ActiveDocument.mdiViewsOfType("Gui::View3DInventor"):
        if hasattr(v, "redraw") and hasattr(v, "getPointOnScreen"):
            return v
    return None


def settle(view, rounds=10):
    for _ in range(rounds):
        time.sleep(0.05)
        for _ in range(3):
            view.redraw()
            FreeCADGui.updateGui()


def grab(view, name):
    p = os.path.join(OUT, name + ".png")
    view.saveRenderDump(p, source="renderer", metadata=False)
    view.saveRenderDump(p, source="renderer", metadata=False)
    return np.asarray(Image.open(p).convert("RGB")).astype(np.float64)


def limb(chroma, y):
    """Sub-pixel column where the sphere's left limb cuts the backdrop.

    The row runs backdrop (high chroma) -> object (near zero), so this is
    the FALLING half-crossing.
    """
    row = chroma[y]
    back = float(np.median(row[20:60]))
    if back < 40.0:
        return None
    half = 0.5 * back
    coarse = None
    for x in range(60, len(row) - 12):
        if row[x] < half:
            coarse = x
            break
    if coarse is None or coarse < 66:
        return None
    if float(np.median(row[coarse + 3:coarse + 11])) > half:
        return None
    a, b = row[coarse - 1], row[coarse]
    if a <= b:
        return None
    frac = (a - half) / (a - b)
    # Blended when either straddling pixel sits well inside the two
    # levels rather than snapping to one of them.
    blended = any(0.12 * back < v < 0.88 * back for v in (a, b))
    return coarse - 1 + frac, coarse - 1, blended


def measure(img):
    chroma = img[:, :, 0] - img[:, :, 2]
    h = chroma.shape[0]
    y0 = h // 2 - BAND // 2
    rows, cols, blends = [], [], []
    for y in range(y0, y0 + BAND):
        got = limb(chroma, y)
        if got is None:
            continue
        sub, xi, blended = got
        rows.append(y)
        cols.append(sub)
        blends.append(1.0 if blended else 0.0)
    if len(rows) < BAND // 2:
        return None
    ints = np.floor(np.array(cols)).astype(int)
    tread = best = 1
    for i in range(1, len(ints)):
        tread = tread + 1 if ints[i] == ints[i - 1] else 1
        best = max(best, tread)
    return float(np.mean(blends)), best, len(rows)


def main():
    if not os.path.isdir(OUT):
        os.makedirs(OUT)
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document") \
        .SetBool("AutoSaveEnabled", False)
    hGrp = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    original = hGrp.GetInt("AntiAliasing", NONE)

    for name in list(FreeCAD.listDocuments()):
        if name.startswith("Silhouette"):
            FreeCAD.closeDocument(name)
    doc = FreeCAD.newDocument("Silhouette")
    back = doc.addObject("Part::Box", "Backdrop")
    back.Length, back.Width, back.Height = 400.0, 4.0, 400.0
    back.Placement = FreeCAD.Placement(
        FreeCAD.Vector(-200.0, 60.0, -200.0), FreeCAD.Rotation())
    sph = doc.addObject("Part::Sphere", "Sphere")
    sph.Radius = 24.0
    doc.recompute()
    back.ViewObject.ShapeColor = (0.95, 0.10, 0.10)
    back.ViewObject.DisplayMode = "Shaded"
    sph.ViewObject.ShapeColor = (0.55, 0.55, 0.55)

    view = get_view()
    if view is None:
        say("NO VIEW")
        return
    accum_was = getattr(view, "Render_TemporalAccum", None)
    say("Render_TemporalAccum was %s, AntiAliasing was %d"
        % (accum_was, original))

    table = {}
    try:
        for (accum, when) in ACCUM:
            view.Render_TemporalAccum = accum
            for (label, value) in MODES:
                hGrp.SetInt("AntiAliasing", value)
                settle(view, 14)
                for style in STYLES:
                    sph.ViewObject.DisplayMode = style
                    doc.recompute()
                    view.setViewDirection((0.0, 1.0, 0.0))
                    view.fitAll()
                    cam = view.getCameraNode()
                    if hasattr(cam, "height"):
                        cam.height.setValue(90.0)
                    settle(view)
                    stats = view.getRenderStats()
                    img = grab(view, "sm_%s_%s_%s"
                               % (when, style.replace(" ", ""),
                                  "off" if value == NONE else "4x"))
                    got = measure(img)
                    if got is None:
                        say("  %-7s %-8s %-10s  no limb found"
                            % (when, label, style))
                        continue
                    blended, tread, n = got
                    table[(when, label, style)] = (blended, tread)
                    say("  %-7s %-8s %-10s  blended %5.1f%%  tread %3d rows"
                        "  (temporalSamples %s, %d rows)"
                        % (when, label, style, 100.0 * blended, tread,
                           stats.get("temporalSamples"), n))
    finally:
        hGrp.SetInt("AntiAliasing", original)
        if accum_was is not None:
            view.Render_TemporalAccum = accum_was
        settle(view, 10)

    say("")
    say("share of limb rows carrying real partial coverage, and the"
        " longest staircase tread")
    say("%-8s %-11s %18s %18s" % ("when", "style", "MSAA off", "MSAA 4x"))
    for (accum, when) in ACCUM:
        for style in STYLES:
            a = table.get((when, "MSAA off", style))
            b = table.get((when, "MSAA 4x", style))
            if not a or not b:
                continue
            say("%-8s %-11s %18s %18s"
                % (when, style,
                   "%5.1f%%  tread %3d" % (100 * a[0], a[1]),
                   "%5.1f%%  tread %3d" % (100 * b[0], b[1])))

    with open(RESULT, "w") as f:
        f.write("\n".join(LOG) + "\n")
    say("wrote %s" % RESULT)


main()
