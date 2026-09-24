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
  - clearing after a batch uncolours them all.

Scored against the tree before the fix: the three batch checks fail (0 of
200 coloured, 0 of 200 element rows, 0 of 150 constraint rows) and the
others pass. With only the view provider fixed, the two list checks fail.
"""
import os
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
