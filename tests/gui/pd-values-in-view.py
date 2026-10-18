"""The other PartDesign features' values shown and typed in the view (entry 170).

docs/OnViewValueEditing.md 3.2 to 3.4, brought from the fillet and the
chamfer to the rest of PartDesign (the reporter 2026-10-18 10:42: "Implement
the same active handle and on view edit to the rest of partdesign feature").
Each feature in a body of its own, in edit in turn:
  - Box: "Length", "Width", "Height" at the top middle; Tab, 12, Enter:
    Length is 12;
  - Pad: "Length 5 mm" at the top middle, no second length (one side);
    Tab, 7, Enter: Length is 7; the menu offers "Reversed", "Sides" and
    "Type"; "Sides" > "Two sides": Tab, Tab, 3, Enter: Length2 is 3;
  - Revolution: "Angle 180 ..." at the top middle; Tab, 90, Enter: Angle
    is 90; "Reversed" from the menu: Reversed is on;
  - Helix: "Pitch", "Height", "Cone angle" and no "Turns" (the mode
    pitch-height-angle); Tab, 4, Enter: Pitch is 4; the menu's "Mode" >
    the third: "Turns" shown;
  - Hole: "Diameter" and "Depth"; Tab, Tab, 4, Enter: Depth is 4; "Depth
    type" > the second: no "Depth" label;
  - Draft: "Angle 5 ..."; Tab, 3, Enter: Angle is 3; "Reverse pull
    direction": Reversed is on; "Remove Face1" is offered;
  - Thickness: "Thickness 1 mm"; Tab, 2, Enter: Value is 2; "Join type" >
    "Intersection": Join is Intersection;
  - after each edit: no label left in the view, and FreeCAD still runs
    (the value set goes before the handles it holds).

Menus are answered from a timer.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore, QtWidgets
from PySide.QtCore import Qt

try:
    from PySide6 import QtTest
except ImportError:
    from PySide2 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PDValuesInView"

state = {"done": False}


def note(msg):
    with open(RESULT, "a", encoding="utf-8") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def pump(turns=30):
    for _ in range(turns):
        QtWidgets.QApplication.processEvents()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        time.sleep(0.01)


def mw():
    return FreeCADGui.getMainWindow()


def view():
    return FreeCADGui.getDocument(DOC).ActiveView


def viewer_widget():
    return view().graphicsView()


def field_text(node, name):
    value = node.getField(name).get()
    return value.getString() if hasattr(value, "getString") else str(value)


def text_labels():
    from pivy import coin

    search = coin.SoSearchAction()
    search.setName(coin.SbName("OnViewRoot"))
    search.setInterest(coin.SoSearchAction.FIRST)
    search.apply(view().getAuxSceneGraph())
    path = search.getPath()
    if not path:
        return []
    root = path.getTail()
    labels = coin.SoSearchAction()
    labels.setType(coin.SoType.fromName("SoDatumLabel"))
    labels.setInterest(coin.SoSearchAction.ALL)
    labels.setSearchingAll(True)
    labels.apply(root)
    found = []
    for p in labels.getPaths():
        label = p.getTail()
        if field_text(label, "datumtype") == "TEXT":
            found.append(field_text(label, "string").strip().strip('"'))
    return found


def has_label(labels, start):
    return any(t.startswith(start) for t in labels)


def real_key(widget, k):
    QtTest.QTest.keyPress(widget, k, Qt.NoModifier)
    pump(5)
    target = QtWidgets.QApplication.focusWidget() or widget
    QtTest.QTest.keyRelease(target, k, Qt.NoModifier)
    pump(10)


def key(k):
    real_key(viewer_widget(), k)


def tabs(n):
    """Nothing active, then n values on. Escape only with a value active:
    with none it is the view's, which leaves the edit on its release."""
    viewer_widget().setFocus()
    pump(5)
    key(Qt.Key_Tab)
    key(Qt.Key_Escape)
    for _ in range(n):
        key(Qt.Key_Tab)


def open_box():
    for w in mw().findChildren(QtWidgets.QAbstractSpinBox):
        if not w.isVisible() or w.metaObject().className() != "Gui::QuantitySpinBox":
            continue
        parent = w
        while parent is not None and parent.metaObject().className() != "Gui::TaskView::TaskView":
            parent = parent.parentWidget()
        if parent is None:
            return w
    return None


def type_value(digits):
    """Typed at the active value: the first digit opens its label"""
    key(getattr(Qt, "Key_" + digits[0]))
    box = open_box()
    if box is None:
        return False
    for ch in digits[1:]:
        QtTest.QTest.keyClicks(box, ch)
    pump(5)
    real_key(box, Qt.Key_Return)
    pump(80)
    return True


def menu_by_space(choice, sub=None):
    """Space in the view, the menu read and `choice` triggered from a timer
    -- in the sub-menu `sub` when given, by the start of its text or, for an
    int, by its place. Returns [(text, checked, enabled)] of the menu, or
    None if none opened."""
    seen = {"items": None}

    def pick(action, text, index):
        if isinstance(choice, int):
            return index == choice
        return text.startswith(choice) if sub is not None else text == choice

    def answer():
        for w in QtWidgets.QApplication.topLevelWidgets():
            if isinstance(w, QtWidgets.QMenu) and w.isVisible():
                seen["items"] = [(a.text().replace("&", ""), a.isChecked(), a.isEnabled())
                                 for a in w.actions() if not a.isSeparator() and a.text()]
                for i, a in enumerate(w.actions()):
                    text = a.text().replace("&", "")
                    if sub is None and pick(a, text, i):
                        a.trigger()
                        break
                    if sub is not None and text == sub and a.menu() is not None:
                        for j, b in enumerate(a.menu().actions()):
                            if pick(b, b.text().replace("&", ""), j):
                                b.trigger()
                                break
                        break
                w.close()
                return
        QtCore.QTimer.singleShot(100, answer)

    QtCore.QTimer.singleShot(300, answer)
    key(Qt.Key_Space)
    pump(80)
    return seen["items"]


def names(items):
    return [t for t, _, _ in (items or [])]


def sketch(body, name, geos, z=0.0):
    sk = body.newObject("Sketcher::SketchObject", name)
    sk.Placement = FreeCAD.Placement(FreeCAD.Vector(0, 0, z), FreeCAD.Rotation())
    for g in geos:
        sk.addGeometry(g, False)
    return sk


def rect(x0, y0, x1, y1):
    pts = [FreeCAD.Vector(x0, y0, 0), FreeCAD.Vector(x1, y0, 0),
           FreeCAD.Vector(x1, y1, 0), FreeCAD.Vector(x0, y1, 0)]
    return [Part.LineSegment(pts[i], pts[(i + 1) % 4]) for i in range(4)]


def circle(x, y, r):
    return [Part.Circle(FreeCAD.Vector(x, y, 0), FreeCAD.Vector(0, 0, 1), r)]


def edit(obj, title):
    gdoc = FreeCADGui.getDocument(DOC)
    gdoc.setEdit(obj, 0)
    pump(100)
    ok = check(title + ": in edit", gdoc.getInEdit() is not None)
    labels = text_labels()
    note("INFO %s labels: %s" % (title, labels))
    return ok, labels


def leave(title):
    gdoc = FreeCADGui.getDocument(DOC)
    gdoc.resetEdit()
    pump(80)
    check(title + ": out of the edit, no label left", gdoc.getInEdit() is None
          and text_labels() == [], text_labels())


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        mw().activateWindow()
        doc = FreeCAD.newDocument(DOC)

        # Box primitive
        b1 = doc.addObject("PartDesign::Body", "BodyBox")
        prim = b1.newObject("PartDesign::AdditiveBox", "Box")
        prim.Length = prim.Width = prim.Height = 10
        # Pad
        b2 = doc.addObject("PartDesign::Body", "BodyPad")
        sk2 = sketch(b2, "SketchPad", rect(20, 0, 30, 10))
        pad = b2.newObject("PartDesign::Pad", "Pad")
        pad.Profile = sk2
        pad.Length = 5
        # Revolution
        b3 = doc.addObject("PartDesign::Body", "BodyRev")
        sk3 = sketch(b3, "SketchRev", rect(5, 20, 10, 25))
        rev = b3.newObject("PartDesign::Revolution", "Revolution")
        rev.Profile = sk3
        rev.ReferenceAxis = (sk3, ["V_Axis"])
        rev.Angle = 180
        # Helix
        b4 = doc.addObject("PartDesign::Body", "BodyHelix")
        sk4 = sketch(b4, "SketchHelix", circle(40, 0, 1))
        helix = b4.newObject("PartDesign::AdditiveHelix", "Helix")
        helix.Profile = sk4
        helix.ReferenceAxis = (sk4, ["V_Axis"])
        # Hole in a pad
        b5 = doc.addObject("PartDesign::Body", "BodyHole")
        sk5 = sketch(b5, "SketchHoleBase", rect(50, 0, 60, 10))
        pad5 = b5.newObject("PartDesign::Pad", "PadHole")
        pad5.Profile = sk5
        pad5.Length = 5
        sk5b = sketch(b5, "SketchHole", circle(55, 5, 1), z=5.0)
        hole = b5.newObject("PartDesign::Hole", "Hole")
        hole.Profile = sk5b
        # its values want the profile's shape
        doc.recompute()
        hole.DepthType = "Dimension"
        hole.Depth = 3
        # Draft
        b6 = doc.addObject("PartDesign::Body", "BodyDraft")
        box6 = b6.newObject("PartDesign::AdditiveBox", "BoxDraft")
        box6.Length = box6.Width = box6.Height = 10
        box6.Placement.Base = FreeCAD.Vector(70, 0, 0)
        draft = b6.newObject("PartDesign::Draft", "Draft")
        draft.Base = (box6, ["Face1"])
        draft.NeutralPlane = (box6, ["Face5"])
        draft.Angle = 5
        # Thickness
        b7 = doc.addObject("PartDesign::Body", "BodyThick")
        box7 = b7.newObject("PartDesign::AdditiveBox", "BoxThick")
        box7.Length = box7.Width = box7.Height = 10
        box7.Placement.Base = FreeCAD.Vector(90, 0, 0)
        thick = b7.newObject("PartDesign::Thickness", "Thickness")
        thick.Base = (box7, ["Face6"])
        thick.Value = 1
        doc.recompute()
        FreeCADGui.SendMsgToActiveView("ViewFit")
        FreeCADGui.activeDocument().activeView().viewIsometric()
        pump()
        for o in (prim, pad, rev, helix, hole, draft, thick):
            note("INFO %s valid %s" % (o.Name, o.isValid()))

        # -- Box
        ok, labels = edit(prim, "box")
        check("box: Length, Width, Height at the top middle",
              all(has_label(labels, t) for t in ("Length 10", "Width 10", "Height 10")), labels)
        tabs(1)
        type_value("12")
        check("box: Tab, 12, Enter: Length is 12", abs(prim.Length.Value - 12) < 1e-9, prim.Length)
        leave("box")

        # -- Pad
        ok, labels = edit(pad, "pad")
        check("pad: 'Length 5 mm' at the top middle, no second length",
              "Length 5 mm" in labels and not has_label(labels, "Length 2"), labels)
        tabs(1)
        type_value("7")
        check("pad: Tab, 7, Enter: Length is 7", abs(pad.Length.Value - 7) < 1e-9, pad.Length)
        tabs(1)
        items = menu_by_space("Two sides", sub="Sides")
        note("INFO pad menu: %s" % items)
        check("pad: the menu offers Reversed, Sides and Type",
              all(n in names(items) for n in ("Reversed", "Sides", "Type")), items)
        check("pad: Sides > Two sides", pad.SideType == "Two sides", pad.SideType)
        tabs(2)
        type_value("3")
        check("pad: Tab, Tab, 3, Enter: Length2 is 3", abs(pad.Length2.Value - 3) < 1e-9,
              pad.Length2)
        leave("pad")

        # -- Revolution
        ok, labels = edit(rev, "revolution")
        check("revolution: 'Angle 180 ...' at the top middle", has_label(labels, "Angle 180"),
              labels)
        tabs(1)
        type_value("90")
        check("revolution: Tab, 90, Enter: Angle is 90", abs(rev.Angle.Value - 90) < 1e-9,
              rev.Angle)
        tabs(1)
        items = menu_by_space("Reversed")
        note("INFO revolution menu: %s" % items)
        check("revolution: Reversed from the menu", bool(rev.Reversed), rev.Reversed)
        leave("revolution")

        # -- Helix
        ok, labels = edit(helix, "helix")
        check("helix: Pitch, Height, Cone angle at the top middle, no Turns",
              all(has_label(labels, t) for t in ("Pitch", "Height", "Cone angle"))
              and not has_label(labels, "Turns"), labels)
        tabs(1)
        type_value("4")
        check("helix: Tab, 4, Enter: Pitch is 4", abs(helix.Pitch.Value - 4) < 1e-9, helix.Pitch)
        tabs(1)
        items = menu_by_space(2, sub="Mode")
        note("INFO helix menu: %s; mode %s" % (items, helix.Mode))
        labels = text_labels()
        check("helix: the third mode: Turns shown", has_label(labels, "Turns"), labels)
        leave("helix")

        # -- Hole
        ok, labels = edit(hole, "hole")
        check("hole: Diameter and Depth at the top middle",
              has_label(labels, "Diameter") and has_label(labels, "Depth"), labels)
        tabs(2)
        type_value("4")
        check("hole: Tab, Tab, 4, Enter: Depth is 4", abs(hole.Depth.Value - 4) < 1e-9, hole.Depth)
        tabs(1)
        items = menu_by_space(1, sub="Depth type")
        note("INFO hole menu: %s; depth type %s" % (items, hole.DepthType))
        labels = text_labels()
        check("hole: the second depth type: no Depth label",
              hole.DepthType != "Dimension" and not has_label(labels, "Depth"),
              (hole.DepthType, labels))
        leave("hole")

        # -- Draft
        ok, labels = edit(draft, "draft")
        check("draft: 'Angle 5 ...' at the top middle", has_label(labels, "Angle 5"), labels)
        tabs(1)
        type_value("3")
        check("draft: Tab, 3, Enter: Angle is 3", abs(draft.Angle.Value - 3) < 1e-9, draft.Angle)
        tabs(1)
        items = menu_by_space("Reverse pull direction")
        note("INFO draft menu: %s" % items)
        check("draft: 'Remove Face1' offered", "Remove Face1" in names(items), items)
        check("draft: Reverse pull direction", bool(draft.Reversed), draft.Reversed)
        leave("draft")

        # -- Thickness
        ok, labels = edit(thick, "thickness")
        check("thickness: 'Thickness 1 mm' at the top middle", "Thickness 1 mm" in labels, labels)
        tabs(1)
        type_value("2")
        check("thickness: Tab, 2, Enter: Value is 2", abs(thick.Value.Value - 2) < 1e-9,
              thick.Value)
        tabs(1)
        items = menu_by_space("Intersection", sub="Join type")
        note("INFO thickness menu: %s" % items)
        check("thickness: Join type > Intersection", thick.Join == "Intersection", thick.Join)
        leave("thickness")
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.getDocument(DOC).resetEdit()
        pump()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, mw().close)


QtCore.QTimer.singleShot(3000, run)
