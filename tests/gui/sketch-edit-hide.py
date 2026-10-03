"""A sketch in edit hides the occurrence it edits per view, not everywhere.

Mode 3 (docs/CoinRetirement.md 5.18): entering a sketch no longer sets its
Visibility off (TempoVis tv.hide(ActiveSketch)) nor moves its view
provider's children under the editing root. The sketch hands the editing
root its own edit node and each view of the edit session hides the ONE
occurrence being edited, transiently (ViewerContext::hideEditedObject).

Claims, each read from the backend's own frame (what the shape draws),
the composited frame (what the edit graph draws) and Coin's pick:
  - A, a sketch in a Body: its shape is gone from the frame while the
    edit geometry is drawn in its place, the view provider's children
    never leave its root, its Visibility stays on, no view's map holds an
    entry and a save writes none, and leaving the edit brings it back;
  - B, a sketch in an App::Part that a Link also shows: the Link's
    occurrence is still drawn and picked while the Part's is edited;
  - C, the same sketch edited THROUGH the Link: the Link's occurrence
    goes, the Part's stays.

Scored against the tree before the change: the sketch's Visibility goes
off in edit, so A's "Visibility stays on" and B's "the Link's occurrence
is still drawn" fail.
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
DOC = "SketchEditHide"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(turns=10):
    for _ in range(turns):
        QtCore.QCoreApplication.processEvents()


def ink(view, pt, source, tag, red=False):
    """How many pixels of a 9x9 window around the projection of pt stand
    out from the window's median: a sketch edge through pt has some, an
    empty patch of background none. With red, only the pure red ones:
    the sketches' own shapes are drawn red here, and the edit geometry
    drawn over the same edge is not."""
    FreeCADGui.getMainWindow().setActiveWindow(view)
    settle()
    path = os.path.join(OUT, "%s.png" % tag)
    view.saveRenderDump(path, source)
    img = QtGui.QImage(path)
    if (img.width(), img.height()) != tuple(view.getSize()):
        note("%s: frame %sx%s, view %s" % (tag, img.width(), img.height(), view.getSize()))
    x, y = view.getPointOnViewport(pt)
    x, y = int(x), int(img.height() - 1 - y)
    px = []
    for dy in range(-4, 5):
        for dx in range(-4, 5):
            c = QtGui.QColor(img.pixel(x + dx, y + dy))
            px.append((c.red(), c.green(), c.blue()))
    if red:
        return sum(1 for p in px if p[0] > 180 and p[1] < 90 and p[2] < 90)
    med = sorted(px, key=sum)[len(px) // 2]
    return sum(1 for p in px if sum(abs(a - b) for a, b in zip(p, med)) > 60)


def pick(view, pt):
    info = view.getObjectInfo(tuple(int(v) for v in view.getPointOnViewport(pt)))
    if not info:
        return None
    return (info.get("Object"), info.get("SubName", ""))


def square(doc, name):
    import Part
    import Sketcher
    sk = doc.addObject("Sketcher::SketchObject", name)
    pts = [FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(10, 0, 0),
           FreeCAD.Vector(10, 10, 0), FreeCAD.Vector(0, 10, 0)]
    for i in range(4):
        sk.addGeometry(Part.LineSegment(pts[i], pts[(i + 1) % 4]), False)
    # No internal face until section D asks for one (new sketches have them
    # by default now): the samples sit on an edge, and one pixel on the
    # boundary of a face is the edge or the face by rounding.
    sk.MakeInternals = False
    return sk


def quiet(sketch):
    """No TempoVis hiding of dependents (it would take the Link with it,
    which is not what is under test), no grid over the samples, and the
    shape drawn red so the frame tells it from the edit geometry."""
    vo = sketch.ViewObject
    vo.LineColor = (1.0, 0.0, 0.0)
    vo.PointColor = (1.0, 0.0, 0.0)
    for prop, value in (("HideDependent", False), ("ShowGrid", False),
                        ("RestoreCamera", False), ("ShowSupport", False)):
        if hasattr(vo, prop):
            setattr(vo, prop, value)


def saved_visibilities(path):
    with zipfile.ZipFile(path) as z:
        xml = z.read("GuiDocument.xml").decode("utf-8", "replace")
    return re.findall(r'<Property name="ObjectVisibilities".*?</Property>', xml, re.S)


def run():
    try:
        params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        if params.GetInt("RenderCache", 3) != 3:
            note("ABORT render cache is not mode 3")
            finish()
            return
        FreeCADGui.activateWorkbench("SketcherWorkbench")

        doc = FreeCAD.newDocument(DOC)
        body = doc.addObject("PartDesign::Body", "Body")
        sk1 = square(doc, "Sketch1")
        body.addObject(sk1)
        asm = doc.addObject("App::Part", "Asm")
        asm.Placement.Base = FreeCAD.Vector(30, 0, 0)
        sk2 = square(doc, "Sketch2")
        asm.addObject(sk2)
        link = doc.addObject("App::Link", "Link")
        link.LinkedObject = asm
        link.Placement.Base = FreeCAD.Vector(60, 0, 0)
        doc.recompute()
        gdoc = FreeCADGui.getDocument(DOC)
        for sk in (sk1, sk2):
            quiet(sk)
            sk.ViewObject.Visibility = True
        view = gdoc.activeView()
        view.viewTop()
        view.fitAll()
        settle()

        # A top edge midpoint of each occurrence, in world space: the
        # bottom edges lie on the sketch's own H axis, drawn red in edit.
        e1 = FreeCAD.Vector(5, 10, 0)
        e2 = FreeCAD.Vector(35, 10, 0)
        el = FreeCAD.Vector(65, 10, 0)
        base = {k: ink(view, p, "renderer", "base-" + k, True)
                for k, p in (("sk1", e1), ("sk2", e2), ("link", el))}
        picks = {k: pick(view, p) for k, p in (("sk1", e1), ("sk2", e2), ("link", el))}
        note("baseline ink %s picks %s" % (base, picks))
        if not all(base.values()) or not all(picks.values()):
            note("ABORT the baseline does not show all three occurrences")
            finish()
            return

        # A. Sketch1 in the Body.
        root = sk1.ViewObject.RootNode
        before = root.getNumChildren()
        gdoc.setEdit(body, 0, "Sketch1.")
        settle(30)
        check("A: in edit", gdoc.getInEdit() is not None)
        check("A: the view provider's children never left its root",
              root.getNumChildren() == before, (root.getNumChildren(), before))
        check("A: the sketch's Visibility stays on", sk1.ViewObject.Visibility is True)
        shape = ink(view, e1, "renderer", "A-edit-renderer", True)
        drawn = ink(view, e1, "framebuffer", "A-edit-composite")
        check("A: the sketch's shape is gone from the backend frame", shape == 0,
              (shape, base["sk1"]))
        check("A: the edit geometry is drawn in its place", drawn > 0, drawn)
        p = pick(view, e1)
        check("A: the Body's sketch shape is not picked (the edit geometry may be)",
              p != picks["sk1"], p)
        check("A: the other sketches are still drawn",
              ink(view, e2, "renderer", "A-sk2", True) > 0
              and ink(view, el, "renderer", "A-link", True) > 0)
        check("A: no entry in the view's map", view.ObjectVisibilities == {},
              view.ObjectVisibilities)
        path = os.path.join(OUT, "sketch-edit-hide.FCStd")
        doc.saveAs(path)
        props = saved_visibilities(path)
        check("A: a save during the edit writes no entry",
              not any("Sketch1" in x for x in props), props)
        gdoc.resetEdit()
        # Leaving selects the sketch (a convenience), which is drawn in
        # the selection colour rather than red.
        FreeCADGui.Selection.clearSelection()
        settle(30)
        check("A: edit left", gdoc.getInEdit() is None)
        check("A: the children are all there", root.getNumChildren() == before,
              (root.getNumChildren(), before))
        back = ink(view, e1, "renderer", "A-after", True)
        check("A: the sketch's shape is drawn again", back > 0, back)
        check("A: and picked again", pick(view, e1) == picks["sk1"], pick(view, e1))

        # B. Sketch2 in the Part, which the Link shows as well.
        gdoc.setEdit(asm, 0, "Sketch2.")
        settle(30)
        check("B: in edit", gdoc.getInEdit() is not None)
        s2 = ink(view, e2, "renderer", "B-sk2", True)
        sl = ink(view, el, "renderer", "B-link", True)
        check("B: the Part's occurrence is gone from the frame", s2 == 0, s2)
        check("B: the Link's occurrence is still drawn", sl > 0, sl)
        check("B: and still picked", pick(view, el) == picks["link"], pick(view, el))
        gdoc.resetEdit()
        # Leaving selects the sketch (a convenience), which is drawn in
        # the selection colour rather than red.
        FreeCADGui.Selection.clearSelection()
        settle(30)

        # C. The same sketch edited through the Link.
        gdoc.setEdit(link, 0, "Sketch2.")
        settle(30)
        check("C: in edit", gdoc.getInEdit() is not None)
        s2 = ink(view, e2, "renderer", "C-sk2", True)
        sl = ink(view, el, "renderer", "C-link", True)
        check("C: the Link's occurrence is gone from the frame", sl == 0, sl)
        check("C: the Part's occurrence is still drawn", s2 > 0, s2)
        check("C: and still picked", pick(view, e2) == picks["sk2"], pick(view, e2))
        gdoc.resetEdit()
        # Leaving selects the sketch (a convenience), which is drawn in
        # the selection colour rather than red.
        FreeCADGui.Selection.clearSelection()
        settle(30)
        check("C: both drawn after the edit",
              ink(view, e2, "renderer", "C-after-sk2", True) > 0
              and ink(view, el, "renderer", "C-after-link", True) > 0)

        # D. While the Part's occurrence is edited, the Link's is still in
        # the scene, and an element of it must resolve for a highlight:
        # the sketch's internal face, drawn by its internal view. The
        # sketch used to skip that view whenever it was in edit, which was
        # right only while edit moved its children out of its root.
        from pivy import coin
        sk2.MakeInternals = True
        doc.recompute()
        settle(30)

        def internal_detail():
            path = coin.SoPath()
            path.ref()
            det = link.ViewObject.getDetailPath("Sketch2.InternalFace1", path, True)
            return (det is not None and det.isOfType(coin.SoFaceDetail.getClassTypeId()),
                    path.getLength())

        outside = internal_detail()
        check("D: outside the edit the Link's internal face resolves",
              outside[0], outside)
        gdoc.setEdit(asm, 0, "Sketch2.")
        settle(30)
        check("D: in edit", gdoc.getInEdit() is not None)
        inside = internal_detail()
        check("D: in edit the Link's internal face still resolves",
              inside[0] and inside[1] == outside[1], (inside, outside))
        gdoc.resetEdit()
        FreeCADGui.Selection.clearSelection()
        settle(30)
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
