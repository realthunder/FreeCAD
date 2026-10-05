"""TempoVis hides and shows in the views of the edit, not in the document.

An edit's visibility automation -- the sketcher hiding what is built on
the sketch and showing what it is attached to -- went through TempoVis
writing Visibility, which is the document's: every view of it followed,
every served client, every Link. Inside an edit session whose views have
a visibility table of their own (render cache mode 3) TempoVis.show and
.hide now put a transient entry in each of those views' tables instead
(Show.SceneDetails.SessionVisibility over Gui.Document.setEditVisibility)
and write nothing.

A body: a pad, a sketch on its top face, and a second pad from that
sketch, which is the tip and the one feature shown; and a Link to the
body. The second sketch is edited with HideDependent and ShowSupport on,
so TempoVis hides what depends on the sketch -- the second pad, and the
Link, which depends on it through the body -- and shows the first pad,
which the sketch is attached to.

Two 3D windows, with the preference PerViewEdit on so that the second
does not join the session: it is the view outside the edit, and draws
the document.

Claims:
  - the document can take the entries (canSetEditVisibility), and holds
    one for each while the edit runs: the second pad and the Link
    hidden, the first pad shown;
  - no Visibility is written;
  - the window of the edit draws the first pad, and neither the second
    nor the Link;
  - the window outside it draws the second pad and the Link throughout;
  - leaving the edit takes the entries back and the first window draws
    the second pad and the Link again, with the Visibility it found.

Mode 3 only; in modes 0-2 TempoVis writes Visibility as before.

Scored against TempoVis without the session path (Show's
session_document() made to answer None): the entries, the Visibility
claims and what the window outside the edit draws fail.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "TempoVisPerView"
V = FreeCAD.Vector
state = {"done": False, "shot": 0}

VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
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
    FreeCADGui.getMainWindow().setActiveWindow(view)
    settle(100)
    state["shot"] += 1
    path = os.path.join(OUT, "%02d-%s.png" % (state["shot"], tag))
    view.saveRenderDump(path, "renderer")
    img = QtGui.QImage(path)
    x, y = view.getPointOnViewport(pt)
    c = QtGui.QColor(img.pixel(int(x), int(img.height() - 1 - y)))
    return (c.red(), c.green(), c.blue())


def solid(c):
    """Green: a feature's own shape."""
    return c[1] - c[0] > 40 and c[1] - c[2] > 40


def rectangle(sketch, a, b):
    corners = [V(a, a, 0), V(b, a, 0), V(b, b, 0), V(a, b, 0)]
    for i in range(4):
        sketch.addGeometry(Part.LineSegment(corners[i], corners[(i + 1) % 4]))


def top_face(feature):
    """The name of the planar face with the highest centre."""
    faces = feature.Shape.Faces
    index = max(range(len(faces)), key=lambda i: faces[i].CenterOfMass.z)
    return "Face%d" % (index + 1)


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/General").SetBool(
            "FitSketchOnEdit", False)
        if VIEW.GetInt("RenderCache", 3) != 3:
            note("ABORT render cache is not mode 3")
            finish()
            return

        doc = FreeCAD.newDocument(DOC)
        body = doc.addObject("PartDesign::Body", "Body")
        sketch1 = body.newObject("Sketcher::SketchObject", "Sketch1")
        sketch1.Support = (doc.getObject("XY_Plane"), [""])
        sketch1.MapMode = "FlatFace"
        rectangle(sketch1, 0, 10)
        pad1 = body.newObject("PartDesign::Pad", "Pad1")
        pad1.Profile = sketch1
        pad1.Length = 10
        doc.recompute()
        sketch2 = body.newObject("Sketcher::SketchObject", "Sketch2")
        sketch2.Support = (pad1, [top_face(pad1)])
        sketch2.MapMode = "FlatFace"
        rectangle(sketch2, 3, 7)
        pad2 = body.newObject("PartDesign::Pad", "Pad2")
        pad2.Profile = sketch2
        pad2.Length = 10
        link = doc.addObject("App::Link", "Link")
        link.LinkedObject = body
        link.Placement.Base = V(40, 0, 0)
        doc.recompute()
        for obj in (body, pad1, pad2):
            obj.ViewObject.ShapeColor = (0.0, 0.8, 0.0)
        # As the commands leave a body: its last feature shown alone
        pad1.Visibility = False
        sketch1.Visibility = False
        sketch2.Visibility = False
        vo = sketch2.ViewObject
        for prop, value in (("HideDependent", True), ("ShowSupport", True),
                            ("ShowLinks", False), ("ShowGrid", False),
                            ("RestoreCamera", False)):
            if hasattr(vo, prop):
                setattr(vo, prop, value)
        gdoc = FreeCADGui.getDocument(DOC)
        view = gdoc.activeView()
        FreeCADGui.runCommand("Std_ViewCreate")
        settle()
        other = gdoc.activeView()
        if len(gdoc.mdiViewsOfType("Gui::View3DInventor")) != 2:
            note("ABORT no second 3D view")
            finish()
            return
        for v in (view, other):
            v.setCameraType("Orthographic")
            v.viewFront()
            v.fitAll()
        settle()

        # The second pad stands on the first: a point on each, and the
        # upper one at the Link's occurrence.
        p_upper, p_lower = V(6, 3, 15), V(1.5, 0, 5)
        l_upper = p_upper + V(40, 0, 0)
        base = {"upper": pixel(view, p_upper, "base"), "lower": pixel(view, p_lower, "base"),
                "link": pixel(view, l_upper, "base"),
                "other": pixel(other, p_upper, "other-base"),
                "other-link": pixel(other, l_upper, "other-base")}
        note("baseline %s; Visibility pad1 %s pad2 %s" % (base, pad1.Visibility, pad2.Visibility))
        if not (all(solid(c) for c in base.values())
                and pad2.Visibility and not pad1.Visibility and link.Visibility):
            note("ABORT the baseline is not the body with its second pad shown")
            finish()
            return
        check("outside an edit the document takes no session entry",
              gdoc.canSetEditVisibility() is False
              and gdoc.setEditVisibility(pad1, True) is False
              and gdoc.getEditVisibility(pad1) is None,
              (gdoc.canSetEditVisibility(), gdoc.getEditVisibility(pad1)))

        # The second window stays out of the session: the view outside it
        VIEW.SetBool("PerViewEdit", True)
        FreeCADGui.getMainWindow().setActiveWindow(view)
        settle()
        gdoc.setEdit(body, 0, "Sketch2.")
        settle(1000)
        FreeCADGui.getMainWindow().setActiveWindow(view)
        settle(100)
        if not check("the sketch's edit is on", gdoc.getInEdit() is not None):
            finish()
            return
        # The sketcher faces its plane; the samples are of the front. Not
        # fitted again: the Link is not drawn here now, and a fit would
        # take its sample out of the frame.
        view.viewFront()
        settle(500)

        def held():
            return tuple(gdoc.getEditVisibility(obj) for obj in (pad2, link, pad1))

        def visibility():
            return (pad2.Visibility, link.Visibility, pad1.Visibility)

        check("in the edit the document takes session entries",
              gdoc.canSetEditVisibility() is True)
        check("TempoVis hid the dependents and showed the support, as entries",
              held() == (False, False, True), held())
        check("and wrote no Visibility", visibility() == (True, True, False), visibility())
        up, low = pixel(view, p_upper, "edit"), pixel(view, p_lower, "edit")
        lup = pixel(view, l_upper, "edit")
        check("drawn: the window of the edit does not draw the second pad", not solid(up), up)
        check("drawn: nor the Link", not solid(lup), lup)
        check("drawn: it draws the first pad", solid(low), low)
        oup, olup = pixel(other, p_upper, "other-edit"), pixel(other, l_upper, "other-edit")
        check("drawn: the window outside the edit keeps the second pad", solid(oup), oup)
        check("drawn: and the Link", solid(olup), olup)

        # A save during the edit has nothing to undo, and writes nothing
        path = os.path.join(OUT, "tempovis-per-view.FCStd")
        doc.saveAs(path)
        check("a save during the edit leaves the entries where they are",
              held() == (False, False, True), held())

        FreeCADGui.getMainWindow().setActiveWindow(view)
        settle(100)
        gdoc.resetEdit()
        settle(1000)
        check("edit left", gdoc.getInEdit() is None)
        check("leaving takes the entries back", held() == (None, None, None), held())
        check("and leaves the Visibility it found",
              visibility() == (True, True, False), visibility())
        view.viewFront()
        settle(500)
        up, lup = pixel(view, p_upper, "after"), pixel(view, l_upper, "after")
        check("drawn: the second pad is back in the first window", solid(up), up)
        check("drawn: and the Link", solid(lup), lup)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    VIEW.SetBool("PerViewEdit", False)
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
