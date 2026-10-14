"""A point inside a fillet edge has a knob along the edge, and its place is typed in the view.

docs/HandsOnQueue.md entry 117, docs/OnViewValueEditing.md 3.3 "POINTS ALONG
AN EDGE": the arrow of a point that is not at an end gets a position handle
as well, sliding along the edge; its label, when active, takes the position.

A 10 mm PartDesign box, a fillet on Edge1 and Edge2; Edge2 varies over three
points (0, 1 mm), (0.5, 2 mm), (1, 1 mm). In edit. Claims:
  - Edge2 has four handles -- a radius arrow per point and a knob for the
    middle one -- and Edge1 one: five shown;
  - Tab four times reaches the knob (the default, R1, R2, P2), whose label
    reads "1  P2 5 mm" (its group first); 7 and Enter put the point at 0.7 of the edge, its
    radius kept;
  - Space there: the menu reads "Varying (set in the panel)", checked and
    not to be chosen.

Runs with QT_QPA_PLATFORM=offscreen; the menu is answered from a timer.
With GT_PICTURE set it also saves the view.
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
DOC = "FilletPointPositionInView"

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


def shown_handles():
    from pivy import coin

    root = view().getViewer().getSoRenderManager().getSceneGraph()
    search = coin.SoSearchAction()
    search.setType(coin.SoType.fromName("SoLinearDraggerContainer"))
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(root)
    return sum(1 for p in search.getPaths()
               if field_text(p.getTail(), "visible") in ("TRUE", "1"))


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
    return [field_text(p.getTail(), "string").strip().strip('"') for p in labels.getPaths()
            if field_text(p.getTail(), "datumtype") == "TEXT"]


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
        fillet.Segments = [("Edge2", [(0.0, 1.0, 0.0), (0.5, 2.0, 0.0), (1.0, 1.0, 0.0)])]
        doc.recompute()
        note("INFO segments: %s, valid %s" % (segments(fillet), fillet.isValid()))
        FreeCADGui.SendMsgToActiveView("ViewFit")
        FreeCADGui.activeDocument().activeView().viewIsometric()
        pump()

        gdoc = FreeCADGui.getDocument(DOC)
        gdoc.setEdit(fillet, 0)
        pump(80)
        check("the fillet is in edit", gdoc.getInEdit() is not None)
        check("five handles: Edge1's arrow, Edge2's three and its middle point's knob",
              shown_handles() == 5, shown_handles())

        viewer_widget().setFocus()
        pump(5)
        for _ in range(4):
            key(Qt.Key_Tab)
        labels = text_labels()
        note("INFO labels: %s" % labels)
        check("four Tabs: the knob's label reads '1  P2 5 mm'", "1  P2 5 mm" in labels, labels)
        if os.environ.get("GT_PICTURE"):
            view().saveRenderDump(os.path.join(OUT, "fillet-point-knob.png"), metadata=False)

        key(Qt.Key_7)
        box_ = open_box()
        typed = box_.text() if box_ else None
        if box_ is not None:
            real_key(box_, Qt.Key_Return)
            pump(80)
        check("7, Enter: the middle point is at 0.7, its radius kept",
              segments(fillet) == [[(0.0, 1.0, 0.0), (0.7, 2.0, 0.0), (1.0, 1.0, 0.0)]],
              (typed, segments(fillet)))

        seen = {"items": None}

        def answer():
            for w in QtWidgets.QApplication.topLevelWidgets():
                if isinstance(w, QtWidgets.QMenu) and w.isVisible():
                    seen["items"] = [(a.text().replace("&", ""), a.isChecked(), a.isEnabled())
                                     for a in w.actions() if not a.isSeparator() and a.text()]
                    w.close()
                    return
            QtCore.QTimer.singleShot(100, answer)

        QtCore.QTimer.singleShot(300, answer)
        viewer_widget().setFocus()
        pump(5)
        key(Qt.Key_Space)
        pump(60)
        items = seen["items"]
        note("INFO the menu: %s" % items)
        check("Space at the knob: 'Varying (set in the panel)' checked, not to be chosen",
              items is not None and ("Varying (set in the panel)", True, False) in items, items)
        check("still in edit", gdoc.getInEdit() is not None)
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
