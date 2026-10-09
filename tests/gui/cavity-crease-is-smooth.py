"""The crease that cavity shading draws has no staircase.

docs/HandsOnQueue.md entry 64: "cavity option shows jagger regardless of
msaa. I think it should be fixed in its shader"; "both realistic and
classic. it's more obvious in Shaded mode (i.e. no edge rendering). more
obvious in slanted view".

Cavity shading (View/Render/Cavity, on by default) is one fullscreen
multiply that darkens the scene where the normal turns between two opposed
neighbours of a pixel. It reads a prepass that holds one normal and one
depth a pixel, after the multisampled scene is resolved, so a hard crease
came out as a band that every pixel was either in or out of: a staircase
that no multisampling reaches. The pass now reads each neighbour as the
average over its pixel -- the crease is found between a pixel and the one
next to it from the planes of their two faces, and the far face is weighed
in by the part of the pixel it covers (fs_fc_cavity.sc).

Measured on the multiplier itself: the same camera drawn with the pass on
and off, the one divided by the other in linear light, which leaves the
crease lines alone -- shading, material and the multisampled rim of the
faces divided out. Along a crease, for every column (or row, for a steep
one) of pixels: how much darkening it holds and where the middle of that
is, fractions of a pixel included. The crease of a straight edge is a
straight line on the screen, so the middle, column after column, lies on
one too; what is left after that line is taken out is the staircase. A band
that pixels are in or out of leaves a sawtooth a pixel high.

The reporter's configuration: the render engine, MSAA 4x, the Shaded draw
style (no edge lines over the creases).

Claims, for a ridge (a block's edge between two faces that are seen) and a
valley (where the block stands on a plate), three edges each at three
slants, under an orthographic and a perspective camera; for the rim of a
cylinder's top; and for a valley at a cavity radius of 3:
  - the crease is drawn: every column of the stretch holds some darkening;
  - it runs smoothly: its middle strays from the line under SMOOTH px (rms);
  - it is as heavy all the way: the darkening it holds across its width
    varies under EVEN of its mean (rms).
And two that the change must leave alone:
  - the middle of a flat face is not darkened at all;
  - a ball at a cavity radius of 4, where the pass shades broad curvature
    and no crease, is darkened as before (a note of the amount, compared by
    hand between two builds; and no pixel of it darker than its
    neighbours by more than a hair).
"""
import math
import os
import time
import traceback

import numpy as np

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = "User parameter:BaseApp/Preferences/View"
SMOOTH = float(os.environ.get("GT_CAVITY_SMOOTH", "0.10"))
EVEN = float(os.environ.get("GT_CAVITY_EVEN", "0.05"))
ROLL = 17.0


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.005)


def pixels(path):
    image = QtGui.QImage(path).convertToFormat(QtGui.QImage.Format_RGB888)
    w, h = image.width(), image.height()
    raw = np.frombuffer(image.constBits(), dtype=np.uint8, count=h * image.bytesPerLine())
    return raw.reshape(h, image.bytesPerLine())[:, :w * 3].reshape(h, w, 3).astype(np.float64)


def linear(a):
    c = a / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def setprop(view, name, kind, value):
    if not hasattr(view, name):
        view.addProperty(kind, name)
    setattr(view, name, value)


def darkening(view, tag):
    """What the pass took away, 0 to 1 a pixel: one picture with it, one
    without, divided."""
    pics = []
    for on in (True, False):
        setprop(view, "Render_Cavity", "App::PropertyBool", on)
        view.redraw()
        settle(0.6)
        path = os.path.join(OUT, "%s-%s.png" % (tag, "on" if on else "off"))
        view.saveRenderDump(path, metadata=False)
        pics.append(linear(pixels(path)))
    setprop(view, "Render_Cavity", "App::PropertyBool", True)
    with_it, without = pics
    ratio = np.where(without > 0.004, np.clip(with_it / np.maximum(without, 1e-6), 0.0, 1.0), 1.0)
    dark = 1.0 - ratio.mean(axis=2)
    QtGui.QImage(np.ascontiguousarray((255 - np.clip(dark * 255.0, 0, 255)).astype(np.uint8)),
                 dark.shape[1], dark.shape[0], dark.shape[1],
                 QtGui.QImage.Format_Grayscale8).save(os.path.join(OUT, tag + "-term.png"))
    return dark


def project(view, dark, point):
    x, y = view.getPointOnViewport(FreeCAD.Vector(*point))
    return float(x), float(dark.shape[0] - 1 - y)


def along(view, dark, tag, points, half, degree):
    """The crease along the polyline of 3D `points`: per column of pixels
    (per row when it is steeper than 45 degrees) the darkening within
    `half` pixels of the line, and the middle of it. Returns the slant, the
    amounts and what is left of the middles after a polynomial of `degree`
    is taken out."""
    path = [project(view, dark, p) for p in points]
    steep = abs(path[-1][1] - path[0][1]) > abs(path[-1][0] - path[0][0])
    if steep:
        path = [(y, x) for x, y in path]
        field = dark.T
    else:
        field = dark
    if path[0][0] > path[-1][0]:
        path.reverse()
    us = [p[0] for p in path]
    vs = [p[1] for p in path]
    slant = math.degrees(math.atan2(abs(vs[-1] - vs[0]), abs(us[-1] - us[0])))
    amount, middle, where = [], [], []
    for u in range(int(us[0]) + 1, int(us[-1])):
        v = int(round(float(np.interp(u, us, vs))))
        lo, hi = v - half, v + half + 1
        if lo < 0 or hi > field.shape[0] or u >= field.shape[1]:
            continue
        column = field[lo:hi, u]
        total = float(column.sum())
        amount.append(total)
        where.append(u)
        middle.append(lo + float((np.arange(len(column)) * column).sum()) / total
                      if total > 1e-3 else float("nan"))
    amount = np.array(amount)
    middle = np.array(middle)
    where = np.array(where, dtype=np.float64)
    if len(where):
        # the stretch, four times its size, for whoever looks
        a, b = int(where[0]), int(where[-1])
        v0 = int(max(min(vs) - half - 2, 0))
        v1 = int(min(max(vs) + half + 3, field.shape[0]))
        crop = np.ascontiguousarray(
            (255 - np.clip(field[v0:v1, a:b + 1] * 255.0, 0, 255)).astype(np.uint8))
        if steep:
            crop = np.ascontiguousarray(crop.T)
        QtGui.QImage(crop, crop.shape[1], crop.shape[0], crop.shape[1],
                     QtGui.QImage.Format_Grayscale8).scaled(
                         crop.shape[1] * 4, crop.shape[0] * 4, QtCore.Qt.IgnoreAspectRatio,
                         QtCore.Qt.FastTransformation).save(os.path.join(OUT, tag + "-x4.png"))
    good = ~np.isnan(middle)
    left = np.array([])
    if good.sum() > degree + 2:
        centred = where[good] - where[good].mean()
        fit = np.polyfit(centred, middle[good], degree)
        left = middle[good] - np.polyval(fit, centred)
        # A column cuts a slanted crease askew and holds more of it the
        # steeper the crease: taken across the crease instead, so that the
        # amounts along a curve, whose slant changes, can be compared.
        slope = np.polyval(np.polyder(fit), where - where[good].mean())
        amount = amount / np.sqrt(1.0 + slope ** 2)
    return slant, amount, left, int((~good).sum())


def crease(view, dark, tag, points, half=6, degree=1):
    slant, amount, left, empty = along(view, dark, tag, points, half, degree)
    if not check("%s: the crease is drawn in every column of the stretch" % tag,
                 len(amount) > 20 and empty == 0 and float(amount.min()) > 0.05,
                 "%d columns, %d with no darkening, the least %.3f; slant %.0f deg" % (
                     len(amount), empty, float(amount.min()) if len(amount) else 0.0, slant)):
        return
    stray = float(np.sqrt(np.mean(left ** 2)))
    uneven = float(amount.std() / amount.mean())
    check("%s: the crease runs smoothly" % tag, stray < SMOOTH,
          "its middle strays %.3f px rms from the line (at most %.2f px), over %d columns at a "
          "slant of %.0f deg" % (stray, float(np.abs(left).max()), len(left), slant))
    check("%s: the crease is as heavy all the way" % tag, uneven < EVEN,
          "across it the crease holds %.3f px of darkening, varying %.1f%% rms (%.3f to %.3f)" % (
              float(amount.mean()), 100.0 * uneven, float(amount.min()), float(amount.max())))


def part(a, b, lo=0.2, hi=0.8):
    """The middle stretch of the edge from a to b: its ends are corners,
    where other creases come in."""
    return [tuple(a[i] + t * (b[i] - a[i]) for i in range(3)) for t in (lo, hi)]


def look(view, camera):
    view.setCameraType(camera)
    # the isometric view, said outright (viewIsometric() turns the camera over
    # several frames), rolled about the line of sight so that no edge of the
    # block is upright or level
    isometric = FreeCAD.Rotation(0.424708, 0.17592, 0.339851, 0.820473)
    view.setCameraOrientation(isometric.multiply(
        FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), ROLL)))
    settle(1.0)
    view.fitAll()
    settle(2.0)


def shaded(doc):
    for obj in doc.Objects:
        obj.ViewObject.DisplayMode = "Shaded"
        obj.ViewObject.ShapeColor = (0.8, 0.8, 0.82)


RIDGES = (("top front", (0, 0, 40), (40, 0, 40)),
          ("top right", (40, 0, 40), (40, 40, 40)),
          ("front right", (40, 0, 0), (40, 0, 40)))
VALLEYS = (("front foot", (0, 0, 0), (40, 0, 0)),
           ("right foot", (40, 0, 0), (40, 40, 0)))


def block():
    doc = FreeCAD.newDocument("CavityBlock")
    box = doc.addObject("Part::Box", "Block")
    box.Length, box.Width, box.Height = 40, 40, 40
    plate = doc.addObject("Part::Box", "Plate")
    plate.Length, plate.Width, plate.Height = 100, 100, 10
    plate.Placement.Base = FreeCAD.Vector(-30, -30, -10)
    doc.recompute()
    shaded(doc)
    view = FreeCADGui.ActiveDocument.ActiveView
    for camera, short in (("Orthographic", "ortho"), ("Perspective", "persp")):
        look(view, camera)
        dark = darkening(view, "block-" + short)
        for name, a, b in RIDGES:
            crease(view, dark, "%s, ridge %s" % (short, name), part(a, b))
        for name, a, b in VALLEYS:
            crease(view, dark, "%s, valley %s" % (short, name), part(a, b))
        if short == "ortho":
            x, y = project(view, dark, (20, 20, 40))
            patch = dark[int(y) - 8:int(y) + 9, int(x) - 8:int(x) + 9]
            check("the middle of a flat face is not darkened", float(patch.max()) < 0.01,
                  "the most over 17 x 17 pixels: %.4f" % float(patch.max()))
    look(view, "Orthographic")
    setprop(view, "Render_CavityRadius", "App::PropertyFloat", 3.0)
    dark = darkening(view, "block-radius3")
    name, a, b = VALLEYS[0]
    crease(view, dark, "radius 3, valley %s" % name, part(a, b), half=10)
    setprop(view, "Render_CavityRadius", "App::PropertyFloat", 1.0)
    FreeCAD.closeDocument(doc.Name)


def rim():
    doc = FreeCAD.newDocument("CavityRim")
    cyl = doc.addObject("Part::Cylinder", "Cylinder")
    cyl.Radius, cyl.Height = 20, 15
    doc.recompute()
    shaded(doc)
    # facets a degree wide: the rim that is drawn is a polygon, and at the
    # default tessellation its corners are what a measurement of the rim's
    # smoothness finds
    cyl.ViewObject.Deviation = 0.01
    cyl.ViewObject.AngularDeflection = 1.0
    settle(1.0)
    view = FreeCADGui.ActiveDocument.ActiveView
    look(view, "Orthographic")
    dark = darkening(view, "rim")
    # the near half of the top's rim, where the side below it is seen (from
    # -135 to 45 degrees, nearest the eye at -45): two slanted stretches
    # either side of the nearest point, well short of the limbs, where the
    # side turns away and the crease with it
    for name, first, last in (("left", -108, -72), ("right", -18, 18)):
        arc = [(20 * math.cos(math.radians(d)), 20 * math.sin(math.radians(d)), 15)
               for d in range(first, last + 1, 2)]
        # a narrow band: the side below the rim has a faint shading of its own
        crease(view, dark, "rim of a cylinder's top, %s" % name, arc, half=3, degree=3)
    FreeCAD.closeDocument(doc.Name)


def ball():
    doc = FreeCAD.newDocument("CavityBall")
    sphere = doc.addObject("Part::Sphere", "Ball")
    sphere.Radius = 20
    doc.recompute()
    shaded(doc)
    view = FreeCADGui.ActiveDocument.ActiveView
    look(view, "Orthographic")
    setprop(view, "Render_CavityRadius", "App::PropertyFloat", 4.0)
    dark = darkening(view, "ball-radius4")
    x, y = project(view, dark, (0, 0, 0))
    rx, _ = project(view, dark, tuple(
        view.getCameraOrientation().multVec(FreeCAD.Vector(20, 0, 0))))
    r = 0.8 * abs(rx - x)
    yy, xx = np.mgrid[0:dark.shape[0], 0:dark.shape[1]]
    inside = (xx - x) ** 2 + (yy - y) ** 2 < r * r
    # a pixel against the mean of the four next to it: a facet pattern or a
    # stray dot stands out, the smooth shading of a ball does not
    around = (np.roll(dark, 1, 0) + np.roll(dark, -1, 0)
              + np.roll(dark, 1, 1) + np.roll(dark, -1, 1)) / 4.0
    rough = np.abs(dark - around)[inside]
    note("NOTE a ball at radius 4: darkened %.4f on average over %d pixels, %.4f at most; a "
         "pixel differs from the four next to it by %.5f on average, %.4f at most" % (
             float(dark[inside].mean()), int(inside.sum()), float(dark[inside].max()),
             float(rough.mean()), float(rough.max())))
    check("a ball at a cavity radius of 4 is shaded, and evenly",
          float(dark[inside].mean()) > 0.002 and float(rough.max()) < 0.03,
          "mean darkening %.4f; the most a pixel differs from the four next to it %.4f" % (
              float(dark[inside].mean()), float(rough.max())))
    setprop(view, "Render_CavityRadius", "App::PropertyFloat", 1.0)
    FreeCAD.closeDocument(doc.Name)


def run():
    try:
        prefs = FreeCAD.ParamGet(VIEW)
        prefs.SetInt("AntiAliasing", 3)
        render = prefs.GetGroup("Render")
        note("NOTE render type '%s', anti-aliasing %d, cavity %s, radius %.1f" % (
            render.GetString("Type", ""), prefs.GetInt("AntiAliasing", -1),
            render.GetBool("Cavity", True), render.GetFloat("CavityRadius", 1.0)))
        block()
        rim()
        ball()
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
