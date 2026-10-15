"""Every operation in the fillet's edit an undo step of its own.

docs/HandsOnQueue.md entry 136 (the reporter, 2026-10-15 09:47, 09:58, 10:07,
10:13): each operation in the fillet's edit undoable, redo too; a drag one
step if it changed something, cancelled by Esc or another mouse button back
to where it began; a label open for typing one step, Enter committing in it
without closing it, Escape dropping what was not entered; OK a step of its
own; Cancel back to the state before the edit.

A 10 mm PartDesign box, a fillet on Edge1 and Edge2 at 1 mm, in edit. Claims:
  - the menu's "One radius" on Edge1: one more undo step;
  - Edge1's value typed: 2 Enter, 3 Enter, then Escape: one more step, Edge1
    3 mm;
  - Undo: Edge1 back to 1 mm (its "One radius" value); Undo: Edge1 back to
    the default; Redo: "One radius" again;
  - a drag of an arrow: one more step, the radius changed; Undo: back;
  - a drag with Escape before the release: the radius as before the drag,
    no step; every arrow back where it was -- Edge3's too, the other edge
    taking the default (entry 145);
  - OK: one more step, the edit closed;
  - the edit again, the default radius set to 4, Cancel: 1 mm again.

Runs with QT_QPA_PLATFORM=offscreen; the drag goes through the window as a
hand's would, at a pixel of the arrow found by picking.
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
DOC = "FilletUndoSteps"

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


def arrow_offsets():
    """Each shown arrow's drag offset, as text"""
    from pivy import coin

    root = view().getViewer().getSoRenderManager().getSceneGraph()
    search = coin.SoSearchAction()
    search.setType(coin.SoType.fromName("SoLinearDraggerContainer"))
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(root)
    offsets = []
    for p in search.getPaths():
        container = p.getTail()
        dragger = container.getPart("dragger", False)
        if dragger is not None:
            value = dragger.getField("translation").get()
            offsets.append(value.getString() if hasattr(value, "getString") else str(value))
    return offsets


def undos():
    return len(FreeCAD.getDocument(DOC).UndoNames)


def segments(fillet, edge):
    try:
        return [(round(p, 6), round(float(r), 6)) for p, r, _ in
                fillet.evalExpression("Segments.%s" % edge)]
    except Exception:
        return []


def menu_by_space(choice):
    seen = {"items": None}

    def answer():
        for w in QtWidgets.QApplication.topLevelWidgets():
            if isinstance(w, QtWidgets.QMenu) and w.isVisible():
                seen["items"] = [a.text().replace("&", "") for a in w.actions()
                                 if not a.isSeparator() and a.text()]
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


def tabs(n):
    """Nothing active (Escape only with a value active), then n values on"""
    viewer_widget().setFocus()
    pump(10)
    key(Qt.Key_Tab)
    key(Qt.Key_Escape)
    for _ in range(n):
        key(Qt.Key_Tab)


def pick_arrow(center, reach=70, step=2):
    """A viewport pixel well inside an arrow near `center`"""
    from pivy import coin

    viewer = view().getViewer()
    region = viewer.getSoRenderManager().getViewportRegion()

    def kind(x, y):
        action = coin.SoRayPickAction(region)
        action.setPoint(coin.SbVec2s(int(x), int(y)))
        action.setRadius(1)
        action.apply(viewer.getSoRenderManager().getSceneGraph())
        picked = action.getPickedPoint()
        if picked is None:
            return False
        path = picked.getPath()
        return any("LinearDragger" in path.getNode(i).getTypeId().getName().getString()
                   for i in range(path.getLength()))

    cx, cy = center
    best, found = -1, None
    for r in range(0, reach, step):
        for dx in range(-r, r + 1, step):
            for dy in (-r, r) if abs(dx) != r else range(-r, r + 1, step):
                if not kind(cx + dx, cy + dy):
                    continue
                score = sum(kind(cx + dx + i, cy + dy + j) for i in (-2, 0, 2) for j in (-2, 0, 2))
                if score > best:
                    best, found = score, (cx + dx, cy + dy)
    return found


def to_window(x, y):
    width, height = view().getSize()
    w = viewer_widget()
    local = QtCore.QPoint(int(x), int(height - y))
    g = w.viewport().mapToGlobal(local) if hasattr(w, "viewport") else w.mapToGlobal(local)
    return mw().mapFromGlobal(g)


def drag(start, offset, escape=False):
    win = mw().windowHandle()
    p = to_window(*start)
    QtTest.QTest.mouseMove(win, p)
    pump(5)
    QtTest.QTest.mousePress(win, Qt.LeftButton, Qt.NoModifier, p)
    pump(5)
    for i in range(1, 11):
        q = QtCore.QPoint(p.x() + offset[0] * i // 10, p.y() - offset[1] * i // 10)
        QtTest.QTest.mouseMove(win, q)
        pump(5)
    doc = FreeCAD.getDocument(DOC)

    def box_text():
        boxes = [w for w in mw().findChildren(QtWidgets.QAbstractSpinBox)
                 if w.objectName() == "filletRadius"]
        return boxes[0].text() if boxes else None

    def where(stage):
        fillet = doc.getObject("Fillet")
        focus = QtWidgets.QApplication.focusWidget()
        note("INFO %s: Radius %.4f, the box %r, undo %s, active %s, focus %s, the view widget %s"
             % (stage, fillet.Radius.Value, box_text(), doc.UndoNames[:3],
                FreeCAD.getActiveTransaction(),
                focus.metaObject().className() if focus else None,
                viewer_widget().metaObject().className()))

    if escape:
        where("in the drag, before Escape")
        real_key(viewer_widget(), Qt.Key_Escape)
        pump(20)
        where("after Escape, the button still down")
    QtTest.QTest.mouseRelease(win, Qt.LeftButton, Qt.NoModifier, q)
    pump(80)
    if escape:
        where("after the release")


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
        fillet.Base = (box, ["Edge1", "Edge2", "Edge3"])
        fillet.Radius = 1
        doc.recompute()
        FreeCADGui.SendMsgToActiveView("ViewFit")
        FreeCADGui.activeDocument().activeView().viewIsometric()
        pump()

        gdoc = FreeCADGui.getDocument(DOC)
        gdoc.setEdit(fillet, 0)
        pump(80)
        check("the fillet is in edit", gdoc.getInEdit() is not None)
        start = undos()
        note("INFO undo steps at the start: %d %s" % (start, doc.UndoNames))

        tabs(1)
        menu_by_space("One radius")
        pump(40)
        check("'One radius' on Edge1: one more undo step",
              undos() == start + 1 and segments(fillet, "Edge1") == [(0.0, 1.0)],
              (undos() - start, segments(fillet, "Edge1")))

        # Size, then Edge1's own value
        tabs(2)
        key(Qt.Key_2)
        b = open_box()
        if b is not None:
            real_key(b, Qt.Key_Return)
            pump(40)
            b.selectAll()
            QtTest.QTest.keyClicks(b, "3")
            pump(5)
            real_key(b, Qt.Key_Return)
            pump(40)
            check("Enter leaves the label open", open_box() is not None)
            real_key(b, Qt.Key_Escape)
            pump(40)
        check("2 Enter, 3 Enter, Escape: Edge1 3 mm, one more step",
              segments(fillet, "Edge1") == [(0.0, 3.0)] and undos() == start + 2,
              (segments(fillet, "Edge1"), undos() - start, doc.UndoNames))

        FreeCADGui.runCommand("Std_Undo")
        pump(60)
        check("Undo: Edge1 back to 1 mm", segments(fillet, "Edge1") == [(0.0, 1.0)],
              segments(fillet, "Edge1"))
        FreeCADGui.runCommand("Std_Undo")
        pump(60)
        check("Undo: Edge1 takes the default again", segments(fillet, "Edge1") == [],
              segments(fillet, "Edge1"))
        FreeCADGui.runCommand("Std_Redo")
        pump(60)
        check("Redo: 'One radius' again", segments(fillet, "Edge1") == [(0.0, 1.0)],
              segments(fillet, "Edge1"))
        check("still in edit", gdoc.getInEdit() is not None)

        # A drag of the default's arrow (Edge2's)
        edge = box.Shape.getElement("Edge2")
        middle = edge.valueAt((edge.FirstParameter + edge.LastParameter) / 2)
        arrow = pick_arrow(view().getPointOnViewport(middle))
        note("INFO the arrow at %s" % (arrow,))
        if check("an arrow is found to drag", arrow is not None):
            before = undos()
            radius = fillet.Radius.Value
            drag(arrow, (40, 20))
            note("INFO after the drag: Radius %.4f, steps %d" % (fillet.Radius.Value, undos() - before))
            check("a drag: the radius changed, one more step",
                  abs(fillet.Radius.Value - radius) > 1e-6 and undos() == before + 1,
                  (fillet.Radius.Value, undos() - before))
            FreeCADGui.runCommand("Std_Undo")
            pump(60)
            check("Undo: the radius as before the drag",
                  abs(fillet.Radius.Value - radius) < 1e-6, fillet.Radius.Value)

            before = undos()
            offsets = arrow_offsets()
            drag(arrow, (40, 20), escape=True)
            check("a drag with Escape: the radius as before it, no step",
                  abs(fillet.Radius.Value - radius) < 1e-6 and undos() == before,
                  (fillet.Radius.Value, undos() - before))
            check("Escape in the drag left the edit alone", gdoc.getInEdit() is not None)
            check("every arrow back where it was, the other default edge's too",
                  arrow_offsets() == offsets, (arrow_offsets(), offsets))

        before = undos()
        accept = None
        for w in mw().findChildren(QtWidgets.QDialogButtonBox):
            ok = w.button(QtWidgets.QDialogButtonBox.Ok)
            if ok is not None and ok.isVisible():
                accept = ok
        if accept is not None:
            accept.click()
        pump(80)
        check("OK: the edit closed", gdoc.getInEdit() is None)
        note("INFO after OK: %s" % doc.UndoNames[:4])
        check("OK: one more step", undos() == before + 1, (undos() - before, doc.UndoNames[:4]))

        # Cancel goes back to before the edit
        gdoc.setEdit(fillet, 0)
        pump(80)
        tabs(1)
        key(Qt.Key_4)
        b = open_box()
        if b is not None:
            real_key(b, Qt.Key_Return)
            pump(40)
            viewer_widget().setFocus()
            pump(20)
        note("INFO the radius in the second edit: %.4f" % fillet.Radius.Value)
        note("INFO before Cancel: undo %s, redo %s, active %s" % (
            doc.UndoNames[:5], doc.RedoNames[:3], FreeCAD.getActiveTransaction()))
        cancel = None
        for w in mw().findChildren(QtWidgets.QDialogButtonBox):
            c = w.button(QtWidgets.QDialogButtonBox.Cancel)
            if c is not None and c.isVisible():
                cancel = c
        note("INFO the Cancel button found: %s" % (cancel is not None))
        if cancel is not None:
            cancel.click()
        pump(80)
        note("INFO after Cancel: undo %s, redo %s, in edit %s" % (
            doc.UndoNames[:5], doc.RedoNames[:3], gdoc.getInEdit() is not None))
        check("Cancel: the radius as before the edit", abs(fillet.Radius.Value - 1.0) < 1e-6,
              fillet.Radius.Value)
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
