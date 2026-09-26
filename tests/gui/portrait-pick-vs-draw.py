"""A portrait view draws, projects and picks with one projection.

Coin maps a camera to a viewport with ADJUST_CAMERA: when the viewport
is taller than wide (aspect < 1) the view volume is widened by 1/aspect,
so the camera's height spans the WIDTH. Coin's ray pick goes through
that mapping (SoCamera::getView). The backend frame (mode 3) and
getPointOnViewport built their volume with getViewVolume(aspect) alone,
which skips the widening: on a portrait view they put every object at
x * (1/aspect) from the centre, and the user picks something other than
what they see.

Five coloured cubes on the front plane, off centre in BOTH axes (a
centre-only probe cannot see a scale error). For each cube, three
screen positions are compared:
  - proj:  view.getPointOnViewport(cube centre);
  - pick:  centroid of a blind getObjectInfo sweep;
  - frame: centroid of the cube's colour in the backend's own
           framebuffer (saveRenderDump "renderer").
Claims, in a portrait view and in a landscape one (the control):
  - fit-all frames the scene's bounding sphere across the smaller side;
  - the pick at proj hits the cube;
  - proj, pick and frame agree within a few pixels.
And in a portrait split cell of the ViewArea unified canvas, which
feeds the backend per cell: the frame puts each cube where the mapped
camera does in the cell's rect.
"""
import colorsys
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PortraitPick"
TOL = 4  # pixels

# (name, world x, world z, hue in degrees, rgb)
CUBES = [
    ("Centre", 0, 0, 0, (1.0, 0.0, 0.0)),
    ("RightUp", 40, 30, 120, (0.0, 1.0, 0.0)),
    ("LeftUp", -40, 30, 240, (0.0, 0.0, 1.0)),
    ("RightDown", 40, -30, 60, (1.0, 1.0, 0.0)),
    ("LeftDown", -40, -30, 300, (1.0, 0.0, 1.0)),
]
SIZE = 8
CELL_HEIGHT = 120  # mm, the camera height in the canvas cell leg


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle():
    for _ in range(10):
        QtCore.QCoreApplication.processEvents()


def centre_of(name):
    for n, x, z, _h, _c in CUBES:
        if n == name:
            return FreeCAD.Vector(x, -SIZE / 2.0, z)
    return None


def sweep(view):
    """Centroid of every object's pick hits, bottom-left origin."""
    w, h = view.getSize()
    found = {}
    for gy in range(0, h, 2):
        for gx in range(0, w, 2):
            i = view.getObjectInfo((gx, gy))
            if i:
                found.setdefault(i.get("Object"), []).append((gx, gy))
    return {k: (sum(p[0] for p in v) / len(v), sum(p[1] for p in v) / len(v))
            for k, v in found.items()}


def canvas_cell(view, tag):
    """The view's cell cut out of the ViewArea unified canvas, or None
    without a canvas. saveRenderDump reads the viewer's own backend,
    and a canvas cell has none of its own: the canvas draws them all."""
    import shiboken6
    from PySide6.QtOpenGLWidgets import QOpenGLWidget
    from PySide import QtWidgets
    mw = FreeCADGui.getMainWindow()
    canvas = [w for w in mw.findChildren(QtWidgets.QWidget)
              if w.metaObject().className() == "Gui::ViewAreaCanvas"
              and w.isVisible()]
    if len(canvas) != 1:
        note("%s: %d visible canvases" % (tag, len(canvas)))
        return None
    canvas = shiboken6.wrapInstance(shiboken6.getCppPointer(canvas[0])[0],
                                    QOpenGLWidget)
    size = tuple(view.getSize())
    # The viewer is matched by its size, and the cell is its enclosing
    # View3DInventor, which the canvas keeps at the cell rect.
    viewers = [w for w in mw.findChildren(QtWidgets.QWidget)
               if w.metaObject().className() == "Gui::View3DInventorViewer"
               and (w.width(), w.height()) == size]
    if len(viewers) != 1:
        note("%s: %d viewers of size %s" % (tag, len(viewers), size))
        return None
    cell = viewers[0]
    while cell and cell.metaObject().className() != "Gui::View3DInventor":
        cell = cell.parentWidget()
    # The canvas is a sibling under the ViewArea, not an ancestor.
    origin = cell.mapToGlobal(QtCore.QPoint(0, 0)) \
        - canvas.mapToGlobal(QtCore.QPoint(0, 0))
    note("%s: canvas %dx%d, cell %dx%d at (%d,%d), viewer %dx%d" % (
        tag, canvas.width(), canvas.height(), cell.width(), cell.height(),
        origin.x(), origin.y(), size[0], size[1]))
    full = canvas.grabFramebuffer()
    full.save(os.path.join(OUT, "%s-canvas.png" % tag))
    img = full.copy(origin.x(), origin.y(), cell.width(), cell.height())
    img.save(os.path.join(OUT, "%s.png" % tag))
    return img


def frame(view, tag, canvas):
    """Centroid of each cube in the backend framebuffer, bottom-left
    origin to match the pick: the largest connected blob of its hue, so
    the NaviCube's axis lines (same red, green, blue) do not count."""
    if canvas:
        img = canvas_cell(view, tag)
        if img is None:
            return {}, None
    else:
        path = os.path.join(OUT, "%s.png" % tag)
        view.saveRenderDump(path, "renderer")
        img = QtGui.QImage(path)
    w, h = img.width(), img.height()
    label = {}
    for y in range(0, h):
        for x in range(0, w):
            c = QtGui.QColor(img.pixel(x, y))
            hh, s, v = colorsys.rgb_to_hsv(c.redF(), c.greenF(), c.blueF())
            if s < 0.6 or v < 0.2:
                continue
            deg = hh * 360.0
            for n, _x, _z, hue, _c in CUBES:
                d = abs(deg - hue)
                if min(d, 360 - d) < 12:
                    label[(x, y)] = n
    best = {}
    seen = set()
    for start, n in label.items():
        if start in seen:
            continue
        blob = []
        todo = [start]
        seen.add(start)
        while todo:
            p = todo.pop()
            blob.append(p)
            for q in ((p[0] + 1, p[1]), (p[0] - 1, p[1]),
                      (p[0], p[1] + 1), (p[0], p[1] - 1)):
                if q not in seen and label.get(q) == n:
                    seen.add(q)
                    todo.append(q)
        if len(blob) > len(best.get(n, ())):
            best[n] = blob
    return {n: (sum(p[0] for p in b) / len(b),
                sum(h - 1 - p[1] for p in b) / len(b))
            for n, b in best.items()}, (w, h)


def dist(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def check_fit(view, tag):
    """Fit-all frames the scene's bounding sphere across the SMALLER side
    of the view: ADJUST_CAMERA already makes the camera height span it,
    and Coin's viewBoundingBox dividing the height by a portrait aspect
    on top left the scene at `aspect` of the width."""
    doc = FreeCAD.getDocument(DOC)
    box = FreeCAD.BoundBox()
    for n, *_ in CUBES:
        box.add(doc.getObject(n).Shape.BoundBox)
    radius = box.DiagonalLength / 2.0
    w, h = view.getSize()
    a = view.getPointOnViewport(centre_of("LeftUp"))
    b = view.getPointOnViewport(centre_of("RightUp"))
    scale = (b[0] - a[0]) / 80.0
    want = min(w, h) / (2.0 * radius)
    note("%s fit: %.3f px/mm, want %.3f" % (tag, scale, want))
    check("%s: fit-all spans the smaller side" % tag,
          abs(scale - want) <= 0.02 * want, "%.3f" % scale)


def measure(view, tag):
    view.viewFront()
    view.fitAll()
    settle()
    FreeCADGui.updateGui()
    settle()
    w, h = view.getSize()
    note("%s view %dx%d aspect %.3f" % (tag, w, h, float(w) / h))
    check_fit(view, tag)
    picks = sweep(view)
    frames, fsize = frame(view, tag, False)
    note("%s frame size %s" % (tag, fsize))
    check("%s: the dump is this view's frame" % tag, fsize == (w, h), fsize)
    for n, *_ in CUBES:
        p = view.getPointOnViewport(centre_of(n))
        proj = (float(p[0]), float(p[1]))
        info = view.getObjectInfo((int(p[0]), int(p[1])))
        got = info.get("Object") if info else None
        pk = picks.get(n)
        fr = frames.get(n)
        note("%s %s proj=(%.1f,%.1f) pick=%s frame=%s" % (
            tag, n, proj[0], proj[1],
            "(%.1f,%.1f)" % pk if pk else None,
            "(%.1f,%.1f)" % fr if fr else None))
        check("%s %s: pick at the projection hits it" % (tag, n), got == n, got)
        check("%s %s: pick agrees with the projection" % (tag, n),
              pk is not None and dist(pk, proj) <= TOL, pk)
        check("%s %s: frame agrees with the projection" % (tag, n),
              fr is not None and dist(fr, proj) <= TOL, fr)


def measure_cell(view, tag):
    """A split cell of the ViewArea unified canvas, which draws its cells
    itself (ViewAreaCanvas) rather than through renderScene. Only the
    frame is checked, against where Coin's mapping puts each cube in the
    CELL's rect: the cell's hidden viewer is not kept at the cell's size
    (a separate defect), so its projection and pick say nothing about
    what the canvas drew. A fixed camera height keeps the cubes large."""
    view.viewFront()
    view.fitAll()
    settle()
    view.getCameraNode().height.setValue(CELL_HEIGHT)
    settle()
    FreeCADGui.updateGui()
    settle()
    frames, fsize = frame(view, tag, True)
    note("%s cell size %s" % (tag, fsize))
    if not check("%s: the canvas cell was grabbed" % tag, fsize is not None):
        return
    w, h = fsize
    # ADJUST_CAMERA: the camera height spans the SMALLER side.
    s = min(w, h) / float(CELL_HEIGHT)
    for n, x, z, _h, _c in CUBES:
        want = ((w - 1) / 2.0 + x * s, (h - 1) / 2.0 + z * s)
        fr = frames.get(n)
        note("%s %s want=(%.1f,%.1f) frame=%s" % (
            tag, n, want[0], want[1], "(%.1f,%.1f)" % fr if fr else None))
        check("%s %s: frame is where the mapped camera puts it" % (tag, n),
              fr is not None and dist(fr, want) <= TOL, fr)


def run():
    try:
        doc = FreeCAD.newDocument(DOC)
        for n, x, z, _h, rgb in CUBES:
            b = doc.addObject("Part::Box", n)
            b.Length = b.Width = b.Height = SIZE
            b.Placement.Base = FreeCAD.Vector(x - SIZE / 2.0, -SIZE, z - SIZE / 2.0)
        doc.recompute()
        for n, *_rest in CUBES:
            vo = doc.getObject(n).ViewObject
            vo.ShapeColor = _rest[-1]
            vo.LineColor = _rest[-1]
            vo.PointColor = _rest[-1]
        view = FreeCADGui.getDocument(DOC).activeView()
        mw = FreeCADGui.getMainWindow()
        mw.showNormal()

        mw.resize(700, 1000)
        settle()
        measure(view, "portrait")

        mw.resize(1200, 700)
        settle()
        measure(view, "landscape")

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "UnifiedCanvas", True)
        mw.resize(1000, 1000)
        settle()
        FreeCADGui.runCommand("Std_ViewSplitRight")
        settle()
        measure_cell(FreeCADGui.getDocument(DOC).activeView(), "cell")
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finish()


def finish():
    try:
        FreeCAD.closeDocument(DOC)
    except Exception:
        pass
    note("DONE")
    QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, run)
