"""An automatically sized datum plane comes back from a file at the size it
was saved with, whichever way the file is opened.

Found 2026-09-29 by progressive-load-diff.py on a user file: a datum
plane in Automatic resize mode sizes itself to the visible content of its
Body, and writes the result into its own Length and Width -- App data.
It asked once, from its own updateData during the load, when only the
features restored before it had a visual: an eager open made a plane
saved at 312 x 11.9 150 x 10, a progressive open 175 x 50, and the
Body's bounding box (and the origin sized over the datums) followed.

Scene: a Body with an Automatic datum plane offset along Z, then a Pad
of a 60 x 30 rectangle drawn after it, so the plane saved against the
whole Body is larger than one sized before the Pad; the datum is resized
against the finished Body before the save. Claims, per open mode
(ProgressiveLoad off, then on):
  - the plane's Length and Width are the saved ones;
  - the Body's origin has the size it had before the save.

Scored against the tree before the fix: both modes change the size.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
import Part
import Sketcher
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
V = FreeCAD.Vector

FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
    "AutoSaveEnabled", False)
RENDER = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def wait(seconds):
    t = time.perf_counter()
    while time.perf_counter() - t < seconds:
        QtCore.QCoreApplication.processEvents()


def build(path):
    doc = FreeCAD.newDocument("DatumSize")
    body = doc.addObject("PartDesign::Body", "Body")
    xy = body.Origin.OriginFeatures[3]
    plane = body.newObject("PartDesign::Plane", "DatumPlane")
    plane.AttachmentSupport = [(xy, "")]
    plane.MapMode = "FlatFace"
    plane.AttachmentOffset = FreeCAD.Placement(V(0, 0, 20), FreeCAD.Rotation())
    doc.recompute()
    sk = body.newObject("Sketcher::SketchObject", "Sketch")
    sk.AttachmentSupport = [(xy, "")]
    sk.MapMode = "FlatFace"
    pts = [V(-30, -15, 0), V(30, -15, 0), V(30, 15, 0), V(-30, 15, 0)]
    for i in range(4):
        sk.addGeometry(Part.LineSegment(pts[i], pts[(i + 1) % 4]))
    for i in range(4):
        sk.addConstraint(Sketcher.Constraint("Coincident", i, 2, (i + 1) % 4, 1))
    pad = body.newObject("PartDesign::Pad", "Pad")
    pad.Profile = sk
    pad.Length = 10
    doc.recompute()
    wait(1)
    # Resized against the finished Body: what the file keeps.
    plane.touch()
    doc.recompute()
    wait(1)
    saved = (plane.Length.Value, plane.Width.Value)
    origin = tuple(FreeCADGui.getDocument(doc.Name).getObject(body.Origin.Name).Size)
    doc.saveAs(path)
    FreeCAD.closeDocument(doc.Name)
    wait(0.5)
    return saved, origin


def reopen(path, progressive):
    RENDER.SetBool("ProgressiveLoad", progressive)
    doc = FreeCAD.openDocument(path)
    while FreeCADGui.isBuildingVisuals():
        QtCore.QCoreApplication.processEvents()
    # Past the deferred sizing (300 ms after the load) with room to spare.
    wait(2)
    plane = doc.getObject("DatumPlane")
    body = doc.getObject("Body")
    size = (plane.Length.Value, plane.Width.Value)
    origin = tuple(FreeCADGui.getDocument(doc.Name).getObject(body.Origin.Name).Size)
    FreeCAD.closeDocument(doc.Name)
    wait(0.5)
    return size, origin


def close(a, b):
    return all(abs(x - y) <= 1e-3 * max(1.0, abs(x)) for x, y in zip(a, b))


def run():
    path = os.path.join(OUT, "datum-size.FCStd")
    saved, origin = build(path)
    note("saved plane %s origin %s" % (saved, origin))
    check("the saved plane is sized against the Pad (60 x 30 with margin)",
          saved[0] >= 60 and saved[1] >= 30, saved)
    for progressive in (False, True):
        mode = "progressive" if progressive else "eager"
        size, osize = reopen(path, progressive)
        check("%s open: the datum plane keeps its saved size" % mode,
              close(size, saved), "%s vs saved %s" % (size, saved))
        check("%s open: the Body's origin keeps its saved size" % mode,
              close(osize, origin), "%s vs saved %s" % (osize, origin))


def main():
    try:
        run()
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        RENDER.SetBool("ProgressiveLoad", True)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, main)
