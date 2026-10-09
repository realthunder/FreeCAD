"""The outline of a face under the pointer has no staircase on its inner edge.

docs/HandsOnQueue.md entry 25: "face highlight silhouette shows jagged edge
regardless whether msaa is used or not". The outline is the outer half of a
thick line along the face's boundary; the inner half is cut away by a
stencil mark of the face, which is one sample a pixel whatever the
multisampling. The outer edge of the line has analytic coverage, the cut had
none: along a slanted boundary the outline stepped against the model's dark
edge a whole pixel at a time. Where the face's own fill is not drawn over
it -- a face under the pointer -- the outline's lines now fade in from the
cut (fc_flat_fs.sh), are the boundary's edges alone, and keep each other
and the corner caps out of the pixels they have drawn.

A SELECTED face is filled as well, and there the cut is right as it is: the
fill and the outline end on the very same pixels and hide the model's edge
between them. A fade there let that edge through as dark dots along the
outline (the first build of this did).

Measured on the top face of a cylinder seen from the isometric side, where
the boundary is an ellipse, over a slanted stretch of the upper arc: for
each pixel column, how much outline colour it holds and where the middle of
that is, fractions of a pixel included. A staircase shows as jumps of the
middle from one column to the next.

Claims:
  - preselected: the outline is there in every column of the stretch, and
    runs smoothly: the mean second difference of its middle, column to
    column, is under 0.22 px (0.33 with the cut, 0.16 with the fade);
  - selected: the outline is there, and nothing dark lies between the
    face's fill and it.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "FaceOutline"
SMOOTH = 0.22


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


def rgb_at(image, x, y):
    p = image.pixel(x, y)
    return ((p >> 16) & 255, (p >> 8) & 255, p & 255)


def apart(a, b):
    return sum(abs(a[c] - b[c]) for c in range(3))


class Scale:
    """How much outline a pixel holds, 0 to 1: its place between the colour
    of the face the outline goes round and the outline's own colour. Both
    are read off the picture: the outline's is the pixel furthest along the
    highlight colour's own contrast, the face's the one in the middle of
    the outline."""

    def __init__(self, image, colour):
        # the middle of the picture only: the corner cube and the axis cross have
        # the same pure colours
        w, h = image.width(), image.height()
        span_x = range(int(0.2 * w), int(0.8 * w), 2)
        span_y = range(int(0.1 * h), int(0.9 * h), 2)
        strong = self.strong = [c for c in range(3) if colour[c] >= 128]
        weak = self.weak = [c for c in range(3) if colour[c] < 128] or [0]
        best = -1
        self.outline = None
        for y in span_y:
            for x in span_x:
                c = rgb_at(image, x, y)
                score = min(c[k] for k in strong) - max(c[k] for k in weak)
                if score > best:
                    best = score
                    self.outline = c
        xs, ys = [], []
        for y in span_y:
            for x in span_x:
                if apart(rgb_at(image, x, y), self.outline) < 40:
                    xs.append(x)
                    ys.append(y)
        self.count = len(xs)
        self.box = (min(xs), max(xs), min(ys), max(ys)) if xs else None
        self.face = None
        if self.box:
            self.face = rgb_at(image, (self.box[0] + self.box[1]) // 2,
                               (self.box[2] + self.box[3]) // 2)
        self.image = image

    def score(self, c):
        return min(c[k] for k in self.strong) - max(c[k] for k in self.weak)

    def amount(self, x, y):
        # Along the highlight colour's own contrast, which a blend with black
        # (the edge the outline lies on), with the face or with the background
        # moves along in proportion; the face's own share of it taken off.
        base = max(self.score(self.face), 0)
        full = self.score(self.outline) - base
        if full <= 0:
            return 0.0
        return max(0.0, min(1.0, (self.score(rgb_at(self.image, x, y)) - base) / float(full)))


def measure(view, tag, colour, faded):
    view.redraw()
    settle(1.0)
    path = os.path.join(OUT, tag + ".png")
    view.saveRenderDump(path)
    image = QtGui.QImage(path)
    scale = Scale(image, colour)
    note("NOTE %s: the outline is drawn %s, the face inside it %s" % (
        tag, scale.outline, scale.face))
    if not check("%s: the outline is in the picture" % tag,
                 scale.count > 50 and apart(scale.face, scale.outline) > 60, scale.count):
        return

    def amount(_image, x, y, _colour):
        return scale.amount(x, y)

    x0, x1, y0, y1 = scale.box
    # a slanted stretch of the upper arc: between the top of the ellipse and its side
    a = x0 + int(0.12 * (x1 - x0))
    b = x0 + int(0.38 * (x1 - x0))
    mid = (y0 + y1) // 2
    thick = []
    dark = [0]
    centre = []
    top = max(y0 - 4, 0)
    for x in range(a, b + 1):
        column = [amount(image, x, y, colour) for y in range(top, mid)]
        # the band itself: a few rows either side of where the column is strongest,
        # not the whole way down the face, whose fill is not exactly nothing
        peak = max(range(len(column)), key=column.__getitem__)
        first = max(peak - 6, 0)
        column = column[first:peak + 7]
        for y in range(top + first, top + first + len(column)):
            if max(rgb_at(image, x, y)) < 90:
                dark[0] += 1
        total = sum(column)
        thick.append(total)
        centre.append(first + sum(i * v for i, v in enumerate(column)) / total
                      if total > 0 else 0.0)
    image.copy(a, top, b - a + 1, mid - top).scaled(
        (b - a + 1) * 4, (mid - top) * 4, QtCore.Qt.IgnoreAspectRatio,
        QtCore.Qt.FastTransformation).save(os.path.join(OUT, tag + "-x4.png"))
    check("%s: every column of the stretch holds some outline" % tag, min(thick) > 0.5,
          "%d columns, the thinnest %.2f px" % (len(thick), min(thick)))

    def rough(series):
        return sum(abs(series[i + 1] - 2 * series[i] + series[i - 1])
                   for i in range(1, len(series) - 1)) / max(len(series) - 2, 1)

    note("NOTE %s: thickness %.2f to %.2f px, its mean second difference %.3f px" % (
        tag, min(thick), max(thick), rough(thick)))
    if faded:
        check("%s: the outline runs smoothly along the arc" % tag, rough(centre) < SMOOTH,
              "the mean second difference of its middle, column to column, is %.3f px over "
              "%d columns" % (rough(centre), len(centre)))
    else:
        note("NOTE %s: the mean second difference of its middle is %.3f px" % (
            tag, rough(centre)))
        check("%s: nothing dark between the face's fill and its outline" % tag, dark[0] == 0,
              "%d dark pixels in the band over %d columns" % (dark[0], len(centre)))


def run():
    try:
        doc = FreeCAD.newDocument(DOC)
        cyl = doc.addObject("Part::Cylinder", "Cylinder")
        cyl.Radius = 10
        cyl.Height = 6
        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        view.viewIsometric()
        view.fitAll()
        settle(3.0)
        # the top face: the one whose centre is highest
        top = max(range(len(cyl.Shape.Faces)),
                  key=lambda i: cyl.Shape.Faces[i].CenterOfMass.z)
        face = "Face%d" % (top + 1)
        prefs = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")

        def rgb(value):
            return ((value >> 24) & 255, (value >> 16) & 255, (value >> 8) & 255)

        hover = rgb(prefs.GetUnsigned("HighlightColor", 0xFFFF00FF))
        picked = rgb(prefs.GetUnsigned("SelectionColor", 0x00FF00FF))
        note("NOTE %s; preselection colour %s, selection colour %s" % (face, hover, picked))
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.setPreselection(cyl, face)
        settle(1.0)
        measure(view, "preselected", hover, True)
        FreeCADGui.Selection.clearPreselection()
        FreeCADGui.Selection.addSelection(cyl, face)
        settle(1.0)
        measure(view, "selected", picked, False)
        FreeCADGui.Selection.clearSelection()
        FreeCAD.closeDocument(doc.Name)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
