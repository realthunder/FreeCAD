"""A sketch's highlight edits are announced by the nodes they change.

updateColor() rewrites the edit graph on every (pre)selection change, and
two of its writes never reached the node's notification:

- the point coordinates were opened with startEditing() to lift the
  highlighted points and never closed with finishEditing(), so the node
  kept its node id;
- the SelectedCurveSet had enableNotify(false) called on it -- twice,
  where the second was meant for the PreSelectedCurveSet -- and nothing
  ever turned it back on. Coin still moves a silenced node's own id, but
  stops the notification there: the separator above it and the viewer's
  redraw sensor never hear of the change.

Render cache mode 3 reuses a shape's vertex cache for as long as its node
id stands, and a separator's cache for as long as nothing below it
notifies. Both defects were hidden only because the shared material node
announces its own rewrite in the same pass; once the highlight colours
stop being written into that material, nothing would.

The measurements: the coordinates' node id around selecting a coincident
constraint (which lifts its two points), and whether the two highlight
line sets still pass notification on after a highlight.

Scored against the tree before the fix: the coordinates' id stood still
while two points moved, and the SelectedCurveSet was silenced.

Since the highlight moved into overlays of its own (sketch-highlight-
overlay.py), a highlighted point is lifted as a copy in the overlay's
coordinates rather than in the geometry's: those are the coordinates
measured here now.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "HighlightNotify"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def find_all(root, nodetype):
    """(node, parent) of every match. The paths belong to the action and die
    with it, so the nodes are taken out while it lives."""
    sa = coin.SoSearchAction()
    sa.setType(nodetype.getClassTypeId())
    sa.setInterest(coin.SoSearchAction.ALL)
    sa.setSearchingAll(True)
    sa.apply(root)
    paths = sa.getPaths()
    found = []
    for i in range(paths.getLength()):
        p = paths[i]
        found.append((p.getTail(), p.getNodeFromTail(1) if p.getLength() > 1 else None))
    return found


def edit_nodes(view):
    root = view.getAuxSceneGraph()
    coords = None
    for node, _ in find_all(root, coin.SoCoordinate3):
        if node.getName().getString() == "SelectedPointsCoordinate":
            coords = node
            break
    byname = {}
    for node, _ in find_all(root, coin.SoIndexedLineSet):
        byname[node.getName().getString()] = node
    sets = [byname[n] for n in ("SelectedCurveSet", "PreSelectedCurveSet") if n in byname]
    return coords, sets


def run():
    try:
        import Part
        import Sketcher

        V = FreeCAD.Vector
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        sketch.addGeometry(Part.LineSegment(V(0, 0, 0), V(20, 0, 0)), False)
        sketch.addGeometry(Part.LineSegment(V(20, 0, 0), V(20, 15, 0)), False)
        sketch.addGeometry(Part.LineSegment(V(-20, 10, 0), V(-5, 25, 0)), False)
        sketch.addConstraint(Sketcher.Constraint("Coincident", 0, 2, 1, 1))
        doc.recompute()
        FreeCADGui.activeDocument().setEdit(sketch)
        state["doc"] = doc
        state["view"] = FreeCADGui.activeDocument().activeView()
        state["view"].fitAll()
        QtCore.QTimer.singleShot(2000, select_edge)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def select_edge():
    try:
        coords, sets = edit_nodes(state["view"])
        if not check("the edit graph has its coordinates and two highlight sets",
                     coords is not None and len(sets) == 2,
                     "coords %s, %d sets" % (coords is not None, len(sets))):
            finish()
            return
        state["coords"], state["selected"], state["sets"] = coords, sets[0], sets
        FreeCADGui.Selection.clearSelection()
        QtCore.QCoreApplication.processEvents()
        state["id"] = sets[0].getNodeId()
        state["num"] = sets[0].coordIndex.getNum()
        FreeCADGui.Selection.addSelection(state["doc"].Name, "Sketch", "Edge3")
        QtCore.QTimer.singleShot(500, after_edge)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def after_edge():
    try:
        selected = state["selected"]
        num = selected.coordIndex.getNum()
        check("selecting an edge fills the SelectedCurveSet",
              num > state["num"], "%d -> %d indices" % (state["num"], num))
        for name, node in zip(("Selected", "PreSelected"), state["sets"]):
            check("the %sCurveSet passes its changes on" % name,
                  node.isNotifyEnabled())

        FreeCADGui.Selection.clearSelection()
        QtCore.QCoreApplication.processEvents()
        coords = state["coords"]
        state["id"] = coords.getNodeId()
        state["z"] = [coords.point[i][2] for i in range(coords.point.getNum())]
        FreeCADGui.Selection.addSelection(state["doc"].Name, "Sketch", "Constraint1")
        QtCore.QTimer.singleShot(500, after_constraint)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def after_constraint():
    try:
        coords = state["coords"]
        z = [coords.point[i][2] for i in range(coords.point.getNum())]
        check("selecting a coincident constraint lifts its points",
              len(z) >= 2 and len(z) > len(state["z"]),
              "%d -> %d lifted copies" % (len(state["z"]), len(z)))
        check("the point coordinates announce it (node id moves)",
              coords.getNodeId() != state["id"],
              "%d -> %d" % (state["id"], coords.getNodeId()))
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.activeDocument().resetEdit()
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
