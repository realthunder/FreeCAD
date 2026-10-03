"""A reference constraint is read-only in the property editor.

The Constraints property of a sketch unfolds, in the property editor, into
its dimensions: the named ones by name, the rest under "Unnamed". A
reference (driven) dimension reports what the sketch measures; it cannot be
given a value. The unnamed ones were read-only already. A named one was
not: the editor opened on it and took a number the sketch then refused.

Upstream `e828c5da4d`.

Claims, with a driving dimension named Width and a reference one named
Measured:

  - Width's value cell is editable;
  - Measured's is not.

The cells are read from the editor's model, which is what decides whether
an editor opens.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchConstraintPropertyReadOnly"
OBJ = "Sketch"
V = FreeCAD.Vector


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def find(model, parent, text):
    """The row under `parent` whose name cell reads `text`."""
    for row in range(model.rowCount(parent)):
        index = model.index(row, 0, parent)
        if str(model.data(index, QtCore.Qt.DisplayRole)) == text:
            return index
        if model.rowCount(index):
            inner = find(model, index, text)
            if inner is not None:
                return inner
    return None


def editable(model, name_index):
    value = model.index(name_index.row(), 1, name_index.parent())
    return bool(model.flags(value) & QtCore.Qt.ItemIsEditable)


def run():
    try:
        import Part
        import Sketcher

        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", OBJ)
        sk.addGeometry(Part.LineSegment(V(0, 0, 0), V(30, 0, 0)), False)
        sk.addGeometry(Part.LineSegment(V(0, 10, 0), V(20, 10, 0)), False)
        sk.addConstraint(Sketcher.Constraint("Distance", 0, 30))
        sk.renameConstraint(0, "Width")
        sk.addConstraint(Sketcher.Constraint("Distance", 1, 20))
        sk.renameConstraint(1, "Measured")
        sk.setDriving(1, False)
        doc.recompute()
        check("Width drives, Measured is a reference",
              sk.getDriving(0) is True and sk.getDriving(1) is False,
              (sk.getDriving(0), sk.getDriving(1)))

        FreeCADGui.getMainWindow().showMaximized()
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(doc.Name, OBJ)
        settle(1.0)

        found = {}
        for view in FreeCADGui.getMainWindow().findChildren(QtWidgets.QTreeView):
            if view.metaObject().className() != "Gui::PropertyEditor::PropertyEditor":
                continue
            model = view.model()
            if model is None:
                continue
            constraints = find(model, QtCore.QModelIndex(), "Constraints")
            if constraints is None:
                continue
            # the children are made when the row is first unfolded
            view.expand(constraints)
            settle(0.3)
            for name in ("Width", "Measured"):
                index = find(model, constraints, name)
                if index is not None:
                    found[name] = editable(model, index)
            if found:
                break
        check("the editor shows both dimensions under Constraints",
              set(found) == {"Width", "Measured"}, found)
        check("Width's value can be edited", found.get("Width") is True, found)
        check("Measured's cannot", found.get("Measured") is False, found)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())

    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
