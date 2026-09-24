"""A bulk selection reaches the sketch being edited.

`Gui::Selection().addSelections()` pauses notification for the batch, and
once more than `MaxSelectionNotification` (100) changes are queued the core
drops the item-by-item messages and sends one `SetSelection`, meaning
"re-read the selection". All three observers of a sketch edit treated
`SetSelection` as a no-op -- the view provider and both task panels -- so a
batch of more than a hundred elements was selected in `Gui.Selection` and
nowhere a user could see: no curve turned the selection colour and no row
of either list was selected.

The sketch is read from the edit graph's `CurvesMaterials` node, one diffuse
colour per curve (a selected curve is `ViewProviderSketch::SelectColor`);
the panels from their list widgets' selected rows.

Checks, on a sketch of 200 lines, 150 of them with a Vertical constraint:

  - a batch past the threshold colours every edge (the view provider);
  - and selects every edge's row in the elements list;
  - a batch of 150 constraints selects their rows in the constraints list;
  - a batch under the threshold, which still arrives item by item, colours
    its edges (the per-item path was refactored to share the parser);
  - clearing after a batch uncolours them all;
  - a box dragged around all 200 lines on the desktop -- synthetic mouse
    events on the viewport, which reach the sketch's rubber band -- selects
    and colours every edge, and reaches a selection observer as one batch
    rather than 600 single adds. That is the speed-up: item by item, each
    observer of the edit redrew once per element (measured 3.7 s for a
    3000-element box, 0.12 s batched). Counted, not timed.

Scored against the tree before the fix: the three batch checks fail (0 of
200 coloured, 0 of 200 element rows, 0 of 150 constraint rows) and the
others pass. With only the view provider fixed, the two list checks fail.
With box selection still adding item by item, the observer counts 600
single adds and the batch check fails.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
N = 200         # past MaxSelectionNotification (100)
NCONSTR = 150   # also past it
SMALL = 20      # under it
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + ("" if detail == "" else " (%s)" % (detail,)))


def coloured_curves():
    """How many edit curves are drawn in SelectColor (0.11, 0.68, 0.11)."""
    mat = coin.SoNode.getByName("CurvesMaterials")
    if not mat:
        return -1
    return sum(1 for c in mat.diffuseColor.getValues()
               if abs(c[0] - 0.11) < 0.02 and abs(c[1] - 0.68) < 0.02
               and abs(c[2] - 0.11) < 0.02)


def selected_rows(name):
    """Selected rows of the named task panel list, or -1 if it is not up."""
    from PySide import QtWidgets
    mw = FreeCADGui.getMainWindow()
    for w in mw.findChildren(QtWidgets.QAbstractItemView):
        if w.objectName() == name:
            return len(w.selectionModel().selectedRows())
    return -1


def coloured_or_preselected(sk):
    """Curves drawn as selected, counting one the pointer rests on: that one
    is PreselectSelectedColor, 0.6 * PreselectColor + 0.4 * SelectColor."""
    mat = coin.SoNode.getByName("CurvesMaterials")
    if not mat:
        return -1
    select = (0.11, 0.68, 0.11)
    preselect_selected = tuple(0.6 * p + 0.4 * q for p, q in zip((0.88, 0.88, 0.0), select))

    def near(c, want):
        return all(abs(c[i] - want[i]) < 0.1 for i in range(3))

    return sum(1 for c in mat.diffuseColor.getValues()
               if near(c, select) or near(c, preselect_selected))


class Counter:
    """A Python selection observer counting what reaches it."""

    def __init__(self):
        self.adds = 0
        self.sets = 0

    def addSelection(self, doc, obj, sub, pnt):
        self.adds += 1

    def setSelection(self, doc):
        self.sets += 1


def viewport():
    from PySide import QtWidgets
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        name = w.metaObject().className()
        if ("Quarter" in name or "View3DInventorViewer" in name) and w.isVisible():
            return w
    return None


def mouse(w, typ, pos, button, buttons):
    from PySide import QtGui, QtWidgets
    p = QtCore.QPointF(pos[0], pos[1])
    QtWidgets.QApplication.sendEvent(
        w, QtGui.QMouseEvent(typ, p, w.mapToGlobal(p), button, buttons,
                             QtCore.Qt.NoModifier))
    QtCore.QCoreApplication.processEvents()


def box_select(view, w, corner_a, corner_b):
    """Drag a window box (left to right) between two sketch points."""
    h = w.height()

    def px(x, y):
        p = view.getPointOnViewport(FreeCAD.Vector(x, y, 0))
        return (p[0], h - 1 - p[1])

    a, b = px(*corner_a), px(*corner_b)
    note("box drag on %s %dx%d from %s to %s" % (w.metaObject().className(), w.width(), h, a, b))
    mid = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
    left, none = QtCore.Qt.LeftButton, QtCore.Qt.NoButton
    mouse(w, QtCore.QEvent.MouseMove, a, none, none)
    mouse(w, QtCore.QEvent.MouseButtonPress, a, left, left)
    mouse(w, QtCore.QEvent.MouseMove, mid, none, left)
    mouse(w, QtCore.QEvent.MouseMove, b, none, left)
    mouse(w, QtCore.QEvent.MouseButtonRelease, b, left, none)


def selected(sk):
    ex = [s for s in FreeCADGui.Selection.getSelectionEx("*") if s.Object == sk]
    return len(ex[0].SubElementNames) if ex else 0


def run():
    try:
        import Part
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCADGui.getMainWindow().showMaximized()
        V = FreeCAD.Vector
        doc = FreeCAD.newDocument("BulkSelection")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry([Part.LineSegment(V(i * 3, 0, 0), V(i * 3, 10, 0))
                        for i in range(N)], False)
        import Sketcher
        sk.addConstraint([Sketcher.Constraint("Vertical", i) for i in range(NCONSTR)])
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        QtCore.QCoreApplication.processEvents()
        FreeCADGui.Selection.clearSelection()
        QtCore.QCoreApplication.processEvents()
        check("nothing coloured before selecting", coloured_curves() == 0,
              coloured_curves())

        FreeCADGui.Selection.addSelections(
            [(sk, "Edge%d" % (i + 1)) for i in range(N)])
        QtCore.QCoreApplication.processEvents()
        got = coloured_curves()
        note("batch of %d: selected %d, coloured %d" % (N, selected(sk), got))
        check("a batch past the notification limit colours every edge",
              got == N and selected(sk) == N, "%d of %d" % (got, N))
        rows = selected_rows("elementsWidget")
        check("a batch past the limit selects every edge's row in the elements list",
              rows == N, "%d of %d" % (rows, N))

        FreeCADGui.Selection.clearSelection()
        QtCore.QCoreApplication.processEvents()
        FreeCADGui.Selection.addSelections(
            [(sk, "Constraint%d" % (i + 1)) for i in range(NCONSTR)])
        QtCore.QCoreApplication.processEvents()
        rows = selected_rows("listWidgetConstraints")
        note("batch of %d constraints: selected %d, rows %d" % (NCONSTR, selected(sk), rows))
        check("a batch of constraints selects their rows in the constraints list",
              rows == NCONSTR, "%d of %d" % (rows, NCONSTR))

        FreeCADGui.Selection.clearSelection()
        QtCore.QCoreApplication.processEvents()
        check("clearing a batch uncolours every edge", coloured_curves() == 0,
              coloured_curves())

        FreeCADGui.Selection.addSelections(
            [(sk, "Edge%d" % (i + 1)) for i in range(SMALL)])
        QtCore.QCoreApplication.processEvents()
        got = coloured_curves()
        note("batch of %d: selected %d, coloured %d" % (SMALL, selected(sk), got))
        check("a batch under the limit colours its edges", got == SMALL,
              "%d of %d" % (got, SMALL))

        # The desktop box, on a sketch of its own: rows of 50 lines, clear
        # of both axes and with no constraint icons, so the press that
        # starts the box lands on nothing (a press on an element drags it
        # instead).
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.getDocument(doc.Name).resetEdit()
        QtCore.QCoreApplication.processEvents()
        # The first sketch is hidden, or fitAll frames both and the box
        # comes out a few pixels from the axes.
        sk.ViewObject.Visibility = False
        rows = (N + 49) // 50
        box = doc.addObject("Sketcher::SketchObject", "Box")
        box.addGeometry([Part.LineSegment(
            V(20 + (i % 50) * 2.0, 20 + (i // 50) * 15.0, 0),
            V(20 + (i % 50) * 2.0, 30 + (i // 50) * 15.0, 0)) for i in range(N)], False)
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(box)
        QtCore.QCoreApplication.processEvents()
        FreeCADGui.Selection.clearSelection()
        view = FreeCADGui.activeDocument().activeView()
        view.viewTop()
        view.fitAll()
        for _ in range(40):     # fitAll animates
            QtCore.QCoreApplication.processEvents()
            time.sleep(0.02)
        w = viewport()
        counter = Counter()
        FreeCADGui.Selection.addObserver(counter)
        try:
            box_select(view, w, (15.0, 35.0 + (rows - 1) * 15.0), (125.0, 15.0))
            QtCore.QCoreApplication.processEvents()
        finally:
            FreeCADGui.Selection.removeObserver(counter)
        ex = [e for e in FreeCADGui.Selection.getSelectionEx("*") if e.Object == box]
        subs = ex[0].SubElementNames if ex else []
        edges = sum(1 for n in subs if n.startswith("Edge"))
        got = coloured_or_preselected(box)
        note("box: %d selected, %d edges, %d coloured; observer saw %d adds, %d sets"
             % (len(subs), edges, got, counter.adds, counter.sets))
        if len(subs) < 5:
            note("box selection holds: %s" % ([(e.ObjectName, e.SubElementNames) for e in FreeCADGui.Selection.getSelectionEx("*")],))
        check("a box around every line selects every edge", edges == N,
              "%d of %d" % (edges, N))
        check("and colours every edge", got == N, "%d of %d" % (got, N))
        check("the box reaches an observer as one batch, not item by item",
              counter.sets >= 1 and counter.adds < len(subs),
              "%d adds, %d sets for %d elements" % (counter.adds, counter.sets, len(subs)))

        FreeCADGui.Selection.clearSelection()
        FreeCADGui.getDocument(doc.Name).resetEdit()
        QtCore.QCoreApplication.processEvents()
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
