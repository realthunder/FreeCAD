"""Toggle Construction passes over what cannot be toggled.

An ellipse's axes and foci are internal geometry: aligned to the ellipse,
they follow it and have no construction state of their own, and the sketch
refuses to toggle them. A box selection takes them along with the ellipse.
The command asked the sketch to toggle each selected edge in turn, the
refusal for the axis came back as an error, and that stopped the command:
what was selected after the axis was never toggled.

Upstream skips such geometry in the command (`7432ce131f`). So does this.

Claims, with an ellipse, its major axis and a plain line selected, in that
order:

  - the command returns without an error;
  - the ellipse and the line are construction geometry afterwards -- the
    line is the one that was lost;
  - the axis is what it was.

Scored against the tree before the change: see the commit message.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchToggleConstructionInternal"
OBJ = "Sketch"
V = FreeCAD.Vector


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def run():
    try:
        import Part
        import SketcherGui  # noqa: F401  -- registers the commands

        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", OBJ)
        ellipse = sk.addGeometry(Part.Ellipse(V(20, 0, 0), V(0, 10, 0), V(0, 0, 0)), False)
        sk.exposeInternalGeometry(ellipse)
        line = sk.addGeometry(Part.LineSegment(V(-30, -20, 0), V(30, -20, 0)), False)
        doc.recompute()
        facades = sk.GeometryFacadeList
        axis = next((i for i, g in enumerate(sk.Geometry)
                     if i not in (ellipse, line) and g.TypeId == "Part::GeomLineSegment"
                     and facades[i].InternalType != "None"), None)
        if not check("the ellipse has an axis, internal to it", axis is not None,
                     [f.InternalType for f in facades]):
            raise RuntimeError("no internal geometry to test with")
        before = [sk.getConstruction(i) for i in (ellipse, axis, line)]
        check("the ellipse and the line are not construction geometry to begin with",
              before[0] is False and before[2] is False, before)

        FreeCADGui.ActiveDocument.setEdit(sk, 0)
        FreeCADGui.Selection.clearSelection()
        for geo in (ellipse, axis, line):
            FreeCADGui.Selection.addSelection(doc.Name, OBJ, "Edge%d" % (geo + 1))
        error = None
        try:
            FreeCADGui.runCommand("Sketcher_ToggleConstruction", 0)
        except Exception as e:  # a command reports rather than raises; be told either way
            error = repr(e)
        doc.recompute()
        after = [sk.getConstruction(i) for i in (ellipse, axis, line)]
        check("the command returns without an error", error is None, error)
        check("the ellipse is construction geometry", after[0] is True, after)
        check("the line selected after the axis is too", after[2] is True, after)
        check("the axis is what it was", after[1] == before[1], after)

        FreeCADGui.Selection.clearSelection()
        FreeCADGui.ActiveDocument.resetEdit()
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
