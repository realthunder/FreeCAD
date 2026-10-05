"""Editing a feature that is not the body's tip swaps nothing in the document.

To edit a PartDesign feature the body has to show it: the edit monitor
(PartDesignGui, beforeEdit) hid the body's other visible solid features
and showed the one about to be edited -- by writing Visibility, so every
view of the document, every served client and every Link to the body
went back to the earlier feature for as long as the panel was open (and,
left by a bare resetEdit, sometimes stayed there). Where the session's
views have a visibility table of their own the same swap is now an entry
of each of them (Gui::Document::setEditVisibility), for the occurrence
being edited, and ends with the session.

A body of a box, a pad standing on it, and a second pad beside the first
that is the tip. The FIRST pad is edited, with the preview on. A Link to
the body is the occurrence that is not being edited.

Claims:
  - no Visibility is written entering the edit: the tip stays the one
    shown feature of the document;
  - the view of the edit draws the first pad's tool and NOT the second
    pad, which was made after it;
  - the Link's occurrence draws the tip, second pad included;
  - with the preview switched off the view draws the first pad itself,
    still without the second;
  - leaving the edit leaves the Visibility it found and the tip drawn.

Mode 3 only; in modes 0-2 the monitor writes Visibility as before.

Scored against the monitor without the session path: the Visibility
claims and the Link's occurrence fail.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PdEditShown"
V = FreeCAD.Vector
state = {"done": False, "shot": 0}

PART = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Part")
PART.SetBool("PreviewOnEdit", True)
PART.SetBool("PreviewWithTransparency", True)
PART.SetBool("EditOnTop", False)
PART.SetUnsigned("PreviewAddColor", 0xFF00FF00)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
    "AutoSaveEnabled", False)


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=300):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


def pixel(view, pt, tag):
    """The backend's own framebuffer at the projection of 3D point pt."""
    state["shot"] += 1
    path = os.path.join(OUT, "%02d-%s.png" % (state["shot"], tag))
    view.saveRenderDump(path, "renderer")
    img = QtGui.QImage(path)
    x, y = view.getPointOnViewport(pt)
    c = QtGui.QColor(img.pixel(int(x), int(img.height() - 1 - y)))
    return (c.red(), c.green(), c.blue())


def tool(c):
    """Magenta: the preview's tinted tool shape."""
    return c[0] - c[1] > 40 and c[2] - c[1] > 40


def solid(c):
    """Green: a feature's own shape."""
    return c[1] - c[0] > 40 and c[1] - c[2] > 40


def preview_switch():
    for box in FreeCADGui.getMainWindow().findChildren(QtWidgets.QCheckBox):
        try:
            if box.text() == "Show preview" and box.isVisible():
                return box
        except RuntimeError:
            pass
    return None


def pad_on(body, doc, name, x0, x1, length):
    sketch = body.newObject("Sketcher::SketchObject", name + "Sketch")
    sketch.Support = (doc.getObject("XY_Plane"), [""])
    sketch.MapMode = "FlatFace"
    corners = [V(x0, 3, 0), V(x1, 3, 0), V(x1, 7, 0), V(x0, 7, 0)]
    for i in range(4):
        sketch.addGeometry(Part.LineSegment(corners[i], corners[(i + 1) % 4]))
    pad = body.newObject("PartDesign::Pad", name)
    pad.Profile = sketch
    pad.Length = length
    return pad, sketch


def run():
    try:
        params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        if params.GetInt("RenderCache", 3) != 3:
            note("ABORT render cache is not mode 3")
            finish()
            return

        doc = FreeCAD.newDocument(DOC)
        body = doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = box.Width = box.Height = 10
        first, sketch1 = pad_on(body, doc, "First", 1, 4, 20)
        second, sketch2 = pad_on(body, doc, "Second", 6, 9, 30)
        link = doc.addObject("App::Link", "Link")
        link.LinkedObject = body
        link.Placement.Base = V(40, 0, 0)
        doc.recompute()
        for obj in (body, box, first, second):
            obj.ViewObject.ShapeColor = (0.0, 0.8, 0.0)
        # As the commands leave a body: its last feature shown alone
        for obj in (box, first, sketch1, sketch2):
            obj.Visibility = False
        gdoc = FreeCADGui.getDocument(DOC)
        view = gdoc.activeView()
        view.setCameraType("Orthographic")
        view.viewFront()
        view.fitAll()
        settle()

        def visibility():
            return (second.Visibility, first.Visibility, box.Visibility, link.Visibility)

        # The first pad's stub above the box, the second's above the
        # first's height, and the second's at the Link's occurrence
        p_first, p_second = V(3.4, 3, 15), V(8.4, 3, 25)
        l_second = p_second + V(40, 0, 0)
        found = visibility()
        base = {"first": pixel(view, p_first, "base"), "second": pixel(view, p_second, "base"),
                "link": pixel(view, l_second, "base")}
        note("baseline %s; Visibility %s" % (base, found))
        if not (all(solid(c) for c in base.values()) and found == (True, False, False, True)):
            note("ABORT the baseline is not the body with its tip shown")
            finish()
            return

        gdoc.setEdit(first, 0)
        settle(1000)
        if not check("the first pad's edit is on", gdoc.getInEdit() is not None):
            finish()
            return
        check("entering the edit writes no Visibility", visibility() == found, visibility())
        f, s = pixel(view, p_first, "edit"), pixel(view, p_second, "edit")
        check("drawn: the tool of the pad in edit", tool(f), f)
        check("drawn: not the pad made after it", not solid(s) and not tool(s), s)
        ls = pixel(view, l_second, "edit")
        check("drawn: the Link's occurrence keeps the tip", solid(ls), ls)

        switch = preview_switch()
        if check("the panel has its preview switch", switch is not None):
            switch.setChecked(False)
            settle(800)
            f, s = pixel(view, p_first, "off"), pixel(view, p_second, "off")
            check("preview off: the pad in edit is drawn itself", solid(f), f)
            check("preview off: still without the one made after it", not solid(s), s)
            check("preview off: and no Visibility written", visibility() == found, visibility())
            ls = pixel(view, l_second, "off")
            check("preview off: the Link's occurrence keeps the tip", solid(ls), ls)
            switch.setChecked(True)
            settle(800)

        gdoc.resetEdit()
        settle(1000)
        check("edit left", gdoc.getInEdit() is None)
        check("leaving leaves the Visibility it found", visibility() == found, visibility())
        f, s = pixel(view, p_first, "after"), pixel(view, p_second, "after")
        check("drawn: the tip again, both pads", solid(f) and solid(s), (f, s))
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        for name in list(FreeCAD.listDocuments().keys()):
            FreeCADGui.getDocument(name).resetEdit()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
