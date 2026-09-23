"""Entering a sketch must not SHOW the datum it is attached to.

Upstream `dc2aec50d4` and `4b50d72769`, taken together (docs/SketcherPort.md).

`ViewProviderSketch::setEdit` runs a TempoVis snippet that, when
ShowSupport is on, shows what the sketch is attached to -- except the
datum it is mapped onto, which is its own support and would only clutter
the view it is being edited in. The exclusion is BY CLASS NAME, and it
named `PartDesign::Plane` alone.

After the datums port an origin plane is an `App::Plane`, so the test
stopped matching and the origin came up with every sketch attached to
one -- which is most of them. A class rename that silently disables a
type test leaves nothing behind to notice: no error, no warning, just a
condition that is never true again. That is what this watches, and why
it is a test rather than a one-line fix left to speak for itself.

Scored against the tree before the fix: the plane comes back visible.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SupportVis"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def run():
    try:
        doc = FreeCAD.newDocument(DOC)
        body = doc.addObject("PartDesign::Body", "Body")
        doc.recompute()
        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        body.addObject(sketch)

        # The origin's planes, whatever class they are in this tree.
        planes = []
        origin = getattr(body, "Origin", None)
        if origin is not None:
            for o in (getattr(origin, "OriginFeatures", None)
                      or getattr(origin, "Group", None) or []):
                planes.append(o)
        note("origin features: %s"
             % [(o.Name, o.TypeId) for o in planes])
        xy = None
        for o in planes:
            if o.Name.startswith("XY") or "XY" in (o.Label or ""):
                xy = o
                break
        if xy is None:
            note("SKIP no XY origin plane found")
            finish()
            return

        for prop in ("AttachmentSupport", "Support"):
            if hasattr(sketch, prop):
                setattr(sketch, prop, [(xy, "")])
                note("attached via %s" % prop)
                break
        sketch.MapMode = "FlatFace"
        doc.recompute()

        sketch.ViewObject.ShowSupport = True
        xy.ViewObject.Visibility = False
        note("before edit: %s visible=%s type=%s"
             % (xy.Name, xy.ViewObject.Visibility, xy.TypeId))

        FreeCADGui.activeDocument().setEdit(sketch)
        QtCore.QCoreApplication.processEvents()
        shown = xy.ViewObject.Visibility
        note("after setEdit: %s visible=%s" % (xy.Name, shown))
        check("the attached origin plane is NOT shown on sketch edit",
              not shown, "%s visible=%s" % (xy.TypeId, shown))
        FreeCADGui.activeDocument().resetEdit()
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
