"""A step of the wall clock changes nothing the pointer does.

The time of day is not monotonic: where it is resynced in steps it moves
back (WSL2: about 0.97 s every 32 s). Event stamps are the time of day, so
an interval measured between two of them, or between one and "now", can
come out a second short -- or negative. Three things were told by such an
interval:

- the spin after a rotation (NavigationStyle::doSpin): released within
  100 ms of the last move = a flick. A step after the pointer had rested
  for a second read as a flick and the view span; a step during a flick
  read as none.
- a click against a hold (centerTime in the navigation styles): a middle
  button held for a second and released read as a click, and the view
  recentred on the point under it.
- the hover pick's rate limit (SoFCUnifiedSelection): a move after a step
  was put off to the timer instead of being picked at once.

All three are measured on the steady clock now. The step is made on
demand by a preloaded gettimeofday() (clock-step-shim.c, which Coin's
SbTime::getTimeOfDay reads), -0.97 s at the moment each case names.

Scored against the tree before the fix: see the commit message.
"""
import ctypes
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
STEP = -0.97
DOC = "ClockStep"
L, M, N = QtCore.Qt.LeftButton, QtCore.Qt.MiddleButton, QtCore.Qt.NoButton
E = QtCore.QEvent
state = {"done": False, "shim": None}


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
        time.sleep(0.005)


def step():
    state["shim"].fc_clock_step(STEP)


def viewport():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        name = w.metaObject().className()
        if ("Quarter" in name or "View3DInventorViewer" in name) and w.isVisible():
            return w
    return None


def send(typ, pos, button, buttons):
    w = viewport()
    p = QtCore.QPointF(pos[0], pos[1])
    QtWidgets.QApplication.sendEvent(
        w, QtGui.QMouseEvent(typ, p, w.mapToGlobal(p), button, buttons, QtCore.Qt.NoModifier))


def camera():
    c = FreeCADGui.getDocument(DOC).ActiveView.getCameraNode()
    return (tuple(c.orientation.getValue().getValue()), tuple(c.position.getValue().getValue()))


def reset():
    """Stop a spin left by the case before, and put the view back."""
    p = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    p.SetBool("UseSpinningAnimations", False)
    settle(0.1)
    p.SetBool("UseSpinningAnimations", True)
    view = FreeCADGui.getDocument(DOC).ActiveView
    view.viewIsometric()
    view.fitAll()
    settle(1.2)


def turn(a, b):
    """The rotation that takes orientation a to orientation b."""
    return FreeCAD.Rotation(*b).multiply(FreeCAD.Rotation(*a).inverted())


def drag(rest, step_at_rest=False, step_mid=False, duration=0.7):
    """Rotate with middle + left (the CAD style), then release the left
    button. Returns (spinning, direction): direction is +1 when the spin
    carries on the way the drag went."""
    reset()
    w = viewport()
    x, y = w.width() // 2 - 60, w.height() // 2 + 40
    send(E.MouseMove, (x, y), N, N)
    send(E.MouseButtonPress, (x, y), M, M)
    settle(0.05)
    send(E.MouseButtonPress, (x, y), L, M | L)
    q0 = camera()[0]
    moves = int(duration / 0.02)
    for i in range(moves):
        if step_mid and i == moves - 2:
            step()
        x += 3
        send(E.MouseMove, (x, y), N, M | L)
        if i < moves - 1:
            settle(0.02)
    settle(rest)
    if step_at_rest:
        step()
    send(E.MouseButtonRelease, (x, y), L, M)
    q1 = camera()[0]
    settle(0.5)
    q2 = camera()[0]
    send(E.MouseButtonRelease, (x, y), M, N)
    settle(0.1)
    spinning = max(abs(a - b) for a, b in zip(q1, q2)) > 1e-4
    if not spinning:
        return False, 0.0
    d, s = turn(q0, q1), turn(q1, q2)
    return True, round(d.Axis.dot(s.Axis) * (1 if d.Angle * s.Angle >= 0 else -1), 2)


def hold(seconds, stepped=False):
    """Press the middle button, keep it down without moving, release.
    Returns whether the view recentred."""
    reset()
    w = viewport()
    x, y = w.width() // 2 - 60, w.height() // 2 + 40
    send(E.MouseMove, (x, y), N, N)
    settle(0.6)
    before = camera()[1]
    send(E.MouseButtonPress, (x, y), M, M)
    settle(seconds)
    if stepped:
        step()
    send(E.MouseButtonRelease, (x, y), M, N)
    settle(1.0)
    after = camera()[1]
    return max(abs(a - b) for a, b in zip(before, after)) > 1e-3


def preselected():
    try:
        return list(FreeCADGui.Selection.getPreselection().SubElementNames)
    except Exception:
        return []


def hover(stepped=False):
    """Rest on the box's top face, then move to its front face. Returns
    the preselection on the top, right after the move, and later."""
    reset()
    view = FreeCADGui.getDocument(DOC).ActiveView
    w = viewport()

    def pixel(point):
        p = view.getPointOnViewport(FreeCAD.Vector(*point))
        return (p[0], w.height() - 1 - p[1])

    top, front = pixel((5, 5, 10)), pixel((5, 0, 5))
    send(E.MouseMove, (top[0] - 2, top[1]), N, N)
    settle(0.4)
    send(E.MouseMove, top, N, N)
    settle(0.4)  # well past the preselection delay
    first = preselected()
    if stepped:
        step()
    send(E.MouseMove, front, N, N)
    at_once = preselected()
    settle(0.4)
    return first, at_once, preselected()


def run():
    try:
        path = os.environ.get("FC_CLOCK_SHIM", "")
        if not path or path not in os.environ.get("LD_PRELOAD", ""):
            note("ABORT: the clock shim is not preloaded (FC_CLOCK_SHIM / LD_PRELOAD)")
            finish()
            return
        shim = ctypes.CDLL(path)
        shim.fc_clock_step.argtypes = [ctypes.c_double]
        shim.fc_clock_calls.restype = ctypes.c_long
        state["shim"] = shim

        p = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        p.SetString("NavigationStyle", "Gui::CADNavigationStyle")
        p.SetBool("UseNavigationAnimations", True)
        p.SetBool("UseSpinningAnimations", True)
        p.SetFloat("PreSelectionDelay", 0.1)
        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        settle(2.0)

        calls = shim.fc_clock_calls()
        send(E.MouseMove, (5, 5), N, N)
        settle(0.2)
        check("the shim is what reads the time of day", shim.fc_clock_calls() > calls,
              "%d -> %d" % (calls, shim.fc_clock_calls()))

        got = drag(1.0)
        check("a rotation released after a rest does not spin", got == (False, 0.0), got)
        got = drag(1.0, step_at_rest=True)
        check("... nor when the clock steps back during the rest", got == (False, 0.0), got)
        got = drag(0.0)
        check("a rotation released while moving spins on the same way", got == (True, 1.0), got)
        got = drag(0.0, step_mid=True)
        check("... and when the clock steps back during the drag", got == (True, 1.0), got)

        got = hold(1.0)
        check("a middle button held for a second is not a click", got is False, got)
        got = hold(1.0, stepped=True)
        check("... nor when the clock steps back while it is held", got is False, got)
        got = hold(0.1)
        check("a short middle click recentres the view", got is True, got)

        first, at_once, later = hover()
        check("a move to another face is picked at once",
              bool(first) and bool(later) and first != later and at_once == later,
              (first, at_once, later))
        first, at_once, later = hover(stepped=True)
        check("... and when the clock steps back before the move",
              bool(first) and bool(later) and first != later and at_once == later,
              (first, at_once, later))
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
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
