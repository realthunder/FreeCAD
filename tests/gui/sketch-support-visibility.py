"""Editing a sketch hides the ORIGIN it is on, and shows the DATUM it is on.

Upstream `dc2aec50d4` and `4b50d72769`, taken together (docs/SketcherPort.md).

`ViewProviderSketch::setEdit` runs a TempoVis snippet that, when
ShowSupport is on, shows what the sketch is attached to -- minus the
things that would only clutter the view being edited. The exclusion is
BY CLASS NAME, and it named `PartDesign::Plane` alone.

Two separate claims, and the pair is the point:

  - an ORIGIN plane (`App::Plane`) and the LCS stay HIDDEN. After the
    datums port an origin plane is an `App::Plane`, so a test naming
    only `PartDesign::Plane` stopped matching and the origin came up
    with every sketch attached to one -- which is most of them;
  - a datum plane the USER made (`PartDesign::Plane`) is SHOWN. It is
    the reference being sketched on, and upstream deliberately shows it:
    the old exclusion was the pre-core-datums spelling of "origin
    plane", not a second intent. Both trees still create that class from
    the Datum Plane command, so this is live behaviour, not legacy.

A class rename that silently disables a type test leaves nothing behind
to notice: no error, no warning, just a condition never true again. That
is what this watches, and why a one-line fix got a test.

Scored against the tree before the fix: the origin plane comes back
visible.
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

        # And the other half: a datum the USER made is the reference
        # being sketched on, so it IS shown. Without this the fix could
        # be "exclude everything", which hides the origin too and would
        # pass the check above while being wrong.
        datum = doc.addObject("PartDesign::Plane", "DatumPlane")
        body.addObject(datum)
        for prop in ("AttachmentSupport", "Support"):
            if hasattr(datum, prop):
                setattr(datum, prop, [(xy, "")])
                break
        datum.MapMode = "FlatFace"
        doc.recompute()

        sketch2 = doc.addObject("Sketcher::SketchObject", "SketchOnDatum")
        body.addObject(sketch2)
        for prop in ("AttachmentSupport", "Support"):
            if hasattr(sketch2, prop):
                setattr(sketch2, prop, [(datum, "")])
                break
        sketch2.MapMode = "FlatFace"
        doc.recompute()
        sketch2.ViewObject.ShowSupport = True
        datum.ViewObject.Visibility = False
        note("before edit: %s visible=%s type=%s"
             % (datum.Name, datum.ViewObject.Visibility, datum.TypeId))

        FreeCADGui.activeDocument().setEdit(sketch2)
        QtCore.QCoreApplication.processEvents()
        dshown = datum.ViewObject.Visibility
        note("after setEdit: %s visible=%s" % (datum.Name, dshown))
        check("the attached user datum plane IS shown on sketch edit",
              dshown, "%s visible=%s" % (datum.TypeId, dshown))
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
