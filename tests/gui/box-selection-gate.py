"""Box selection under an active selection gate (upstream a5bf17b144).

A selection gate -- `Gui.Selection.addSelectionGate("SELECT ... SUBELEMENT
Vertex")`, or a command's own filter -- decides what may be selected. A box
selection adds every element it finds one at a time, and each one the gate
refuses was treated as a refused click: a status bar message, the view's
cursor set to Qt::ForbiddenCursor, and a beep. One box over a part refuses
dozens of faces and edges at once.

Measured here, on a Part box in an isometric view, with
Std_BoxElementSelection driven by synthetic mouse events on the viewer:

  - with no gate, the box selects faces, edges and vertices (the control);
  - with a vertex-only gate, it selects the vertices and nothing else;
  - the elements the gate turns away are not reported one by one: the
    status bar does not say "not allowed" (each refusal also beeped and
    set the forbidden cursor -- 15 of them for one box here, measured
    with a counting Python gate);
  - and the view's cursor is not left forbidden (the command resets it
    on the way out, so this one is a guard, not the defect).

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "BoxSelectionGate"

state = {"done": False, "refusals": 0}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.0):
    end = time.monotonic() + seconds
    while True:
        QtCore.QCoreApplication.processEvents()
        if time.monotonic() >= end:
            break
        time.sleep(0.01)


def viewport():
    from PySide import QtWidgets
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        name = w.metaObject().className()
        if ("Quarter" in name or "View3DInventorViewer" in name) and w.isVisible():
            return w
    return None


def mouse(w, typ, pos, button, buttons):
    from PySide import QtGui, QtWidgets
    p = QtCore.QPointF(pos[0], pos[1])
    QtWidgets.QApplication.sendEvent(
        w, QtGui.QMouseEvent(typ, p, w.mapToGlobal(p), button, buttons,
                             QtCore.Qt.NoModifier))
    settle()


def element_box(w):
    """Std_BoxElementSelection over the whole view, left to right (so
    by centre); the elapsed time from press to done."""
    settle(0.8)
    FreeCADGui.runCommand("Std_BoxElementSelection")
    settle(0.2)
    a = (5, 5)
    b = (w.width() - 5, w.height() - 5)
    left, none = QtCore.Qt.LeftButton, QtCore.Qt.NoButton
    mouse(w, QtCore.QEvent.MouseMove, a, none, none)
    t0 = time.perf_counter()
    mouse(w, QtCore.QEvent.MouseButtonPress, a, left, left)
    mouse(w, QtCore.QEvent.MouseMove, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2), none, left)
    mouse(w, QtCore.QEvent.MouseMove, b, none, left)
    mouse(w, QtCore.QEvent.MouseButtonRelease, b, left, none)
    dt = time.perf_counter() - t0
    settle(0.3)
    return dt


def selected():
    subs = []
    for s in FreeCADGui.Selection.getSelectionEx("*"):
        subs.extend(s.SubElementNames)
    return subs


def kinds(subs):
    out = {}
    for s in subs:
        k = s.rstrip("0123456789")
        out[k] = out.get(k, 0) + 1
    return out


def status_texts():
    """What the status bar's labels say."""
    from PySide import QtWidgets
    bar = FreeCADGui.getMainWindow().statusBar()
    return [l.text() for l in bar.findChildren(QtWidgets.QLabel) if l.text()]


class VertexGate:
    """A Python selection gate letting vertices through, counting what it
    is asked and what it refuses."""

    def __init__(self):
        self.asked = 0
        self.refused = 0

    def allow(self, doc, obj, sub):
        self.asked += 1
        if sub and sub.startswith("Vertex"):
            return True
        self.refused += 1
        return False


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)

        doc = FreeCAD.newDocument(DOC)
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        view = FreeCADGui.getDocument(DOC).ActiveView
        view.viewIsometric()
        view.fitAll()
        settle(0.5)
        w = viewport()
        if not check("the viewer widget is found", w is not None):
            finish()
            return

        # Control: no gate.
        FreeCADGui.Selection.clearSelection()
        element_box(w)
        got = kinds(selected())
        check("with no gate the box selects faces, edges and vertices",
              got.get("Face", 0) > 0 and got.get("Edge", 0) > 0 and got.get("Vertex", 0) > 0,
              got)

        # A vertex-only gate.
        FreeCADGui.Selection.clearSelection()
        gate = VertexGate()
        FreeCADGui.Selection.addSelectionGate(gate)
        dt = element_box(w)
        got = kinds(selected())
        note("gated box took %.3f s; the gate was asked %d times, refused %d" % (
            dt, gate.asked, gate.refused))
        check("under a vertex-only gate the box selects vertices only",
              got.get("Vertex", 0) > 0 and set(got) == {"Vertex"}, got)
        said = status_texts()
        check("and the user is not told any element was refused",
              not any("not allowed" in t.lower() for t in said), said)
        shape = w.cursor().shape()
        check("and the view's cursor is not left forbidden",
              shape != QtCore.Qt.ForbiddenCursor, shape)
        FreeCADGui.Selection.removeSelectionGate()
    except Exception:
        note("ABORT run:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.Selection.removeSelectionGate()
    except Exception:
        pass
    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
