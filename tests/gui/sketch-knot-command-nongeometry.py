"""A knot command must survive a selection that is not geometry.

`getIdsFromName()` maps a sketch sub-element name to a GeoId, and leaves
GeoId at `GeoEnum::GeoUndef` for any name it does not recognise as
geometry -- `Constraint3`, `Face1`, anything else that can be picked in
a sketch. `SketchObject::getGeometry()` answers null for an id out of
range. `isBsplineKnotOrEndPoint()` dereferenced that null:

    #0  libc
    #1  SketcherGui::isBsplineKnotOrEndPoint(...)
    #2  CmdSketcherIncreaseKnotMultiplicity::activated(int)

The knot commands read the selection straight into that helper with no
check of what kind of sub-element it is, so selecting a constraint and
invoking "Increase knot multiplicity" from the menu took the process
down.

Upstream's `2aa8f133f3` guards one call site, in an activation predicate
this fork does not have. The guard here is in the helper, which is what
all of the fork's call sites go through.

What is asserted: with a constraint selected, and then with a face,
both knot commands return without a crash and without touching the
sketch. Reaching DONE at all is most of the test -- a segfault has no
result line to fail.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchKnotNonGeometry"
OBJ = "Sketch"

COMMANDS = ["Sketcher_BSplineIncreaseKnotMultiplicity",
            "Sketcher_BSplineDecreaseKnotMultiplicity"]

state = {"doc": None}


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
        import Sketcher
        import SketcherGui  # noqa: F401  -- registers the commands

        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        sk = doc.addObject("Sketcher::SketchObject", OBJ)
        # A closed square: gives the sketch both a constraint to select and
        # an internal face, the two sub-element kinds that are not geometry.
        pts = [(0, 0), (10, 0), (10, 10), (0, 10)]
        for i in range(4):
            a, b = pts[i], pts[(i + 1) % 4]
            sk.addGeometry(Part.LineSegment(FreeCAD.Vector(a[0], a[1], 0),
                                            FreeCAD.Vector(b[0], b[1], 0)), False)
        for i in range(4):
            sk.addConstraint(Sketcher.Constraint("Coincident", i, 2, (i + 1) % 4, 1))
        doc.recompute()

        check("the sketch has constraints to select", len(sk.Constraints) == 4,
              len(sk.Constraints))
        geo_before = len(sk.Geometry)
        constr_before = len(sk.Constraints)

        FreeCADGui.ActiveDocument.setEdit(sk, 0)
        check("the sketch is in edit mode",
              FreeCADGui.ActiveDocument.getInEdit() is not None)

        subs = ["Constraint1"]
        if sk.Shape.Faces:
            subs.append("Face1")
        note("sub-elements under test: %s" % ", ".join(subs))

        for sub in subs:
            for name in COMMANDS:
                FreeCADGui.Selection.clearSelection()
                FreeCADGui.Selection.addSelection(doc.Name, OBJ, sub)
                # The crash was here: the command reads GeoUndef out of the
                # name and hands it straight to isBsplineKnotOrEndPoint.
                FreeCADGui.runCommand(name, 0)
                check("%s survives a %s selection" % (name, sub), True)

        check("the geometry is untouched", len(sk.Geometry) == geo_before,
              len(sk.Geometry))
        check("the constraints are untouched", len(sk.Constraints) == constr_before,
              len(sk.Constraints))

        FreeCADGui.Selection.clearSelection()
        FreeCADGui.ActiveDocument.resetEdit()
        check("edit mode is left cleanly",
              FreeCADGui.ActiveDocument.getInEdit() is None)
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
