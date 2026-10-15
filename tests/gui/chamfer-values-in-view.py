"""A chamfer's values shown and typed in the view, its edges' modes from the menu.

docs/OnViewValueEditing.md 3.4 (entry 117 step 4, the reporter's "Do
chamfer" of 2026-10-15): as the fillet's -- the default values' labels at
the top middle (the size, and the second size or the angle by the type),
handles on every edge, the root button's menu: the mode ("Default size",
"Own size", "Own two sizes", "Own size and angle"), "Flip direction",
"Group", "Remove". An angle has a rotation handle. Groups as the fillet's
(6.5): sizes and angle mirrored, the flip each edge's own.

A 10 mm PartDesign box, a chamfer on Edge1 and Edge2, Size 1, equal
distance. In edit. Claims:
  - one arrow per edge, no rotation handle; "Size 1 mm" at the top middle;
  - Tab, 2, Enter: Size is 2;
  - type set to "Distance and Angle": an arrow and a rotation handle per
    edge, labels "Size 2 mm" and "Angle 45 ...";
  - Tab, Tab, 30, Enter: Angle is 30;
  - Tab, Space: Edge1's menu, "Default size" checked, "Flip direction"
    enabled; "Own two sizes": Edge1 has its own sizes 2 and 2, group 1;
    three arrows and one rotation handle;
  - Tab, Space, "Own size and angle": Edge2 has size 2 and angle 30, group 2;
  - Edge2's size (Tab x5), Space, Group "1": Edge2 takes group 1's values;
  - Edge1's first size (Tab x3), 3, Enter: both edges' sizes are 3;
  - Edge1's menu, "Flip direction": Edge1 flipped, Edge2 not;
  - still in edit, the chamfer valid.

Runs with QT_QPA_PLATFORM=offscreen; menus are answered from a timer.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets
from PySide.QtCore import Qt

try:
    from PySide6 import QtTest
except ImportError:
    from PySide2 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ChamferValuesInView"

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


def shown(type_name):
    """How many dragger containers of a type the view has switched on"""
    from pivy import coin

    root = view().getViewer().getSoRenderManager().getSceneGraph()
    search = coin.SoSearchAction()
    search.setType(coin.SoType.fromName(type_name))
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(root)
    count = 0
    for p in search.getPaths():
        if field_text(p.getTail(), "visible") in ("TRUE", "1"):
            count += 1
    return count


def handles():
    return (shown("SoLinearDraggerContainer"), shown("SoRotationDraggerContainer"))


def text_labels():
    from pivy import coin

    search = coin.SoSearchAction()
    search.setName(coin.SbName("OnViewRoot"))
    search.setInterest(coin.SoSearchAction.FIRST)
    search.apply(view().getAuxSceneGraph())
    path = search.getPath()
    if not path:
        return []
    # the tail before the action that owns the path goes
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
    """Typed at the active handle: the first digit opens its label"""
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
    -- in the sub-menu `sub` when given, by the start of its text. Returns
    [(text, checked, enabled)] of the menu, or None if none opened."""
    seen = {"items": None}

    def answer():
        for w in QtWidgets.QApplication.topLevelWidgets():
            if isinstance(w, QtWidgets.QMenu) and w.isVisible():
                seen["items"] = [(a.text().replace("&", ""), a.isChecked(), a.isEnabled())
                                 for a in w.actions() if not a.isSeparator() and a.text()]
                for a in w.actions():
                    text = a.text().replace("&", "")
                    if sub is None and text == choice:
                        a.trigger()
                        break
                    if sub is not None and text == sub and a.menu() is not None:
                        for b in a.menu().actions():
                            if b.text().replace("&", "").startswith(choice):
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


def info(chamfer, edge):
    try:
        size, size2, angle, flip = chamfer.evalExpression("ChamferInfo.%s" % edge)
    except Exception:
        return None
    return (round(float(size), 6), round(float(size2), 6), round(float(angle), 6), bool(flip))


def group(chamfer, edge):
    try:
        return chamfer.evalExpression("ChamferInfo.%s.Group" % edge)
    except Exception:
        return 0


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        mw().activateWindow()
        doc = FreeCAD.newDocument(DOC)
        body = doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = box.Width = box.Height = 10
        doc.recompute()
        chamfer = body.newObject("PartDesign::Chamfer", "Chamfer")
        chamfer.Base = (box, ["Edge1", "Edge2"])
        chamfer.Size = 1
        doc.recompute()
        FreeCADGui.SendMsgToActiveView("ViewFit")
        FreeCADGui.activeDocument().activeView().viewIsometric()
        pump()

        gdoc = FreeCADGui.getDocument(DOC)
        gdoc.setEdit(chamfer, 0)
        pump(80)
        check("the chamfer is in edit", gdoc.getInEdit() is not None)
        check("equal size: one arrow per edge, no rotation handle", handles() == (2, 0), handles())
        labels = text_labels()
        check("'Size 1 mm' at the top middle", "Size 1 mm" in labels, labels)

        tabs(1)
        type_value("2")
        check("Tab, 2, Enter: Size is 2", abs(chamfer.Size.Value - 2.0) < 1e-9, chamfer.Size)

        chamfer.ChamferType = "Distance and Angle"
        doc.recompute()
        pump(80)
        check("distance and angle: an arrow and a rotation handle per edge",
              handles() == (2, 2), handles())
        labels = text_labels()
        check("labels 'Size 2 mm' and 'Angle 45 ...'",
              "Size 2 mm" in labels and any(t.startswith("Angle 45") for t in labels), labels)

        tabs(2)
        type_value("30")
        check("Tab, Tab, 30, Enter: Angle is 30", abs(chamfer.Angle.Value - 30.0) < 1e-9,
              chamfer.Angle)

        tabs(1)
        items = menu_by_space("Own two sizes")
        note("INFO Edge1's menu: %s" % items)
        names = [t for t, _, _ in (items or [])]
        check("the menu offers the four modes, 'Default size' checked",
              items is not None and ("Default size", True, True) in items
              and all(m in names for m in ("Own size", "Own two sizes", "Own size and angle")),
              items)
        check("'Flip direction' and 'Remove' are there, Flip enabled",
              items is not None and ("Flip direction", False, True) in items and "Remove" in names,
              items)
        check("'Own two sizes': Edge1 has sizes 2 and 2 of its own, group 1",
              info(chamfer, "Edge1") == (2.0, 2.0, 0.0, False) and group(chamfer, "Edge1") == 1,
              (info(chamfer, "Edge1"), group(chamfer, "Edge1")))
        check("three arrows and one rotation handle", handles() == (3, 1), handles())

        tabs(1)
        menu_by_space("Own size and angle")
        check("'Own size and angle': Edge2 has size 2, angle 30, group 2",
              info(chamfer, "Edge2") == (2.0, 0.0, 30.0, False) and group(chamfer, "Edge2") == 2,
              (info(chamfer, "Edge2"), group(chamfer, "Edge2")))

        # Size, Angle, Edge1's S1 and S2, then Edge2's S
        tabs(5)
        menu_by_space("1 ", sub="Group")
        check("joined group 1: Edge2 takes its sizes",
              group(chamfer, "Edge2") == 1 and info(chamfer, "Edge2") == (2.0, 2.0, 0.0, False),
              (group(chamfer, "Edge2"), info(chamfer, "Edge2")))

        tabs(3)
        type_value("3")
        check("Edge1's first size typed 3: both edges' sizes are 3",
              info(chamfer, "Edge1")[0] == 3.0 and info(chamfer, "Edge2")[0] == 3.0,
              (info(chamfer, "Edge1"), info(chamfer, "Edge2")))

        tabs(3)
        menu_by_space("Flip direction")
        check("Edge1 flipped, Edge2 not",
              info(chamfer, "Edge1")[3] is True and info(chamfer, "Edge2")[3] is False,
              (info(chamfer, "Edge1"), info(chamfer, "Edge2")))

        check("still in edit", gdoc.getInEdit() is not None)
        check("the chamfer is valid", chamfer.isValid() and chamfer.Shape.isValid())
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
