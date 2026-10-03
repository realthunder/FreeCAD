"""A child a Link also shows is still drawn and picked after a reopen.

Found 2026-09-28 (docs/CoinRetirement.md 5.23): a Part holding Box2 and a
Link to the Part. Live, Box2 picks in both occurrences; after save + close
+ reopen it picked in neither, although it stayed drawn -- its display
switch was off. A document load creates the view providers in its own
order, and the Part claimed Box2 before Box2's view provider existed, so
ViewProviderDocumentObject::updateChildren could not register the Part
in Box2's parentSet and, with the child already in claimedChildren, the
retry after the load found nothing changed. isShowable() then judged Box2
by the Link alone -- which draws it through nodes of its own -- and
switched it off. Without the Link the empty parent set counted as
showable, which is why a Part alone never showed it.

Which parent a load reaches first follows allocation order, so a given
scene fails most runs rather than every run; two Link scenes raise the odds.

Claims, each for a Part with a Link to it and for a Part alone (control):
  - after reopen, Box2's display switch is on;
  - a pick sweep finds Box2, in the Part's occurrence and in the Link's;
  - the same objects pick as before the save.

Scored against the tree before the fix: the Link case fails every claim,
the control passes.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(turns=10):
    for _ in range(turns):
        QtCore.QCoreApplication.processEvents()


def sweep(view):
    """{object name: set of occurrence rows} a blind pick sweep finds; a
    row is 'low' below z = 20 (the Part's own), 'high' above (the Link's)."""
    w, h = view.getSize()
    low = view.getPointOnViewport(FreeCAD.Vector(0, 0, 15))[1]
    seen = {}
    for gy in range(0, h, 5):
        for gx in range(0, w, 5):
            info = view.getObjectInfo((gx, gy))
            if info:
                seen.setdefault(info.get("Object"), set()).add("low" if gy < low else "high")
    return seen


def front(doc):
    view = FreeCADGui.getDocument(doc.Name).activeView()
    FreeCADGui.getMainWindow().setActiveWindow(view)
    view.viewFront()
    view.fitAll()
    settle()
    return view


def case(name, with_link, placed=True):
    doc = FreeCAD.newDocument(name)
    if placed:
        doc.addObject("Part::Box", "Box1")
    asm = doc.addObject("App::Part", "Asm")
    box2 = doc.addObject("Part::Box", "Box2")
    if placed:
        box2.Placement.Base = FreeCAD.Vector(30, 0, 0)
    asm.addObject(box2)
    if with_link:
        link = doc.addObject("App::Link", "Link2")
        link.LinkedObject = asm
        link.Placement.Base = FreeCAD.Vector(0, 0, 30 if placed else 20)
    doc.recompute()
    settle()
    live = sweep(front(doc))
    path = os.path.join(OUT, name + ".FCStd")
    doc.saveAs(path)
    FreeCAD.closeDocument(doc.Name)
    settle()
    doc = FreeCAD.openDocument(path)
    settle(30)
    after = sweep(front(doc))
    which = doc.getObject("Box2").ViewObject.SwitchNode.whichChild.getValue()
    tag = ("Link" if placed else "Link, unplaced") if with_link else "control"
    check("%s: Box2's display switch is on after reopen" % tag, which >= 0, which)
    want = {"low", "high"} if with_link else {"low"}
    check("%s: Box2 picks in every occurrence after reopen" % tag,
          after.get("Box2") == want, (after.get("Box2"), want))
    check("%s: the same objects pick as before the save" % tag, after == live,
          (sorted(live.items()), sorted(after.items())))
    FreeCAD.closeDocument(doc.Name)
    settle()


def run():
    try:
        params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        if params.GetInt("RenderCache", 3) != 3:
            note("ABORT render cache is not mode 3")
        else:
            case("ReopenLink", True)
            case("ReopenLinkUnplaced", True, placed=False)
            case("ReopenControl", False)
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
