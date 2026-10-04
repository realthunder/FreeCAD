"""A number typed into a tool's on-view box is the number the tool uses.

The rectangle tool, after its first corner, shows two entry boxes, width
and height, the width's with the keys. What a user does with them:

    10 Enter 20 Enter      10 Tab 20 Enter

and either makes a rectangle 10 by 20.

Neither did. The box commits on Enter or Tab from a key filter
(EditableDatumLabel::eventFilter, upstream 9b40afea7a, here 4d210d07f8),
and the filter runs before the box has read its own text: with keyboard
tracking off a typed number waits in the box until the box's own Enter
handling or a loss of focus. So the filter committed the number the box
held BEFORE the typing. For the width that was put right a moment later --
the focus moved on, the box read its text and reported it -- but the last
box of a stage loses the focus to nothing before the tool finishes:

    10 Enter 20 Enter   made 10 by the pointer's height, and constrained
                        that height;
    10 Tab 20 Enter     made nothing: the typed 10 did not count as typed,
                        Tab took the "nothing entered, move on" branch,
                        the focus came back to the width, and 20 was
                        appended to it.

And a pointer move while typing took the box back to the pointer's value,
which is the defect upstream's commit was written for. Upstream turned
keyboard tracking on in that commit; the fork took the commit's logic and
not that line.

Claims, each on a fresh rectangle tool:
  - 10 Enter 20 Enter: a rectangle 10 by 20, constrained to both;
  - 10 Tab 20 Enter: the same;
  - Tab with nothing typed moves the keys to the other box and back, and
    makes nothing;
  - a typed width survives a pointer move, and a click then makes a
    rectangle of that width.

Keys go to whatever has the focus, press and release each, as a
keyboard's do; clicks are real press / release events on the viewport.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "OnViewTypedValues"
V = FreeCAD.Vector
FIRST = V(-20, -12, 0)
SECOND = V(-3, 2, 0)
THIRD = V(7, 9, 0)
KEYS = {"0": QtCore.Qt.Key_0, "1": QtCore.Qt.Key_1, "2": QtCore.Qt.Key_2}
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def mouse(view, kind, pt, button, buttons):
    gv = view.graphicsView()
    vp = gv.viewport()
    x, y = view.getPointOnViewport(pt)
    dpr = vp.devicePixelRatioF()
    pos = QtCore.QPointF(x / dpr, vp.height() - 1 - y / dpr)
    QtWidgets.QApplication.sendEvent(
        vp, QtGui.QMouseEvent(kind, pos, gv.mapToGlobal(pos.toPoint()), button, buttons,
                              QtCore.Qt.NoModifier))


def move(view, pt):
    none = QtCore.Qt.NoButton
    mouse(view, QtCore.QEvent.MouseMove, pt, none, none)
    settle(0.3)


def click(view, pt):
    none = QtCore.Qt.NoButton
    left = QtCore.Qt.LeftButton
    move(view, pt)
    mouse(view, QtCore.QEvent.MouseMove, pt, none, none)
    mouse(view, QtCore.QEvent.MouseButtonPress, pt, left, left)
    settle(0.1)
    mouse(view, QtCore.QEvent.MouseButtonRelease, pt, left, none)
    settle(0.5)


def key(code, text=""):
    # The press and the release each go to what has the focus THEN: a Tab's
    # press can move it.
    for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        target = QtWidgets.QApplication.focusWidget()
        if target is None:
            raise RuntimeError("nothing has the keyboard focus")
        QtWidgets.QApplication.sendEvent(
            target, QtGui.QKeyEvent(kind, code, QtCore.Qt.NoModifier, text))
        settle(0.08)


def typed(text):
    for ch in text:
        key(KEYS[ch], ch)


def boxes():
    return [b for b in FreeCADGui.getMainWindow().findChildren(QtWidgets.QAbstractSpinBox)
            if b.isVisible()]


def focused():
    for b in boxes():
        if b.hasFocus():
            return b
    return None


def texts():
    return [(b.text(), b.hasFocus()) for b in boxes()]


def escape_tool(view):
    # One Escape ends the tool; a second would leave the sketch.
    view.graphicsView().setFocus(QtCore.Qt.OtherFocusReason)
    settle(0.2)
    key(QtCore.Qt.Key_Escape)
    settle(0.4)


def clear(sk):
    if len(sk.Geometry):
        sk.delGeometries(list(range(len(sk.Geometry))))
    sk.Document.recompute()
    settle(0.3)


def extent(sk):
    pts = [p for g in sk.Geometry if g.TypeId == "Part::GeomLineSegment"
           for p in (g.StartPoint, g.EndPoint)]
    if not pts:
        return None
    return (round(max(p.x for p in pts) - min(p.x for p in pts), 6),
            round(max(p.y for p in pts) - min(p.y for p in pts), 6))


def dimensions(sk):
    return sorted(round(c.Value, 6) for c in sk.Constraints if c.Type.startswith("Distance"))


def begin(view, name):
    """The rectangle tool with its first corner placed and the pointer moved
    on: two boxes, one of them with the keys."""
    FreeCADGui.runCommand("Sketcher_CreateRectangle")
    settle(0.6)
    click(view, FIRST)
    move(view, SECOND)
    settle(0.3)
    ok = check("%s: two entry boxes, one with the keys" % name,
               len(boxes()) == 2 and focused() is not None, texts())
    return ok


def made(name, sk, want):
    lines = [g for g in sk.Geometry if g.TypeId == "Part::GeomLineSegment"]
    check("%s: a rectangle was made" % name, len(lines) == 4, len(sk.Geometry))
    check("%s: it is %g by %g" % (name, want[0], want[1]), extent(sk) == want, extent(sk))
    check("%s: and constrained to both" % name, dimensions(sk) == sorted(want), dimensions(sk))


def enter_enter(view, sk):
    name = "10 Enter 20 Enter"
    if begin(view, name):
        typed("10")
        key(QtCore.Qt.Key_Return, "\r")
        typed("20")
        key(QtCore.Qt.Key_Return, "\r")
        settle(0.6)
        made(name, sk, (10.0, 20.0))
    escape_tool(view)
    clear(sk)


def tab_enter(view, sk):
    name = "10 Tab 20 Enter"
    if begin(view, name):
        width = focused()
        typed("10")
        key(QtCore.Qt.Key_Tab, "\t")
        settle(0.2)
        check("%s: Tab took the keys to the other box" % name,
              focused() is not None and focused() is not width, texts())
        typed("20")
        key(QtCore.Qt.Key_Return, "\r")
        settle(0.6)
        made(name, sk, (10.0, 20.0))
    escape_tool(view)
    clear(sk)


def tab_alone(view, sk):
    name = "Tab alone"
    if begin(view, name):
        width = focused()
        key(QtCore.Qt.Key_Tab, "\t")
        settle(0.2)
        height = focused()
        check("%s: the keys go to the other box" % name,
              height is not None and height is not width, texts())
        key(QtCore.Qt.Key_Tab, "\t")
        settle(0.2)
        check("%s: and back" % name, focused() is width, texts())
        check("%s: nothing was made" % name, len(sk.Geometry) == 0, len(sk.Geometry))
    escape_tool(view)
    clear(sk)


def typed_survives_move(view, sk):
    name = "typed, then the pointer moves"
    if begin(view, name):
        width = focused()
        typed("10")
        move(view, THIRD)
        settle(0.3)
        check("%s: the box keeps the typed number" % name,
              width.text().startswith("10"), texts())
        click(view, THIRD)
        settle(0.5)
        size = extent(sk)
        check("%s: a click makes a rectangle 10 wide" % name,
              size is not None and size[0] == 10.0, size)
    escape_tool(view)
    clear(sk)


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").SetInt(
            "OnViewParameterVisibility", 1)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/General").SetBool(
            "AdjustCamera", False)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher").SetBool(
            "EnableEscape", False)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)
        mw = FreeCADGui.getMainWindow()
        mw.showMaximized()
        mw.raise_()
        mw.activateWindow()
        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        doc.recompute()
        gdoc = FreeCADGui.getDocument(DOC)
        view = gdoc.activeView()
        view.viewTop()
        view.setCameraType("Orthographic")
        gdoc.setEdit(sk)
        settle(1.0)
        cam = view.getCameraNode()
        cam.position.setValue(0.0, 0.0, cam.position.getValue()[2])
        cam.height.setValue(60.0)
        settle(0.8)
        enter_enter(view, sk)
        tab_enter(view, sk)
        tab_alone(view, sk)
        typed_survives_move(view, sk)
        check("still editing the sketch", gdoc.getInEdit() is not None)
        gdoc.resetEdit()
        settle(0.5)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher").RemBool("EnableEscape")
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").RemInt(
            "OnViewParameterVisibility")
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
