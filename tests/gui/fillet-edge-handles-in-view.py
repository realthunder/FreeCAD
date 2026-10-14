"""A fillet's edges each have an arrow in the view, with a menu for the edge's mode.

docs/HandsOnQueue.md entry 117, docs/OnViewValueEditing.md section 4 step 2:
one arrow on every edge of the fillet; an edge that takes the default radius
drives it, a customised edge has an arrow per point of its own, whose label
shows beside the arrow while it is active; the root of each arrow opens a menu
-- "Default radius", "One radius", "Two radii", "Remove" -- and Space opens
the active handle's menu as the root does.

A 10 mm PartDesign box, a fillet on Edge1 and Edge2 at 1 mm, in edit. Claims:
  - two arrows are shown, one per edge;
  - Tab, Space: the menu offers the three modes, "Default radius" checked,
    and Remove;
  - "Two radii" from it: Edge1 has two points at 0 and 1, both 1 mm, and
    three arrows are shown;
  - Tab (the first point's value, the default still active), 2, Enter: Edge1's first point is
    2 mm, and its label "R1 2 mm" is shown beside the arrow;
  - Space there: "Two radii" checked; "Default radius" from it: Edge1 has no
    points of its own, two arrows again;
  - Tab, Space, "Remove": the fillet has Edge2 alone, one arrow.

Runs with QT_QPA_PLATFORM=offscreen; menus are answered from a timer. With
GT_PICTURE set it also saves the view with the two radii's arrows.
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
DOC = "FilletEdgeHandlesInView"

state = {"done": False, "menu": None}


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


def shown_arrows():
    """How many linear dragger containers of the view are switched on"""
    from pivy import coin

    root = view().getViewer().getSoRenderManager().getSceneGraph()
    search = coin.SoSearchAction()
    search.setType(coin.SoType.fromName("SoLinearDraggerContainer"))
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(root)
    count = 0
    for p in search.getPaths():
        if field_text(p.getTail(), "visible") in ("TRUE", "1"):
            count += 1
    return count


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
    search = labels
    found = []
    for p in search.getPaths():
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


def type_into_box(text):
    box = open_box()
    if box is None:
        return None
    for ch in text:
        QtTest.QTest.keyClicks(box, ch)
    pump(5)
    seen = box.text()
    real_key(box, Qt.Key_Return)
    pump(60)
    return seen


def menu_by_space(choice):
    """Space in the view, the menu read and `choice` triggered from a timer.
    Returns [(text, checked, enabled)] of the menu, or None if none opened."""
    seen = {"items": None}

    def answer():
        for w in QtWidgets.QApplication.topLevelWidgets():
            if isinstance(w, QtWidgets.QMenu) and w.isVisible():
                items = [(a.text().replace("&", ""), a.isChecked(), a.isEnabled())
                         for a in w.actions() if not a.isSeparator() and a.text()]
                seen["items"] = items
                for a in w.actions():
                    if a.text().replace("&", "") == choice:
                        a.trigger()
                        break
                w.close()
                return
        QtCore.QTimer.singleShot(100, answer)

    QtCore.QTimer.singleShot(300, answer)
    key(Qt.Key_Space)
    pump(80)
    return seen["items"]


def segments(fillet):
    return [[tuple(round(v, 6) for v in s) for s in edge] for edge in fillet.Segments]


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
        fillet = body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (box, ["Edge1", "Edge2"])
        fillet.Radius = 1
        doc.recompute()
        FreeCADGui.SendMsgToActiveView("ViewFit")
        FreeCADGui.activeDocument().activeView().viewIsometric()
        pump()

        gdoc = FreeCADGui.getDocument(DOC)
        gdoc.setEdit(fillet, 0)
        pump(80)
        check("the fillet is in edit", gdoc.getInEdit() is not None)
        check("two arrows, one per edge", shown_arrows() == 2, shown_arrows())

        viewer_widget().setFocus()
        pump(5)
        key(Qt.Key_Tab)
        items = menu_by_space("Two radii")
        note("INFO the menu: %s" % items)
        names = [t for t, _, _ in (items or [])]
        check("Tab, Space: the menu offers the modes and Remove",
              items is not None and all(n in names for n in
                                        ("Default radius", "One radius", "Two radii", "Remove")),
              names)
        check("'Default radius' is the checked one",
              items is not None and any(t == "Default radius" and c for t, c, _ in items), items)
        pump(40)
        check("'Two radii': Edge1 has two points of 1 mm", segments(fillet) ==
              [[(0.0, 1.0, 0.0), (1.0, 1.0, 0.0)]], segments(fillet))
        check("three arrows are shown", shown_arrows() == 3, shown_arrows())

        if os.environ.get("GT_PICTURE"):
            view().saveRenderDump(os.path.join(OUT, "fillet-two-radii.png"), metadata=False)

        # The default is still the active value: one Tab is the first point's
        viewer_widget().setFocus()
        pump(5)
        key(Qt.Key_Tab)
        key(Qt.Key_2)
        typed = type_into_box("")
        check("Tab, 2, Enter: Edge1's first point is 2 mm",
              segments(fillet) == [[(0.0, 2.0, 0.0), (1.0, 1.0, 0.0)]], (typed, segments(fillet)))
        labels = text_labels()
        note("INFO labels: %s" % labels)
        check("its label 'R1 2 mm' is shown with the default's", "R1 2 mm" in labels
              and "Radius 1 mm" in labels, labels)

        if os.environ.get("GT_PICTURE"):
            view().saveRenderDump(os.path.join(OUT, "fillet-point-active.png"), metadata=False)

        items = menu_by_space("Default radius")
        note("INFO the menu: %s" % items)
        check("Space at the point: 'Two radii' checked",
              items is not None and any(t == "Two radii" and c for t, c, _ in items), items)
        pump(40)
        check("'Default radius': Edge1 has no points of its own", segments(fillet) == [],
              segments(fillet))
        check("two arrows again", shown_arrows() == 2, shown_arrows())

        # The point's value went with its mode: none is active, and Escape
        # would leave the edit. Tab is the default's again.
        viewer_widget().setFocus()
        pump(5)
        key(Qt.Key_Tab)
        items = menu_by_space("Remove")
        pump(80)
        subs = fillet.Base[1] if fillet.Base else None
        check("Tab, Space, 'Remove': one edge left", subs is not None and len(subs) == 1,
              subs)
        check("one arrow", shown_arrows() == 1, shown_arrows())
        check("still in edit", gdoc.getInEdit() is not None)
        gdoc.resetEdit()
        pump(40)
        check("leaving the edit takes the arrows away", shown_arrows() == 0, shown_arrows())
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
