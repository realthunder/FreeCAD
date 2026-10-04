"""The PartDesign Gui candidates of docs/PartDesignPort.md sec 8 the user took
on 2026-10-01, each as the user meets it:

  - Placement... in an object's context menu (upstream f4167b48c0);
  - "Active body" / "Active object" checkable, checked while active
    (upstream aac3003dcf);
  - a selected ShapeBinder or SubShapeBinder offers the features that take
    it as a profile, a section or a path (upstream ab60695ef9);
  - showing a body whose features are all hidden shows its Tip (upstream
    089d344343);
  - several elements of a reference table deleted at once, e.g. a pipe's
    path edges (upstream f7c03bb929);
  - New Sketch opens the attacher for several references, a face that is
    not planar, a selected sketch and Shift (upstream b43cb81c0a);
  - the Pad panel's direction vector only shows when it means something
    (upstream 873fa449ce);
  - a binder's outline in the datum line color (upstream 5dbb4d7c7e);
  - no legacy workflow prompt: a document with a body-less PartDesign
    feature is not offered a migration by every command (upstream
    5ee788447c).

Run: FreeCAD <this script>, GT_OUT set to a directory; result.txt there.
"""
import ctypes
import os
import traceback

import FreeCAD
import FreeCADGui
import Part
import Sketcher
from PySide import QtCore, QtGui, QtWidgets
from PySide6.QtTest import QTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
V = FreeCAD.Vector
state = {"done": False, "boxes": []}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def events(n=10):
    for _ in range(n):
        QtWidgets.QApplication.processEvents()


def settle(ms):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def sweep():
    """Record and answer any modal box a command raises."""
    w = QtWidgets.QApplication.activeModalWidget()
    if isinstance(w, QtWidgets.QMessageBox):
        state["boxes"].append(w.text())
        note("NOTE message box: " + w.text())
        w.accept()
    elif isinstance(w, QtWidgets.QDialog):
        state["boxes"].append(w.metaObject().className())
        note("NOTE dialog: " + w.metaObject().className())
        w.reject()


def close_panel():
    FreeCADGui.Control.closeDialog()
    settle(300)
    if FreeCADGui.ActiveDocument:
        FreeCADGui.ActiveDocument.resetEdit()
    settle(300)
    # The panels go by deleteLater, which a nested event loop does not run
    QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
    events()


def close_all():
    close_panel()
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    events()


def task_view():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        try:
            if w.metaObject().className() == "Gui::TaskView::TaskView":
                return w
        except RuntimeError:
            pass
    return None


def task_boxes():
    """The label texts of each task box shown: a dialog's, or the watchers'."""
    out = []
    tv = task_view()
    if tv is None:
        return out
    for box in tv.findChildren(QtWidgets.QWidget):
        try:
            if not box.inherits("Gui::TaskView::TaskBox") or box.isHidden():
                continue
            out.append([l.text() for l in box.findChildren(QtWidgets.QWidget)
                        if l.metaObject().className() == "QSint::ActionLabel"])
        except RuntimeError:
            pass
    return out


def select(doc, obj, sub=""):
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(doc.Name, obj.Name, sub)
    events()


def body_with_box(doc, name="Body"):
    body = doc.addObject("PartDesign::Body", name)
    box = body.newObject("PartDesign::AdditiveBox", name + "Box")
    doc.recompute()
    return body, box


def set_active_body(body):
    FreeCADGui.ActiveDocument.ActiveView.setActiveObject("pdbody", body)
    events()


# ---- context menus ------------------------------------------------------

def menu_entries(menu, prefix=""):
    out = []
    for a in menu.actions():
        if a.isSeparator():
            continue
        t = prefix + a.text().replace("&", "")
        out.append((t, a.isCheckable(), a.isChecked()))
        if a.menu():
            out += menu_entries(a.menu(), t + "/")
    return out


def tree_widget():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QTreeWidget):
        try:
            if w.isVisible() and w.metaObject().className() == "Gui::TreeWidget":
                return w
        except RuntimeError:
            pass
    return None


def find_item(tree, label):
    it = QtWidgets.QTreeWidgetItemIterator(tree)
    while it.value():
        if it.value().text(0) == label:
            return it.value()
        it += 1
    return None


def tree_menu(doc, obj):
    """What right-clicking obj in the tree offers."""
    captured = []

    def grab(tries=[0]):
        w = QtWidgets.QApplication.activePopupWidget()
        if isinstance(w, QtWidgets.QMenu):
            captured.extend(menu_entries(w))
            w.close()
        elif tries[0] < 40:
            tries[0] += 1
            QtCore.QTimer.singleShot(50, grab)

    tree = tree_widget()
    select(doc, obj)
    item = find_item(tree, obj.Label)
    if item is None:
        for o in obj.InList:
            p = find_item(tree, o.Label)
            if p:
                tree.expandItem(p)
        events()
        item = find_item(tree, obj.Label)
    tree.scrollToItem(item)
    events()
    pos = tree.visualItemRect(item).center()
    QtCore.QTimer.singleShot(100, grab)
    ev = QtGui.QContextMenuEvent(QtGui.QContextMenuEvent.Mouse, pos,
                                 tree.viewport().mapToGlobal(pos))
    QtWidgets.QApplication.sendEvent(tree.viewport(), ev)
    events()
    return captured


def test_context_menus():
    doc = FreeCAD.newDocument("PDCandMenu")
    body, box = body_with_box(doc)
    part = doc.addObject("App::Part", "Part")
    entries = tree_menu(doc, box)
    check("a feature's context menu offers Placement...",
          any(e[0] == "Placement..." for e in entries),
          [e[0] for e in entries if "lacement" in e[0]])

    set_active_body(body)
    entries = tree_menu(doc, body)
    act = [e for e in entries if e[0] in ("Active body", "Toggle active body")]
    check("an active body's menu item is a checked 'Active body'",
          act == [("Active body", True, True)], act)
    set_active_body(None)
    entries = tree_menu(doc, body)
    act = [e for e in entries if e[0] in ("Active body", "Toggle active body")]
    check("an inactive body's item is unchecked", act == [("Active body", True, False)], act)

    FreeCADGui.ActiveDocument.ActiveView.setActiveObject("part", part)
    events()
    entries = tree_menu(doc, part)
    act = [e for e in entries if e[0] in ("Active object", "Toggle active part")]
    check("an active Part's item is a checked 'Active object'",
          act == [("Active object", True, True)], act)
    close_all()


# ---- the binder watcher, the binder colours ------------------------------

def test_binders():
    doc = FreeCAD.newDocument("PDCandBinder")
    body, box = body_with_box(doc)
    body2 = doc.addObject("PartDesign::Body", "Body2")
    binder = body2.newObject("PartDesign::ShapeBinder", "ShapeBinder")
    binder.Support = [(box, ["Face6"])]
    # named as its command names it: the binder style goes by the name
    sub = body2.newObject("PartDesign::SubShapeBinder", "Binder")
    sub.Support = [(box, ("Face6",))]
    doc.recompute()
    set_active_body(body2)
    for obj, what in ((binder, "ShapeBinder"), (sub, "SubShapeBinder")):
        select(doc, obj)
        settle(600)
        boxes = task_boxes()
        check("a selected %s offers Pad, Pocket and Pipe" % what,
              any("Pad" in b and "Pocket" in b and "Additive pipe" in b for b in boxes),
              boxes)

    orange = (0xFA / 255.0, 0x96 / 255.0, 0.0)
    for obj, what in ((binder, "ShapeBinder"), (sub, "SubShapeBinder")):
        vo = obj.ViewObject
        lc = tuple(vo.LineColor[:3])
        check("a %s's outline is the datum line color" % what,
              all(abs(a - b) < 0.01 for a, b in zip(lc, orange)), lc)
        check("a %s's outline has the usual width" % what,
              vo.LineWidth == FreeCAD.ParamGet(
                  "User parameter:BaseApp/Preferences/View").GetInt("DefaultShapeLineWidth", 2),
              vo.LineWidth)
    close_all()


# ---- a body shown with every feature hidden ------------------------------

def test_body_shows_tip():
    doc = FreeCAD.newDocument("PDCandTip")
    body = doc.addObject("PartDesign::Body", "Body")
    box = body.newObject("PartDesign::AdditiveBox", "Box")
    cyl = body.newObject("PartDesign::AdditiveCylinder", "Cylinder")
    doc.recompute()
    events()
    for f in (box, cyl):
        f.ViewObject.Visibility = False
    body.ViewObject.Visibility = False
    events()
    body.ViewObject.Visibility = True
    events()
    check("showing a body with every feature hidden shows its Tip",
          cyl.ViewObject.Visibility and not box.ViewObject.Visibility,
          (box.ViewObject.Visibility, cyl.ViewObject.Visibility))

    cyl.ViewObject.Visibility = False
    box.ViewObject.Visibility = True
    body.ViewObject.Visibility = False
    events()
    body.ViewObject.Visibility = True
    events()
    check("with another feature visible the Tip stays hidden",
          box.ViewObject.Visibility and not cyl.ViewObject.Visibility,
          (box.ViewObject.Visibility, cyl.ViewObject.Visibility))
    close_all()


# ---- deleting several path edges -----------------------------------------

def test_pipe_edges():
    doc = FreeCAD.newDocument("PDCandPipe")
    body = doc.addObject("PartDesign::Body", "Body")
    prof = body.newObject("Sketcher::SketchObject", "Profile")
    prof.Support = (doc.getObject("XY_Plane"), [""])
    prof.MapMode = "FlatFace"
    prof.addGeometry(Part.Circle(V(0, 0, 0), V(0, 0, 1), 2))
    path = body.newObject("Sketcher::SketchObject", "Path")
    path.Support = (doc.getObject("XZ_Plane"), [""])
    path.MapMode = "FlatFace"
    pts = [V(0, 0, 0), V(0, 10, 0), V(10, 10, 0), V(10, 20, 0)]
    for a, b in zip(pts, pts[1:]):
        path.addGeometry(Part.LineSegment(a, b))
    doc.recompute()
    pipe = body.newObject("PartDesign::AdditivePipe", "Pipe")
    pipe.Profile = prof
    pipe.Spine = (path, ["Edge1", "Edge2", "Edge3"])
    doc.recompute()
    set_active_body(body)
    FreeCADGui.ActiveDocument.setEdit(pipe.Name)
    settle(800)
    table = None
    for t in FreeCADGui.getMainWindow().findChildren(QtWidgets.QTableWidget):
        try:
            if t.isVisible() and any(t.item(0, c) and t.item(0, c).text() == "Edge2"
                                     for c in range(t.columnCount())):
                table = t
        except RuntimeError:
            pass
    if check("the pipe panel shows the path edges", table is not None):
        cols = {table.item(0, c).text(): c for c in range(table.columnCount())
                if table.item(0, c)}
        vp = table.viewport()
        r1 = table.visualRect(table.model().index(0, cols["Edge1"]))
        r2 = table.visualRect(table.model().index(0, cols["Edge2"]))
        QTest.mouseClick(vp, QtCore.Qt.LeftButton, QtCore.Qt.NoModifier, r1.center())
        QTest.mouseClick(vp, QtCore.Qt.LeftButton, QtCore.Qt.ControlModifier, r2.center())
        events()
        table.setFocus()
        QTest.keyClick(table, QtCore.Qt.Key_Delete)
        settle(500)
        check("two picked path edges go at once",
              pipe.Spine[1] == ["Edge3"], pipe.Spine)
    close_all()


# ---- New Sketch ----------------------------------------------------------

VK_SHIFT = 0x10
KEYUP = 0x0002


def new_sketch(doc, shift=False):
    before = set(o.Name for o in doc.Objects)
    if shift:
        ctypes.windll.user32.keybd_event(VK_SHIFT, 0, 0, 0)
    try:
        FreeCADGui.runCommand("PartDesign_NewSketch")
    finally:
        if shift:
            ctypes.windll.user32.keybd_event(VK_SHIFT, 0, KEYUP, 0)
    settle(800)
    new = [doc.getObject(n) for n in set(o.Name for o in doc.Objects) - before]
    sketches = [o for o in new if o.TypeId == "Sketcher::SketchObject"]
    sketch = sketches[0] if sketches else None
    state["tasks"] = [t[:2] for t in task_boxes()]
    attacher = any(b and b[0] == "Sketch attachment" for b in task_boxes())
    editing = FreeCADGui.ActiveDocument.getInEdit()
    in_sketch = bool(editing and sketch and editing.Object == sketch)
    return sketch, attacher, in_sketch


def sketch_case(label, setup, expect_attacher, expect_refs=None, shift=False):
    doc = FreeCAD.newDocument("PDCandSketch")
    body, box = body_with_box(doc)
    cyl = body.newObject("PartDesign::AdditiveCylinder", "Cylinder")
    cyl.Placement.Base = V(30, 0, 0)
    doc.recompute()
    set_active_body(body)
    setup(doc, body, box, cyl)
    state["boxes"] = []
    sketch, attacher, in_sketch = new_sketch(doc, shift)
    refs = []
    if sketch is not None:
        for obj, subs in sketch.Support:
            refs += [obj.Name + "." + s for s in (subs or [""])]
    if expect_attacher:
        ok = sketch is not None and attacher and not in_sketch
        if expect_refs is not None:
            ok = ok and len(refs) == expect_refs
        check("New Sketch, %s: the attacher opens%s" % (
                  label, "" if expect_refs is None else " with %d reference(s)" % expect_refs),
              ok and not state["boxes"],
              dict(sketch=sketch and sketch.Name, attacher=attacher, edit=in_sketch,
                   refs=refs, mode=sketch and sketch.MapMode, boxes=state["boxes"],
                   tasks=state["tasks"]))
    else:
        check("New Sketch, %s: sketched on at once" % label,
              sketch is not None and in_sketch and not attacher
              and sketch.MapMode == "FlatFace",
              dict(sketch=sketch and sketch.Name, attacher=attacher, edit=in_sketch,
                   refs=refs, boxes=state["boxes"]))
    close_all()


def test_new_sketch():
    def one_face(doc, body, box, cyl):
        select(doc, box, "Face6")

    def two_faces(doc, body, box, cyl):
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(doc.Name, box.Name, "Face6")
        FreeCADGui.Selection.addSelection(doc.Name, box.Name, "Face2")
        events()

    def curved(doc, body, box, cyl):
        side = [i for i, f in enumerate(cyl.Shape.Faces, 1)
                if f.Surface.TypeId == "Part::GeomCylinder"]
        select(doc, cyl, "Face%d" % side[0])

    def a_sketch(doc, body, box, cyl):
        sk = body.newObject("Sketcher::SketchObject", "Earlier")
        sk.Support = (doc.getObject("XY_Plane"), [""])
        sk.MapMode = "FlatFace"
        sk.addGeometry(Part.LineSegment(V(0, 0, 0), V(5, 5, 0)))
        sk.addGeometry(Part.LineSegment(V(5, 5, 0), V(10, 0, 0)))
        doc.recompute()
        select(doc, sk)

    sketch_case("one planar face", one_face, False)
    sketch_case("two faces", two_faces, True, 2)
    sketch_case("a cylinder's side", curved, True, 1)
    sketch_case("a sketch selected", a_sketch, True, 0)
    sketch_case("one planar face with Shift", one_face, True, 1, shift=True)


# ---- the Pad panel's direction -------------------------------------------

def test_pad_direction():
    doc = FreeCAD.newDocument("PDCandPad")
    body = doc.addObject("PartDesign::Body", "Body")
    sk = body.newObject("Sketcher::SketchObject", "Sketch")
    sk.Support = (doc.getObject("XY_Plane"), [""])
    sk.MapMode = "FlatFace"
    pts = [V(0, 0, 0), V(10, 0, 0), V(10, 10, 0), V(0, 10, 0)]
    for i in range(4):
        sk.addGeometry(Part.LineSegment(pts[i], pts[(i + 1) % 4]))
    doc.recompute()
    pad = body.newObject("PartDesign::Pad", "Pad")
    pad.Profile = sk
    doc.recompute()
    set_active_body(body)
    FreeCADGui.ActiveDocument.setEdit(pad.Name)
    settle(800)

    def widget(name):
        for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget, name):
            try:
                w.isVisible()
                return w
            except RuntimeError:
                pass
        return None

    group = widget("groupBoxDirection")
    along = widget("checkBoxAlongDirection")
    combo = widget("directionCB")
    if check("the Pad panel is open", group is not None and combo is not None):
        check("along the profile normal: no direction vector, no 'along normal' box",
              group.isHidden() and along.isHidden(),
              (group.isHidden(), along.isHidden()))
        combo.setCurrentIndex(2)
        combo.activated.emit(2)
        settle(300)
        edits = [widget(n) for n in ("XDirectionEdit", "YDirectionEdit", "ZDirectionEdit")]
        check("a custom direction: the vector shows, editable, with the box",
              not group.isHidden() and not along.isHidden()
              and all(e.isEnabled() for e in edits) and pad.UseCustomVector,
              (group.isHidden(), along.isHidden(), [e.isEnabled() for e in edits]))
        combo.setCurrentIndex(0)
        combo.activated.emit(0)
        settle(300)
        check("back to the normal: hidden again",
              group.isHidden() and along.isHidden() and not pad.UseCustomVector,
              (group.isHidden(), along.isHidden(), pad.UseCustomVector))
    close_all()


# ---- no legacy workflow prompt -------------------------------------------

def test_no_legacy_prompt():
    doc = FreeCAD.newDocument("PDCandLegacy")
    # a body-less PartDesign feature, which is what the legacy workflow was;
    # only a document opened from a file is asked about it
    doc.addObject("PartDesign::AdditiveBox", "OldBox")
    doc.recompute()
    path = os.path.join(OUT, "legacy.FCStd")
    doc.saveAs(path)
    FreeCAD.closeDocument(doc.Name)
    events()
    doc = FreeCAD.openDocument(path)
    events()
    FreeCADGui.Selection.clearSelection()
    state["boxes"] = []
    FreeCADGui.runCommand("PartDesign_Pad")
    settle(800)
    migrate = [b for b in state["boxes"] if "old version" in b or "migrat" in b.lower()]
    check("a body-less PartDesign feature: no migration prompt", not migrate, state["boxes"])
    check("and nothing was moved into a body",
          not [o for o in doc.Objects if o.TypeId == "PartDesign::Body"],
          [o.Name for o in doc.Objects])
    close_all()


def run():
    timer = QtCore.QTimer()
    timer.timeout.connect(sweep)
    timer.start(50)
    FreeCADGui.activateWorkbench("PartDesignWorkbench")
    events()
    try:
        for test in (test_context_menus, test_binders, test_body_shows_tip, test_pipe_edges,
                     test_new_sketch, test_pad_direction, test_no_legacy_prompt):
            try:
                test()
            except Exception:
                note("FAIL %s raised:\n%s" % (test.__name__, traceback.format_exc()))
                try:
                    close_all()
                except Exception:
                    pass
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    timer.stop()
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
