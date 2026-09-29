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

Second scene, the same file's other defect: a datum plane attached to a
SubShapeBinder of a Part's origin plane. The binder is an unbounded face
drawn as a bounded patch; asked for its box before its visual was built,
it answered with the face's +-1e100, which the datum refused to size
over (and the bounding-box cache kept past the build). Claims, per open
mode, five opens each (the drain's timing decides it; one progressive
open in three failed before the fix): the binder's box is finite and the
plane has its saved size; and across all ten the enclosing Part's origin
has one size (the pass re-sized only the Body's origin, and the Part's
had been sized before the plane grew in some opens and after it in
others: 50 in six, 60 in four).
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
NFILL = 300
OPENS = 5

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


def build_unbounded(path):
    doc = FreeCAD.newDocument("DatumUnbounded")
    # Ahead of the binder in the visual queue, so a progressive drain is
    # still building when the binder's box is asked.
    for i in range(NFILL):
        b = doc.addObject("Part::Box", "Fill%d" % i)
        b.Placement.Base = V(200 + (i % 20) * 12, (i // 20) * 12, 0)
    part = doc.addObject("App::Part", "UPart")
    body = doc.addObject("PartDesign::Body", "UBody")
    part.addObject(body)
    xz = [f for f in part.Origin.OriginFeatures if f.Role == "XZ_Plane"][0]
    binder = body.newObject("PartDesign::SubShapeBinder", "UBinder")
    binder.Support = [(part, "%s.%s." % (part.Origin.Name, xz.Name))]
    doc.recompute()
    plane = body.newObject("PartDesign::Plane", "UPlane")
    plane.AttachmentSupport = [(binder, "")]
    plane.MapMode = "FlatFace"
    plane.AttachmentOffset = FreeCAD.Placement(V(0, 0, 8), FreeCAD.Rotation())
    doc.recompute()
    wait(1)
    plane.touch()
    doc.recompute()
    wait(1)
    saved = (plane.Length.Value, plane.Width.Value)
    doc.saveAs(path)
    FreeCAD.closeDocument(doc.Name)
    wait(0.5)
    return saved


def reopen_unbounded(path, progressive):
    RENDER.SetBool("ProgressiveLoad", progressive)
    RENDER.SetInt("ProgressiveLoadBudgetMS", 1)
    doc = FreeCAD.openDocument(path)
    # Anything may ask for a box while the visuals drain -- a fit, the
    # origin or datum sizing: asked here, and asked again after.
    asked = False
    while FreeCADGui.isBuildingVisuals():
        vp = FreeCADGui.getDocument(doc.Name).getObject("UBinder")
        if vp is not None and not asked:
            vp.getBoundingBox()
            asked = True
        QtCore.QCoreApplication.processEvents()
    RENDER.RemInt("ProgressiveLoadBudgetMS")
    wait(2)
    plane = doc.getObject("UPlane")
    size = (plane.Length.Value, plane.Width.Value)
    g = FreeCADGui.getDocument(doc.Name)
    bb = g.getObject("UBinder").getBoundingBox()
    box = (bb.XMin, bb.YMin, bb.ZMin, bb.XMax, bb.YMax, bb.ZMax)
    porigin = tuple(g.getObject(doc.getObject("UPart").Origin.Name).Size)
    FreeCAD.closeDocument(doc.Name)
    wait(0.5)
    return size, box, porigin


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

    upath = os.path.join(OUT, "datum-unbounded.FCStd")
    usaved = build_unbounded(upath)
    note("saved plane over the binder %s" % (usaved,))
    check("the saved plane over the binder is sized to the binder's patch",
          usaved[0] > 10 and usaved[1] > 10, usaved)
    porigins = []
    for progressive in (False, True):
        mode = "progressive" if progressive else "eager"
        for i in range(OPENS):
            size, box, porigin = reopen_unbounded(upath, progressive)
            porigins.append(porigin)
            check("%s open %d: the binder's box is finite" % (mode, i + 1),
                  all(abs(x) < 1e50 for x in box), [round(x, 2) for x in box])
            check("%s open %d: the plane over the binder keeps its saved size"
                  % (mode, i + 1), close(size, usaved),
                  "%s vs saved %s" % (size, usaved))

    check("the Part's origin, over the Body the plane grew in, has one size "
          "in every open", all(close(o, porigins[0]) for o in porigins),
          porigins)


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
