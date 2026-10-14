"""A fillet's default radius is shown at the top middle of the view and typed there.

docs/HandsOnQueue.md entry 117, docs/OnViewValueEditing.md section 4 step 1:
the default radius's label always at the top middle of the view; its arrow
the handle that takes typing once active (made so by Tab or a click on it);
keys while it is active -- a digit opens its label with that digit, Enter
opens it with the value selected, Escape leaves no handle active; a click on
the label opens it too.

A 10 mm PartDesign box, a fillet on Edge1 at 1 mm, in edit. Claims:
  - the view has one label of the text-only kind reading "Radius 1 mm", at
    the top middle of the view;
  - Tab in the view, then 3 and Enter: Radius is 3 mm, the label reads so;
  - Enter (the handle still active), 2.5 and Enter: Radius is 2.5 mm;
  - Escape, then 7: no box opens and Radius stays 2.5 mm;
  - a click on the label, 4 and Enter: Radius is 4 mm;
  - Radius driven by an expression: the label shows it, and Tab then 6
    opens no box.

Keys go to the viewer widget through QTest, which asks for a shortcut first
(ShortcutOverride) as a real key does; the typed digits go to the box that
has the focus. Runs with QT_QPA_PLATFORM=offscreen; the pointer is moved
inside the FreeCAD window only. With GT_PICTURE set it also saves the view.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from PySide.QtCore import Qt

try:
    from PySide6 import QtTest
except ImportError:
    from PySide2 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "FilletRadiusTypedInView"

state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
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


def field_text(node, name):
    value = node.getField(name).get()
    return value.getString() if hasattr(value, "getString") else str(value)


def text_labels():
    """(text, world point) of each text-only datum label under the on-view root"""
    from pivy import coin

    search = coin.SoSearchAction()
    search.setName(coin.SbName("OnViewRoot"))
    search.setInterest(coin.SoSearchAction.FIRST)
    search.apply(view().getAuxSceneGraph())
    path = search.getPath()
    if not path:
        return []
    root = path.getTail()
    search = coin.SoSearchAction()
    search.setType(coin.SoType.fromName("SoDatumLabel"))
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(root)
    found = []
    for p in search.getPaths():
        label = p.getTail()
        # pivy gives an SbString
        if field_text(label, "datumtype") != "TEXT":
            continue
        text = field_text(label, "string").strip().strip('"')
        transform = p.getNodeFromTail(1).getChild(0)
        point = None
        if transform.isOfType(coin.SoTransform.getClassTypeId()):
            point = FreeCAD.Vector(*transform.translation.getValue().getValue())
        found.append((text, point))
    return found


def viewer_widget():
    return view().graphicsView()


def real_key(widget, k):
    """Press on `widget`, release to whichever widget has the focus then, as a
    keyboard does: a key that moves the focus is let go somewhere else"""
    QtTest.QTest.keyPress(widget, k, Qt.NoModifier)
    pump(5)
    target = QtWidgets.QApplication.focusWidget() or widget
    QtTest.QTest.keyRelease(target, k, Qt.NoModifier)
    pump(10)


def key(k, text=""):
    real_key(viewer_widget(), k)


def open_box():
    """The label's box, if one is open: a Gui::QuantitySpinBox outside the task panel"""
    focus = QtWidgets.QApplication.focusWidget()
    if focus is not None and focus.metaObject().className() == "Gui::QuantitySpinBox":
        task = focus
        while task is not None and task.metaObject().className() != "Gui::TaskView::TaskView":
            task = task.parentWidget()
        if task is None:
            return focus
    for w in mw().findChildren(QtWidgets.QAbstractSpinBox):
        if not w.isVisible() or w.metaObject().className() != "Gui::QuantitySpinBox":
            continue
        parent = w
        while parent is not None and parent.metaObject().className() != "Gui::TaskView::TaskView":
            parent = parent.parentWidget()
        if parent is None:
            return w
    return None


def type_into_box(text, enter=True):
    box = open_box()
    if box is None:
        return None
    for ch in text:
        QtTest.QTest.keyClicks(box, ch)
    pump(5)
    seen = box.text()
    if enter:
        real_key(box, Qt.Key_Return)
        pump(40)
    return seen


def radius(fillet):
    return round(fillet.Radius.Value, 6)


def label_screen_point(point):
    width, height = view().getSize()
    x, y = view().getPointOnViewport(point)
    # Coin counts from the bottom, Qt from the top
    return x, height - y, width, height


def click_label(point):
    x, y, _, _ = label_screen_point(point)
    w = viewer_widget()
    local = QtCore.QPoint(int(x), int(y))
    g = w.viewport().mapToGlobal(local) if hasattr(w, "viewport") else w.mapToGlobal(local)
    win = mw().windowHandle()
    p = mw().mapFromGlobal(g)
    QtTest.QTest.mouseMove(win, p)
    pump(5)
    QtTest.QTest.mousePress(win, Qt.LeftButton, Qt.NoModifier, p)
    pump(5)
    QtTest.QTest.mouseRelease(win, Qt.LeftButton, Qt.NoModifier, p)
    pump(40)


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
        fillet.Base = (box, ["Edge1"])
        fillet.Radius = 1
        doc.recompute()
        FreeCADGui.SendMsgToActiveView("ViewFit")
        pump()

        gdoc = FreeCADGui.getDocument(DOC)
        gdoc.setEdit(fillet, 0)
        pump(80)
        check("the fillet is in edit", gdoc.getInEdit() is not None)

        labels = text_labels()
        note("INFO text labels: %s" % [(t, None if p is None else tuple(round(c, 3) for c in p))
                                       for t, p in labels])
        if not check("one text label in the view", len(labels) == 1, len(labels)):
            return
        text, point = labels[0]
        check("it reads 'Radius 1 mm'", text == "Radius 1 mm", text)
        x, y, width, height = label_screen_point(point)
        check("it stands at the top middle of the view",
              abs(x - width / 2) <= 2 and 10 <= y <= 50, (x, y, width, height))

        if os.environ.get("GT_PICTURE"):
            view().saveRenderDump(os.path.join(OUT, "fillet-in-edit.png"), metadata=False)

        # Tab makes the radius's handle active; a digit opens its label
        viewer_widget().setFocus()
        pump(5)
        key(Qt.Key_Tab)
        key(Qt.Key_3)
        typed = type_into_box("", enter=True)
        check("Tab, 3, Enter: the label opened with the digit", typed is not None
              and typed.strip().startswith("3"), typed)
        check("and Radius is 3 mm", radius(fillet) == 3.0, radius(fillet))
        labels = text_labels()
        check("the label reads 'Radius 3 mm'", labels and labels[0][0] == "Radius 3 mm",
              labels and labels[0][0])

        # Enter with the handle still active: the value selected, replaced
        key(Qt.Key_Return)
        typed = type_into_box("2.5", enter=True)
        check("Enter, 2.5, Enter: Radius is 2.5 mm", radius(fillet) == 2.5,
              (typed, radius(fillet)))

        # Still active after a commit: a digit opens the label; Escape in the
        # label restores the value and keeps the handle active
        focus = QtWidgets.QApplication.focusWidget()
        note("INFO focus after the commit: %s" % (focus.metaObject().className() if focus else None))
        key(Qt.Key_9)
        opened = open_box()
        check("after a commit the handle is still active: 9 opens its label",
              opened is not None, opened and opened.text())
        if opened is not None:
            real_key(opened, Qt.Key_Escape)
            pump(20)
        check("Escape in the label: Radius stays 2.5 mm, still in edit",
              radius(fillet) == 2.5 and gdoc.getInEdit() is not None,
              (radius(fillet), gdoc.getInEdit() is not None))
        focus = QtWidgets.QApplication.focusWidget()
        note("INFO focus after Escape in the label: %s"
             % (focus.metaObject().className() if focus else None))

        # Escape: no handle active, a digit opens nothing
        key(Qt.Key_Escape)
        note("INFO after Escape: in edit %s, labels %s" % (gdoc.getInEdit() is not None,
                                                         [t for t, _ in text_labels()]))
        key(Qt.Key_7)
        note("INFO after 7: in edit %s, labels %s" % (gdoc.getInEdit() is not None,
                                                    [t for t, _ in text_labels()]))
        check("Escape, then 7: no box opens", open_box() is None)
        check("and the edit goes on", gdoc.getInEdit() is not None)
        check("and Radius stays 2.5 mm", radius(fillet) == 2.5, radius(fillet))

        # A click on the label opens it
        labels = text_labels()
        click_label(labels[0][1])
        typed = type_into_box("4", enter=True)
        check("a click on the label, 4, Enter: Radius is 4 mm", radius(fillet) == 4.0,
              (typed, radius(fillet)))

        # Driven by an expression: shown, not typed
        gdoc.resetEdit()
        pump(40)
        fillet.setExpression("Radius", "1.5 mm")
        doc.recompute()
        gdoc.setEdit(fillet, 0)
        pump(80)
        labels = text_labels()
        check("with an expression the label shows it",
              labels and "=" in labels[0][0] and labels[0][0].startswith("Radius 1.5 mm"),
              labels and labels[0][0])
        viewer_widget().setFocus()
        pump(5)
        key(Qt.Key_Tab)
        key(Qt.Key_6)
        check("Tab, 6: no box opens for a value an expression drives", open_box() is None)
        check("and Radius stays 1.5 mm", radius(fillet) == 1.5, radius(fillet))
        gdoc.resetEdit()
        pump(40)
        check("leaving the edit takes the label away", not text_labels(), text_labels())
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
