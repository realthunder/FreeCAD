"""The Zebra matcap: stripes that show how two faces meet.

A zebra view shades a surface as a mirror in a room of parallel light
strips. A stripe is a line of equal reflection, so what the stripes do
at an edge says how the two faces meet there: they STEP where the faces
meet at an angle (a crease), they meet but kink where the faces are
tangent and the curvature jumps, and they run through where the
curvature is continuous too.

The preset is the fifth of Render_MatcapPreset; Render_MatcapStripes is
how many dark/light pairs the room has between its two poles.

Three bodies, each framed alone, the backend's own framebuffer read
(saveRenderDump, source "renderer"):

  sphere    the number of stripes follows the setting: the pixel column
            through the centre crosses twice as many at 16 as at 8.
  tangent   a cylinder running tangentially into a rounded shoulder.
            Pairs of pixels a few pixels to either side of the seam
            carry the same tone: the stripes cross it unbroken.
  creased   the same body with the shoulder left sharp: about half of
            the pairs disagree, the stripes of the two faces have
            nothing to do with each other.

Scored against the tree before the preset existed: the first check
fails (there is no such preset; index 4 shaded as Pearl) and nothing
else can be measured.
"""
import math
import os
import time
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore, QtGui

V = FreeCAD.Vector
OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "MatcapZebra"
# The revolved body's profile, in (radius, height): a cylinder of radius
# R up to H, then a cone to radius TOP_R at TOP_Z. The shoulder between
# them is rounded with FILLET or left sharp.
R, H, TOP_R, TOP_Z, FILLET = 30.0, 30.0, 10.0, 55.0, 16.0
SPHERE_R = 25.0
# Pixels to either side of the seam a pair is read at.
OFFSET_PX = 3.0
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.01)


def shoulder():
    """The profile's corner and what rounds it, in (radius, height)."""
    ux, uz = TOP_R - R, TOP_Z - H
    n = math.hypot(ux, uz)
    ux, uz = ux / n, uz / n
    turn = math.acos(uz)                 # between straight up and the cone
    reach = FILLET * math.tan(turn / 2.0)
    t1 = (R, H - reach)
    t2 = (R + reach * ux, H + reach * uz)
    centre = (R - FILLET, H - reach)
    return (ux, uz), turn, t1, t2, centre


def body(tangent):
    (ux, uz), turn, t1, t2, centre = shoulder()

    def p(rz):
        return V(rz[0], 0.0, rz[1])

    edges = [Part.LineSegment(V(0, 0, 0), V(R, 0, 0)).toShape()]
    if tangent:
        mid = (centre[0] + FILLET * math.cos(turn / 2.0),
               centre[1] + FILLET * math.sin(turn / 2.0))
        edges.append(Part.LineSegment(V(R, 0, 0), p(t1)).toShape())
        edges.append(Part.Arc(p(t1), p(mid), p(t2)).toShape())
        edges.append(Part.LineSegment(p(t2), V(TOP_R, 0, TOP_Z)).toShape())
    else:
        edges.append(Part.LineSegment(V(R, 0, 0), V(R, 0, H)).toShape())
        edges.append(Part.LineSegment(V(R, 0, H), V(TOP_R, 0, TOP_Z)).toShape())
    edges.append(Part.LineSegment(V(TOP_R, 0, TOP_Z), V(0, 0, TOP_Z)).toShape())
    edges.append(Part.LineSegment(V(0, 0, TOP_Z), V(0, 0, 0)).toShape())
    face = Part.Face(Part.Wire(edges))
    return face.revolve(V(0, 0, 0), V(0, 0, 1), 360)


def seam_pair(tangent, phi, d):
    """Two points d to either side of the seam, at the angle phi."""
    (ux, uz), turn, t1, t2, centre = shoulder()
    if tangent:
        below = (R, t1[1] - d)
        a = d / FILLET
        above = (centre[0] + FILLET * math.cos(a),
                 centre[1] + FILLET * math.sin(a))
    else:
        below = (R, H - d)
        above = (R + d * ux, H + d * uz)
    c, s = math.cos(phi), math.sin(phi)
    return (V(below[0] * c, below[0] * s, below[1]),
            V(above[0] * c, above[0] * s, above[1]))


def run():
    try:
        render = FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/View/Render")
        # One flat material, nothing else in the frame: the tones read
        # are then the stripes' own.
        render.SetBool("Matcap", True)
        render.SetFloat("MatcapTint", 0.0)
        for off in ("AO", "Cavity", "Bloom", "Light", "Shadow", "PBR",
                    "Volumetric"):
            render.SetBool(off, False)

        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        shapes = {"Sphere": Part.makeSphere(SPHERE_R),
                  "Tangent": body(True), "Creased": body(False)}
        for name, shape in shapes.items():
            obj = doc.addObject("Part::Feature", name)
            obj.Shape = shape
            vobj = obj.ViewObject
            # No edge lines over the faces, and a mesh fine enough that
            # the stripes are the surface's and not the facets'.
            vobj.DisplayMode = "Shaded"
            vobj.Deviation = 0.02
            vobj.AngularDeflection = 4.0
        doc.recompute()
        view = FreeCADGui.activeDocument().activeView()
        view.setAnimationEnabled(False)
        state["view"] = view
        QtCore.QTimer.singleShot(2000, measure)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def frame(name, stripes):
    """Frame one body alone from the front and read the backend's frame."""
    view = state["view"]
    gdoc = FreeCADGui.getDocument(DOC)
    for other in ("Sphere", "Tangent", "Creased"):
        gdoc.getObject(other).Visibility = other == name
    view.Render_MatcapStripes = stripes
    view.viewFront()
    view.fitAll()
    # A change of visibility reaches the backend's scene a few passes of
    # the event loop later: a frame read at once shows the body before.
    view.redraw()
    settle(0.5)
    view.waitFrameComplete()
    path = os.path.join(OUT, "%s-%d.png" % (name.lower(), stripes))
    view.saveRenderDump(path, "renderer")
    return QtGui.QImage(path)


def tone(img, point):
    view = state["view"]
    vx, vy = view.getPointOnViewport(point)
    c = QtGui.QColor(img.pixel(int(vx), int(img.height() - 1 - vy)))
    return (c.red() + c.green() + c.blue()) / 3.0


def crossings(img):
    """Dark/light changes down the pixel column through the sphere."""
    view = state["view"]
    x, top = view.getPointOnViewport(V(0, 0, SPHERE_R * 0.99))
    _, bottom = view.getPointOnViewport(V(0, 0, -SPHERE_R * 0.99))
    h = img.height()
    tones = []
    for vy in range(int(bottom), int(top) + 1):
        c = QtGui.QColor(img.pixel(int(x), h - 1 - vy))
        tones.append((c.red() + c.green() + c.blue()) / 3.0)
    lo, hi = min(tones), max(tones)
    if hi - lo < 60:
        return 0, lo, hi
    # With hysteresis: a stripe too fine to resolve fades to grey and
    # must not count as a run of changes.
    dark, light = lo + 0.3 * (hi - lo), lo + 0.7 * (hi - lo)
    count, last = 0, None
    for t in tones:
        now = False if t < dark else True if t > light else last
        if last is not None and now != last:
            count += 1
        last = now
    return count, lo, hi


def seam(name, tangent, stripes):
    """Share of pixel pairs across the seam that differ in tone."""
    view = state["view"]
    img = frame(name, stripes)
    a = view.getPointOnViewport(V(0, -R, 0))
    b = view.getPointOnViewport(V(0, -R, 10))
    d = OFFSET_PX / (math.hypot(b[0] - a[0], b[1] - a[1]) / 10.0)
    pairs = []
    # The half of the seam that faces the front view, short of the
    # silhouette where a pixel covers a wide turn of the surface.
    for i in range(121):
        phi = math.radians(-150.0 + i)
        below, above = seam_pair(tangent, phi, d)
        pairs.append((tone(img, below), tone(img, above)))
    flat = [t for pair in pairs for t in pair]
    lo, hi = min(flat), max(flat)
    if hi - lo < 60:
        return None, "tones %d..%d: no stripes at the seam" % (lo, hi)
    dark, light = lo + 0.3 * (hi - lo), lo + 0.7 * (hi - lo)
    # A pair with a pixel on a stripe's edge says nothing either way.
    sure = [(p, q) for p, q in pairs
            if (p < dark or p > light) and (q < dark or q > light)]
    differ = sum(1 for p, q in sure if (p > light) != (q > light))
    share = differ / float(len(sure)) if sure else 0.0
    return share, "%d of %d pairs differ (%.0f%%), %.2f mm to either side" % (
        differ, len(sure), 100.0 * share, d)


def measure():
    try:
        view = state["view"]
        try:
            view.Render_MatcapPreset = "Zebra"
            preset, detail = True, ""
        except Exception as e:
            preset, detail = False, str(e)
        if not check("the view has a Zebra matcap preset", preset, detail):
            finish()
            return
        try:
            view.Render_MatcapStripes = 8
            stripes, detail = True, ""
        except Exception as e:
            stripes, detail = False, str(e)
        if not check("the view has a stripe count", stripes, detail):
            finish()
            return
        try:
            view.saveRenderDump(os.path.join(OUT, "probe.png"), "renderer")
            active, detail = True, ""
        except Exception as e:
            active, detail = False, str(e)
        if not check("the bgfx renderer draws the view", active, detail):
            finish()
            return

        n8, lo, hi = crossings(frame("Sphere", 8))
        check("the sphere is striped dark and light", hi - lo > 150,
              "tones %d..%d" % (lo, hi))
        n16, _, _ = crossings(frame("Sphere", 16))
        # Pole to pole and back again on each half of the column: four
        # changes per stripe, less the few the silhouette swallows.
        check("8 stripes cross the sphere's centre column", 24 <= n8 <= 32,
              "%d changes" % n8)
        check("16 stripes cross it twice as often", 1.6 * n8 <= n16 <= 2.2 * n8,
              "%d changes against %d" % (n16, n8))

        share, detail = seam("Tangent", True, 4)
        check("stripes cross a tangent seam unbroken",
              share is not None and share <= 0.15, detail)
        share, detail = seam("Creased", False, 4)
        check("stripes break at a creased seam",
              share is not None and share >= 0.35, detail)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
