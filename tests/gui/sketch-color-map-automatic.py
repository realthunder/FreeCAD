"""A sketch's automatic colours are not mapped onto shapes made from it.

Under AutoColor a sketch's colours follow the preferences: display state,
set by the viewer's theme. With Part's colour mapping on (MapLineColor,
MapPointColor; off by default), a shape made from the sketch copied the
sketch's edge colour into its own -- and saved it -- so a theme's colour
ended up in the derived objects of the file. A sketch now offers its
colours to the mapping only when they are its own (AutoColor off).

Measured on Part Extrude, Part Face, a Compound and a PartDesign Pad with
edge mapping on and SketchEdgeColor green: from a sketch following the
preference their edges stay their own default; from a sketch whose green
was set by hand they take it, as before. Before the change both gave
green.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PART = "User parameter:BaseApp/Preferences/Mod/Part"
VIEW = "User parameter:BaseApp/Preferences/View"
GREEN = (0.0, 1.0, 0.0)
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds):
    import time
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def rectangle(doc, name, body=None):
    import Part
    import Sketcher
    V = FreeCAD.Vector
    sk = (body.newObject if body else doc.addObject)("Sketcher::SketchObject", name)
    p = [V(0, 0, 0), V(20, 0, 0), V(20, 10, 0), V(0, 10, 0)]
    for i in range(4):
        sk.addGeometry(Part.LineSegment(p[i], p[(i + 1) % 4]), False)
    for i in range(4):
        sk.addConstraint(Sketcher.Constraint("Coincident", i, 2, (i + 1) % 4, 1))
    return sk


def edge_colours(obj):
    return sorted(set(tuple(round(c, 2) for c in col[:3])
                      for col in obj.ViewObject.LineColorArray))


def derived(automatic):
    doc = FreeCAD.newDocument("Map%s" % ("Auto" if automatic else "Own"))
    sk = rectangle(doc, "Sketch")
    body = doc.addObject("PartDesign::Body", "Body")
    sk2 = rectangle(doc, "SketchB", body)
    doc.recompute()
    settle(0.3)
    for s in (sk, sk2):
        if not automatic:
            s.ViewObject.AutoColor = False
            s.ViewObject.LineColor = GREEN
    ext = doc.addObject("Part::Extrusion", "Extrude")
    ext.Base = sk
    ext.Dir = FreeCAD.Vector(0, 0, 5)
    ext.Solid = True
    face = doc.addObject("Part::Face", "Face")
    face.Sources = [sk]
    comp = doc.addObject("Part::Compound", "Compound")
    comp.Links = [sk]
    pad = body.newObject("PartDesign::Pad", "Pad")
    pad.Profile = sk2
    pad.Length = 5
    doc.recompute()
    settle(1.0)
    check("the sketch's edge colour is green (%s)" % ("automatic" if automatic else "its own"),
          all(abs(a - b) < 0.01 for a, b in zip(sk.ViewObject.LineColor[:3], GREEN)),
          sk.ViewObject.LineColor[:3])
    return {o.Name: edge_colours(o) for o in (ext, face, comp, pad)}


def run():
    try:
        FreeCADGui.getMainWindow().showMaximized()
        FreeCAD.ParamGet(PART).SetBool("MapLineColor", True)
        FreeCAD.ParamGet(PART).SetBool("MapPointColor", True)
        FreeCAD.ParamGet(VIEW).SetUnsigned("SketchEdgeColor", 0x00FF00FF)
        settle(0.5)
        auto = derived(True)
        own = derived(False)
        green = [round(c, 2) for c in GREEN]
        check("from an automatic sketch no derived edge takes its colour",
              all(tuple(green) not in cols for cols in auto.values()), auto)
        check("from a sketch coloured by hand they do, as before",
              all(cols == [tuple(green)] for cols in own.values()), own)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    for key in ("MapLineColor", "MapPointColor"):
        FreeCAD.ParamGet(PART).RemBool(key)
    FreeCAD.ParamGet(VIEW).RemUnsigned("SketchEdgeColor")
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
