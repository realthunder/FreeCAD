"""Keyboard focus and the tool cursor in a sketch edit.

- An edit entered from the tree left the keyboard in the tree, so an
  Escape pressed right away did nothing (upstream 22a98d81f0). The
  edit now gives the view the focus.
- A tool dismissed with Escape leaves the focus in the view, so a second
  Escape leaves the edit. Upstream had to give it back after the tool
  was purged; here it was never lost, and this checks that it stays so.
- A click with a tool active put the viewer's edit cursor back in place
  of the tool's until the next move (upstream a1487106ab).

Keys go to whatever has the focus, as a keyboard's would; the sketcher
acts on the release, so both are sent. Before the fixes the first and
the last checks failed; the Escape pair passed.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchFocusCursor"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    import time
    end = time.monotonic() + seconds
    while True:
        QtCore.QCoreApplication.processEvents()
        if time.monotonic() >= end:
            break
        time.sleep(0.01)


def in_edit():
    return FreeCADGui.getDocument(DOC).getInEdit() is not None


def focus_name():
    w = QtWidgets.QApplication.focusWidget()
    return "None" if w is None else w.metaObject().className()


def escape():
    w = QtWidgets.QApplication.focusWidget() or FreeCADGui.getMainWindow()
    for kind in (QtCore.QEvent.ShortcutOverride, QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        QtWidgets.QApplication.sendEvent(
            w, QtGui.QKeyEvent(kind, QtCore.Qt.Key_Escape, QtCore.Qt.NoModifier))
    settle(0.5)


def shape_value(s):
    return int(s.value) if hasattr(s, "value") else int(s)


BITMAP = shape_value(QtCore.Qt.BitmapCursor)


def cursor(w):
    return shape_value(w.cursor().shape())


def mouse(w, kind, pos, button, buttons):
    local = QtCore.QPointF(*pos)
    glob = QtCore.QPointF(w.mapToGlobal(QtCore.QPoint(*pos)))
    QtWidgets.QApplication.sendEvent(
        w, QtGui.QMouseEvent(kind, local, glob, button, buttons, QtCore.Qt.NoModifier))
    settle(0.1)


def enter(sk):
    FreeCADGui.getDocument(DOC).setEdit(sk)
    settle(1.0)


def leave():
    if in_edit():
        FreeCADGui.getDocument(DOC).resetEdit()
        settle(0.5)


def viewport():
    return FreeCADGui.getDocument(DOC).ActiveView.graphicsView().viewport()


def run():
    try:
        import Part
        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(10, 5, 0)), False)
        doc.recompute()
        mw = FreeCADGui.getMainWindow()
        mw.activateWindow()
        settle(0.5)

        # Entered from the tree, the way a double click there does.
        tree = None
        for t in mw.findChildren(QtWidgets.QTreeWidget):
            if t.metaObject().className() == "Gui::TreeWidget" and t.isVisible():
                tree = t
        check("the tree is there to start from", tree is not None)
        if tree is not None:
            tree.setFocus()
            settle(0.2)
        enter(sk)
        check("in edit", in_edit())
        escape()
        check("an Escape right after entering from the tree leaves the edit",
              not in_edit(), "focus in " + focus_name())
        leave()

        # A tool dismissed by one Escape, then the edit by another.
        enter(sk)
        FreeCADGui.runCommand("Sketcher_CreateLine")
        settle(0.5)
        mouse(viewport(), QtCore.QEvent.MouseMove, (200, 200), QtCore.Qt.NoButton, QtCore.Qt.NoButton)
        check("the line tool is on (its cursor)", cursor(viewport()) == BITMAP, cursor(viewport()))
        escape()
        check("one Escape ends the tool and keeps the edit",
              in_edit() and cursor(viewport()) != BITMAP,
              "in edit %s, cursor %d" % (in_edit(), cursor(viewport())))
        escape()
        check("a second Escape leaves the edit", not in_edit(), "focus in " + focus_name())
        leave()

        # The tool's cursor across a click.
        enter(sk)
        FreeCADGui.runCommand("Sketcher_CreateLine")
        settle(0.5)
        vp = viewport()
        mouse(vp, QtCore.QEvent.MouseMove, (200, 200), QtCore.Qt.NoButton, QtCore.Qt.NoButton)
        check("the line tool's cursor before the click", cursor(vp) == BITMAP, cursor(vp))
        mouse(vp, QtCore.QEvent.MouseButtonPress, (200, 200), QtCore.Qt.LeftButton, QtCore.Qt.LeftButton)
        mouse(vp, QtCore.QEvent.MouseButtonRelease, (200, 200), QtCore.Qt.LeftButton, QtCore.Qt.NoButton)
        settle(0.3)
        check("the line tool's cursor after the click, before any move",
              cursor(vp) == BITMAP, cursor(vp))
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    if FreeCAD.listDocuments().get(DOC) and in_edit():
        FreeCADGui.getDocument(DOC).resetEdit()
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
