"""A line is drawn as heavy as the width it was asked for, fractions included,
at every angle, with multisampling and without.

docs/HandsOnQueue.md entry 52: "it makes me question whether previous line
shader effort is still needed", and the rule given for it: "if it does helps
to get the fractional width right, then it is still needed". The line shader
in question is the coverage fs_fc_line works out for itself
(docs/RenderEngine.md, "Lines"): the quad of a line is half a pixel wider on
each side and every pixel takes the share of itself the line covers. This is
the measurement behind the answer, kept as a test.

Black lines on a white ground, seen from the top: six widths from 1 to 3.5
pixels, each at six angles. For every line the ink across it is added up
over the middle of its length -- the share of each pixel that is line, read
back out of the picture's colour (the frame is blended in linear light and
encoded for the screen when View/Render/OutputTransform is 1) -- and divided
by that length: the width the line has to the eye.

Claims, with View/AntiAliasing 0 and then 3 (MSAA 4x):
  - every line weighs what was asked for, within 0.12 pixel without
    multisampling and within 0.16 with it;
  - the lines of one width weigh the same at all six angles, within the
    same.

Why multisampling gets the wider margin: a line comes out up to 0.13 pixel
LIGHTER there (measured 2026-10-10). The quad ends where the coverage
reaches nothing, so its outermost pixels are covered in part, and a
multisampled pixel keeps its colour for the samples the quad covers alone:
the little coverage it had is cut again. Half a pixel more quad on each
side would close it, at the price of that many more fragments; not done.

FC_BGFX_LINE_NO_COVERAGE in the environment draws the lines as plain quads,
the way they were before the coverage. The same numbers are noted then and
nothing is claimed: that leg is the comparison, not a state of the program.
Measured with it, 2026-10-10: without multisampling a line is up to 0.75
pixel from its width and one width differs by up to 1.29 pixel between two
angles; with MSAA 4x, 0.26 and 0.29.
"""
import math
import os
import time
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
RENDER = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
PLAIN = bool(os.environ.get("FC_BGFX_LINE_NO_COVERAGE"))
WIDTHS = (1.0, 1.25, 1.5, 2.0, 2.5, 3.5)
ANGLES = (0.0, 4.0, 22.5, 45.0, 81.0, 90.0)
CELL = 30.0     # the grid the lines stand on, mm
LENGTH = 24.0   # a line, mm
NEAR = {0: 0.12, 3: 0.16}   # pixels, by the value of View/AntiAliasing


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


def decode(v):
    # the screen's encoding undone: a blend of black and white in linear light
    v /= 255.0
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def make(doc):
    lines = []
    for i, width in enumerate(WIDTHS):
        for j, angle in enumerate(ANGLES):
            a = math.radians(angle)
            c = FreeCAD.Vector(i * CELL, j * CELL, 0)
            d = FreeCAD.Vector(math.cos(a), math.sin(a), 0) * (LENGTH / 2)
            obj = doc.addObject("Part::Feature", "L")
            obj.Shape = Part.makeLine(c - d, c + d)
            vo = obj.ViewObject
            vo.LineColor = (0.0, 0.0, 0.0)
            vo.PointColor = (0.0, 0.0, 0.0)
            vo.LineWidth = width
            vo.PointSize = 1
            lines.append((width, angle, c - d, c + d))
    doc.recompute()
    return lines


def weight(image, managed, a, b):
    """The ink across the line from a to b (picture coordinates), over the
    middle 60 per cent of its length, a pixel of length: its width."""
    ax, ay = a
    bx, by = b
    length = math.hypot(bx - ax, by - ay)
    ux, uy = (bx - ax) / length, (by - ay) / length
    t0, t1 = 0.2 * length, 0.8 * length
    # a whole number of pixels along it, so that a line along the grid is cut
    # between pixels at both ends
    t1 = t0 + math.floor(t1 - t0)
    reach = 7.0
    xs = [ax + ux * t0, ax + ux * t1]
    ys = [ay + uy * t0, ay + uy * t1]
    x_lo, x_hi = int(min(xs) - reach - 1), int(max(xs) + reach + 2)
    y_lo, y_hi = int(min(ys) - reach - 1), int(max(ys) + reach + 2)
    total = 0.0
    for y in range(max(y_lo, 0), min(y_hi, image.height())):
        for x in range(max(x_lo, 0), min(x_hi, image.width())):
            px, py = x + 0.5 - ax, y + 0.5 - ay
            t = px * ux + py * uy
            if t < t0 or t >= t1 or abs(py * ux - px * uy) > reach:
                continue
            p = image.pixel(x, y)
            grey = (((p >> 16) & 255) + ((p >> 8) & 255) + (p & 255)) / 3.0
            total += 1.0 - (decode(grey) if managed else grey / 255.0)
    return total / (t1 - t0), length


def measure(view, lines, mode, tag):
    VIEW.SetInt("AntiAliasing", mode)
    settle(2.5)
    view.redraw()
    settle(1.0)
    stats = view.getRenderStats()
    path = os.path.join(OUT, tag + ".png")
    view.saveRenderDump(path)
    image = QtGui.QImage(path)
    managed = RENDER.GetInt("OutputTransform", 1) != 0
    corner = image.pixel(3, 3)
    note("NOTE %s: %d x %d, %s samples, output transform %s, the ground 0x%06x" % (
        tag, image.width(), image.height(), stats.get("msaaSamples"), managed,
        corner & 0xFFFFFF))
    table = {}
    shortest = None
    for width, angle, p0, p1 in lines:
        a = view.getPointOnViewport(p0)
        b = view.getPointOnViewport(p1)
        h = image.height()
        got, length = weight(image, managed, (a[0], h - a[1]), (b[0], h - b[1]))
        table[(width, angle)] = got
        shortest = length if shortest is None else min(shortest, length)
    note("NOTE %s: a line is %.0f pixels long; its weight in pixels, a row a width asked "
         "for, a column an angle (%s)" % (
             tag, shortest, ", ".join("%g" % a for a in ANGLES)))
    worst = 0.0
    spread = 0.0
    for width in WIDTHS:
        row = [table[(width, angle)] for angle in ANGLES]
        note("NOTE %s:   %-5g %s   lightest to heaviest %.2f" % (
            tag, width, "  ".join("%5.2f" % v for v in row), max(row) - min(row)))
        worst = max(worst, max(abs(v - width) for v in row))
        spread = max(spread, max(row) - min(row))
    if PLAIN:
        note("NOTE %s, lines WITHOUT coverage: furthest from the width asked for %.2f px, "
             "widest difference between two angles of one width %.2f px" % (tag, worst, spread))
        return
    check("%s: every line weighs the width asked for" % tag, worst <= NEAR[mode],
          "the furthest is %.2f px off, %.2f allowed" % (worst, NEAR[mode]))
    check("%s: a width weighs the same at every angle" % tag, spread <= NEAR[mode],
          "the widest difference between two angles is %.2f px" % spread)


def run():
    doc = None
    had = dict((k, VIEW.GetBool(k, True)) for k in ("ShowNaviCube", "CornerCoordSystem"))
    try:
        for k in had:
            VIEW.SetBool(k, False)
        VIEW.SetBool("Simple", True)
        VIEW.SetBool("Gradient", False)
        VIEW.SetBool("RadialGradient", False)
        VIEW.SetUnsigned("BackgroundColor", 0xFFFFFFFF)
        doc = FreeCAD.newDocument("LineWeight")
        lines = make(doc)
        view = FreeCADGui.ActiveDocument.ActiveView
        view.setAnimationEnabled(False)
        view.setCameraType("Orthographic")
        view.viewTop()
        view.fitAll()
        settle(3.0)
        try:
            view.getRenderStats()
        except Exception as e:
            check("the view is drawn by the backend", False, e)
            return
        if PLAIN:
            note("NOTE FC_BGFX_LINE_NO_COVERAGE is set: lines are plain quads, nothing is claimed")
        measure(view, lines, 0, "none")
        measure(view, lines, 3, "msaa4")
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        try:
            for k, v in had.items():
                VIEW.SetBool(k, v)
            if doc is not None:
                FreeCAD.closeDocument(doc.Name)
        except Exception:
            pass
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
