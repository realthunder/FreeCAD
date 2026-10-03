"""An edit session hides the occurrence it edits, per view and transiently.

ViewerContext::hideEditedObject (EditingRoot::hideEdited): an edit mode
that hands the editing root a node of its own and leaves the view
provider's geometry where it is hides the ONE occurrence being edited --
Gui::Document::getInEdit's parent and subname -- as a path entry of each
session view's visibility table, ahead of the view's persisted map and
never written into it.

Two 3D views of one document, so the second joins the session. Box2 is
edited (a Python view provider whose edit hangs nothing) through the App::Part
holding it; a Link to that Part shows a second occurrence of Box2 that is
NOT being edited.

Claims:
  - the edited occurrence is gone from both views, picked and drawn;
  - the Link's occurrence of the same object, and Box1, stay;
  - nothing is written into either view's ObjectVisibilities, and a save
    during the edit writes no entry;
  - a view opened during the edit joins it and hides the occurrence too;
  - the hide beats a persisted SHOW of the same path;
  - leaving the edit shows it again everywhere, and the persisted show
    set during the edit is still there.

Mode 3 only: modes 0-2 have no per-view table (hideEditedObject answers
False there, and the edit mode falls back to moving the children).

Scored against the tree before the feature: hideEditedObject does not
exist, and the run ABORTs.
"""
import os
import re
import traceback
import zipfile

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "EditHide"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle():
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


def hit(view, pt):
    """(object name, subname) the view picks at screen point pt, or None."""
    info = view.getObjectInfo((int(pt[0]), int(pt[1])))
    if not info:
        return None
    return (info.get("Object"), info.get("SubName", ""))


def pixel(view, pt, tag):
    """The backend's own framebuffer at the projection of 3D point pt."""
    FreeCADGui.getMainWindow().setActiveWindow(view)
    settle()
    path = os.path.join(OUT, "%s.png" % tag)
    view.saveRenderDump(path, "renderer")
    img = QtGui.QImage(path)
    x, y = view.getPointOnViewport(pt)
    c = QtGui.QColor(img.pixel(int(x), int(img.height() - 1 - y)))
    return (c.red(), c.green(), c.blue())


def differ(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) > 30


def saved_visibilities(path):
    """Every ObjectVisibilities property GuiDocument.xml carries, as text."""
    with zipfile.ZipFile(path) as z:
        xml = z.read("GuiDocument.xml").decode("utf-8", "replace")
    return re.findall(r'<Property name="ObjectVisibilities".*?</Property>', xml, re.S)


class PlainEdit:
    """A view provider whose edit mode is accepted and draws nothing."""

    def attach(self, vobj):
        pass

    def setEdit(self, vobj, mode):
        return True

    def unsetEdit(self, vobj, mode):
        return True

    def dumps(self):
        return None

    def loads(self, state):
        return None


def run():
    try:
        params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        if params.GetInt("RenderCache", 3) != 3:
            note("ABORT render cache is not mode 3")
            finish()
            return

        doc = FreeCAD.newDocument(DOC)
        box1 = doc.addObject("Part::Box", "Box1")
        asm = doc.addObject("App::Part", "Asm")
        # Edited through a Python view provider that hangs nothing, so the
        # edit itself neither covers a pick (a dragger would) nor draws.
        import Part
        box2 = doc.addObject("Part::FeaturePython", "Box2")
        box2.Shape = Part.makeBox(10, 10, 10)
        box2.Placement.Base = FreeCAD.Vector(30, 0, 0)
        box2.ViewObject.Proxy = PlainEdit()
        asm.addObject(box2)
        link = doc.addObject("App::Link", "Link")
        link.LinkedObject = asm
        link.Placement.Base = FreeCAD.Vector(60, 0, 0)
        doc.recompute()
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
            v.viewFront()
            v.fitAll()
        settle()

        # Picked at the projection of each box's centre, per view: a view
        # opened later re-tiles the others, so no screen point is shared.
        # getObjectInfo and getPointOnViewport are both Coin's viewport
        # coordinates.
        p_box1 = FreeCAD.Vector(5, 5, 5)
        p_box2 = FreeCAD.Vector(35, 5, 5)
        p_link = FreeCAD.Vector(95, 5, 5)

        def state_of(view):
            return {k: hit(view, view.getPointOnViewport(p))
                    for k, p in (("box1", p_box1), ("box2", p_box2), ("link", p_link))}

        base1 = state_of(v1)
        base2 = state_of(v2)
        note("baseline v1 %s" % base1)
        note("baseline v2 %s" % base2)
        if not (base1["box1"] and base1["box2"] and base1["link"]):
            note("ABORT baseline picks miss: %s" % base1)
            finish()
            return

        d1 = pixel(v1, p_box2, "v1-base")
        d2 = pixel(v2, p_box2, "v2-base")
        dl = pixel(v1, p_link, "v1-link-base")

        # The edit: Box2 through Asm, started in the first view.
        FreeCADGui.getMainWindow().setActiveWindow(v1)
        settle()
        gdoc.setEdit(asm, 0, "Box2.")
        settle()
        check("the edit is on Box2", gdoc.getInEdit() is not None)
        check("the edit alone hides nothing", state_of(v1) == base1, state_of(v1))

        # A joiner cannot ask; the initiator can.
        check("a joiner may not hide the edited object",
              v2.getViewer().hideEditedObject() is False)
        check("the initiator hides it", v1.getViewer().hideEditedObject() is True)
        settle()

        s1, s2 = state_of(v1), state_of(v2)
        check("picked: the edited Box2 is gone from the initiator", s1["box2"] is None, s1)
        check("picked: and from the view that joined", s2["box2"] is None, s2)
        check("picked: the Link's Box2 stays", s1["link"] == base1["link"]
              and s2["link"] == base2["link"], (s1, s2))
        check("picked: Box1 stays", s1["box1"] == base1["box1"], s1)
        h1 = pixel(v1, p_box2, "v1-hidden")
        h2 = pixel(v2, p_box2, "v2-hidden")
        hl = pixel(v1, p_link, "v1-link-hidden")
        check("drawn: the edited Box2 is gone from the initiator", differ(d1, h1), (d1, h1))
        check("drawn: and from the view that joined", differ(d2, h2), (d2, h2))
        check("drawn: the Link's Box2 is still drawn", not differ(dl, hl), (dl, hl))

        # Transient: in no map, and not in the file.
        check("no entry in the initiator's map", v1.ObjectVisibilities == {},
              v1.ObjectVisibilities)
        check("no entry in the joiner's map", v2.ObjectVisibilities == {},
              v2.ObjectVisibilities)
        check("getObjectVisibility sees no entry",
              v1.getObjectVisibility(asm, "Box2.") is None)
        path = os.path.join(OUT, "edit-hide.FCStd")
        doc.saveAs(path)
        props = saved_visibilities(path)
        check("a save during the edit writes no entry",
              not any("Box2" in p for p in props), props)

        # A view opened now joins the session and hides it as well.
        FreeCADGui.runCommand("Std_ViewCreate")
        settle()
        v3 = gdoc.activeView()
        v3.viewFront()
        v3.fitAll()
        settle()
        s3 = state_of(v3)
        check("a view opened during the edit hides it", s3["box2"] is None, s3)
        check("and still shows the Link's", s3["link"] is not None, s3)

        # The edit hide beats a persisted show of the same path.
        v1.setObjectVisibility(asm, True, "Box2.")
        settle()
        check("a persisted show does not bring the edited Box2 back",
              state_of(v1)["box2"] is None, state_of(v1))

        gdoc.resetEdit()
        settle()
        check("edit left", gdoc.getInEdit() is None)
        s1, s2, s3 = state_of(v1), state_of(v2), state_of(v3)
        check("after the edit: Box2 is back in the initiator", s1["box2"] == base1["box2"], s1)
        check("after the edit: and in the joiner", s2["box2"] == base2["box2"], s2)
        check("after the edit: and in the late joiner", s3["box2"] is not None, s3)
        check("the late joiner picks the Link's Box2 throughout",
              s3["link"] is not None, s3)
        r1 = pixel(v1, p_box2, "v1-restored")
        check("after the edit: Box2 is drawn again", not differ(d1, r1), (d1, r1))
        check("the persisted show set during the edit is still there",
              v1.getObjectVisibility(asm, "Box2.") is True,
              v1.getObjectVisibility(asm, "Box2."))
        v1.ObjectVisibilities = {}
        settle()
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
