"""A PartDesign feature's edit preview belongs to the edit session's views.

The fork's preview-on-edit (PartParams PreviewOnEdit) shows, while a
feature's panel is open, the feature's BASE with the tinted tool shape
over it in place of the feature, whose own recompute is paused. It did
that in the document: the tool was hung as a child of the base feature's
switch -- the one scene every view and every served client draws -- and
the two Visibility properties were swapped. Now the tool hangs in the
session's editing root (Gui::EditingRoot::addSessionNode) and each view
of the session hides the feature and shows the base through its own
visibility table, transiently (setVisibilitySwaps): no Visibility is
written, and whatever is not in the session keeps the feature as it was.

A body of a 10 mm box and a pad standing 10 mm out of its top; the pad
is edited. Two 3D views of the document, so the second joins the
session, and a Link to the body, which shows a second occurrence of the
pad that is NOT the one being edited -- what a view outside the session
sees. The preview colour is set to magenta and the body is green, so a
pixel says which of the two is drawn there.

A pad rather than a primitive: a primitive never pauses its recompute
(FeaturePrimitive::setPauseRecompute is empty), so its shape follows the
panel everywhere whatever the preview does.

It is also why the Link survives the edit: a PRIMITIVE's panel, which
shows the body's origin while it is open, turns a Link to the body off,
preview or not, and nothing turns it back on (found writing this,
2026-10-05; not this test's subject). Should a pad's ever do the same
the test says so in its result and shows the Link again.

Claims:
  - entering the edit writes neither Visibility, and no view's map;
  - both views of the session draw the tool over the edited occurrence,
    and pick nothing where only the tool is (the base has no geometry
    there, and the tool is not selectable);
  - the Link's occurrence still draws and picks the feature, with no tool;
  - a change made in the panel reaches the tool in the session's views
    and nothing else (the feature's shape is held back);
  - a view opened during the edit joins and draws the tool;
  - the panel's preview switch takes the tool away and brings it back;
  - leaving the edit leaves no tool, no entry and the Visibility it found.

Mode 3 only: modes 0-2 have no per-view table, and the preview falls
back to the document's scene there.

Scored against the tree before the change: the Visibility claims, the
Link's occurrence and its pick fail.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PdPreview"
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


def hit(view, pt):
    """What the view picks at the projection of 3D point pt, as text."""
    x, y = view.getPointOnViewport(pt)
    info = view.getObjectInfo((int(x), int(y)))
    if not info:
        return ""
    parent = info.get("ParentObject")
    return "%s|%s|%s|%s" % (parent.Name if parent else "", info.get("SubName", ""),
                            info.get("Object", ""), info.get("Component", ""))


def pixel(view, pt, tag):
    """The backend's own framebuffer at the projection of 3D point pt."""
    FreeCADGui.getMainWindow().setActiveWindow(view)
    settle(100)
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


def differ(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) > 30


def preview_switch():
    for box in FreeCADGui.getMainWindow().findChildren(QtWidgets.QCheckBox):
        try:
            if box.text() == "Show preview" and box.isVisible():
                return box
        except RuntimeError:
            pass
    return None


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
        sketch = body.newObject("Sketcher::SketchObject", "Sketch")
        sketch.Support = (doc.getObject("XY_Plane"), [""])
        sketch.MapMode = "FlatFace"
        corners = [V(3, 3, 0), V(7, 3, 0), V(7, 7, 0), V(3, 7, 0)]
        for i in range(4):
            sketch.addGeometry(Part.LineSegment(corners[i], corners[(i + 1) % 4]))
        pad = body.newObject("PartDesign::Pad", "Pad")
        pad.Profile = sketch
        # Tall while the views are framed, so that the length the edit
        # gives it later is on screen
        pad.Length = 40
        link = doc.addObject("App::Link", "Link")
        link.LinkedObject = body
        link.Placement.Base = V(40, 0, 0)
        doc.recompute()
        green = (0.0, 0.8, 0.0)
        for obj in (body, box, pad):
            obj.ViewObject.ShapeColor = green
        # As the commands leave a body: its last feature shown alone
        box.Visibility = False
        sketch.Visibility = False
        gdoc = FreeCADGui.getDocument(DOC)

        v1 = gdoc.activeView()
        FreeCADGui.runCommand("Std_ViewCreate")
        settle()
        v2 = gdoc.activeView()
        if len(gdoc.mdiViewsOfType("Gui::View3DInventor")) != 2:
            note("ABORT no second 3D view")
            finish()
            return
        for v in (v1, v2):
            v.setCameraType("Orthographic")
            v.viewFront()
            v.fitAll()
        settle()
        pad.Length = 20
        doc.recompute()
        settle()
        volume = pad.Shape.Volume

        # The stub above the box, the stretch the edit adds to it, and the
        # box's front face -- of the edited occurrence and of the Link's.
        p_stub, p_more, p_face = V(6, 5, 15), V(6, 5, 30), V(2, 0, 5)
        off = V(40, 0, 0)
        l_stub, l_more = p_stub + off, p_more + off

        base = {
            "stub1": pixel(v1, p_stub, "v1-base"),
            "stub2": pixel(v2, p_stub, "v2-base"),
        }
        base["more1"] = pixel(v1, p_more, "v1-base")
        base["lstub"] = pixel(v1, l_stub, "v1-base")
        base["lmore"] = pixel(v1, l_more, "v1-base")
        note("baseline %s" % base)
        # A face of the box picks as the Box's whichever feature is shown
        # (the body names the feature an element comes from), so the pick
        # that tells the feature from its base is on the stub.
        note("baseline picks %s | %s" % (hit(v1, p_stub), hit(v1, l_stub)))
        if not (solid(base["stub1"]) and solid(base["lstub"])
                and not differ(base["more1"], base["lmore"])
                and "Pad" in hit(v1, p_stub) and "Pad" in hit(v1, l_stub)):
            note("ABORT the baseline is not the scene the claims are about")
            finish()
            return

        FreeCADGui.getMainWindow().setActiveWindow(v1)
        settle()
        gdoc.setEdit(pad, 0)
        settle(1000)
        if not check("the edit is on", gdoc.getInEdit() is not None):
            finish()
            return
        switch = preview_switch()
        check("the panel has its preview switch, on",
              switch is not None and switch.isChecked())
        if not link.Visibility:
            note("NOTE entering the edit turned the Link's Visibility off")
            link.Visibility = True
            settle()

        check("entering the edit leaves the feature's Visibility alone",
              pad.Visibility is True, pad.Visibility)
        check("and the base's", box.Visibility is False, box.Visibility)
        check("and writes no view's map",
              v1.ObjectVisibilities == {} and v2.ObjectVisibilities == {},
              (v1.ObjectVisibilities, v2.ObjectVisibilities))

        s1 = pixel(v1, p_stub, "v1-edit")
        s2 = pixel(v2, p_stub, "v2-edit")
        check("drawn: the tool, in the view the edit started in", tool(s1), s1)
        check("drawn: and in the view that joined", tool(s2), s2)
        ls = pixel(v1, l_stub, "v1-edit")
        check("drawn: the Link's occurrence keeps the feature",
              solid(ls) and not differ(ls, base["lstub"]), (ls, base["lstub"]))
        h1, h2, hl = hit(v1, p_stub), hit(v2, p_stub), hit(v1, l_stub)
        check("picked: nothing where only the tool is, in the initiator", h1 == "", h1)
        check("picked: nor in the joiner", h2 == "", h2)
        check("picked: the box under it is still there",
              "Box" in hit(v1, p_face), hit(v1, p_face))
        check("picked: the Link's occurrence is still the feature's", "Pad" in hl, hl)

        # What the panel does for a changed value
        doc.openTransaction("Edit Pad")
        pad.Length = 40
        pad.recompute(True)
        settle(500)
        check("the change is held back from the shape",
              abs(pad.Shape.Volume - volume) < 1e-6, (pad.Shape.Volume, volume))
        m1 = pixel(v1, p_more, "v1-taller")
        m2 = pixel(v2, p_more, "v2-taller")
        lm = pixel(v1, l_more, "v1-taller")
        check("drawn: the tool follows the change in the initiator", tool(m1), m1)
        check("drawn: and in the joiner", tool(m2), m2)
        check("drawn: nothing of it at the Link's occurrence",
              not differ(lm, base["lmore"]), (lm, base["lmore"]))

        FreeCADGui.runCommand("Std_ViewCreate")
        settle()
        v3 = gdoc.activeView()
        v3.setCameraType("Orthographic")
        v3.viewFront()
        v3.fitAll()
        settle()
        m3 = pixel(v3, p_more, "v3-joined")
        l3 = pixel(v3, l_stub, "v3-joined")
        check("a view opened during the edit draws the tool", tool(m3), m3)
        check("and the Link's feature", solid(l3), l3)
        check("and picks nothing where only the tool is",
              hit(v3, p_stub) == "", hit(v3, p_stub))

        # The switch: off makes the feature and shows it, everywhere
        switch = preview_switch()
        if check("the preview switch is still there", switch is not None):
            switch.setChecked(False)
            settle(800)
            check("preview off: the feature is made",
                  pad.Shape.Volume > volume + 1.0, (pad.Shape.Volume, volume))
            o1 = pixel(v1, p_more, "v1-off")
            o2 = pixel(v2, p_more, "v2-off")
            ol = pixel(v1, l_more, "v1-off")
            check("preview off: the initiator draws the feature", solid(o1), o1)
            check("preview off: and the joiner", solid(o2), o2)
            check("preview off: and the Link's occurrence", solid(ol), ol)
            check("preview off: the pick is the feature's again",
                  "Pad" in hit(v1, p_stub), hit(v1, p_stub))
            check("preview off: Visibility as it was",
                  pad.Visibility is True and box.Visibility is False,
                  (pad.Visibility, box.Visibility))

            switch.setChecked(True)
            settle(800)
            n1 = pixel(v1, p_more, "v1-on")
            n2 = pixel(v2, p_more, "v2-on")
            n3 = pixel(v3, p_more, "v3-on")
            nl = pixel(v1, l_more, "v1-on")
            check("preview on again: the tool is back in the initiator", tool(n1), n1)
            check("preview on again: and in both joiners", tool(n2) and tool(n3), (n2, n3))
            check("preview on again: the Link's occurrence keeps the feature",
                  solid(nl), nl)
            check("preview on again: Visibility as it was",
                  pad.Visibility is True and box.Visibility is False,
                  (pad.Visibility, box.Visibility))

        gdoc.resetEdit()
        settle(1000)
        check("edit left", gdoc.getInEdit() is None)
        e1 = pixel(v1, p_more, "v1-after")
        e2 = pixel(v2, p_more, "v2-after")
        e3 = pixel(v3, p_more, "v3-after")
        el = pixel(v1, l_more, "v1-after")
        check("after the edit: no tool, the feature, in every view",
              solid(e1) and solid(e2) and solid(e3), (e1, e2, e3))
        check("after the edit: and at the Link's occurrence", solid(el), el)
        check("after the edit: the pick is the feature's",
              "Pad" in hit(v1, p_stub) and "Pad" in hit(v2, p_stub),
              (hit(v1, p_stub), hit(v2, p_stub)))
        check("after the edit: Visibility as it was found",
              pad.Visibility is True and box.Visibility is False,
              (pad.Visibility, box.Visibility))
        check("after the edit: no view's map holds an entry",
              v1.ObjectVisibilities == {} and v2.ObjectVisibilities == {}
              and v3.ObjectVisibilities == {},
              (v1.ObjectVisibilities, v2.ObjectVisibilities, v3.ObjectVisibilities))

        # A second session starts clean: nothing of the first is in it
        FreeCADGui.getMainWindow().setActiveWindow(v1)
        settle()
        gdoc.setEdit(box, 0)
        settle(1000)
        if check("a second edit, of the base, is on", gdoc.getInEdit() is not None):
            b1 = pixel(v1, p_more, "v1-second")
            check("the second session shows nothing of the first's tool",
                  not tool(b1), b1)
            gdoc.resetEdit()
            settle(1000)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        for name in list(FreeCAD.listDocuments().keys()):
            gdoc = FreeCADGui.getDocument(name)
            if gdoc and gdoc.getInEdit():
                gdoc.resetEdit()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
