"""The view in sight keeps its camera animation.

A 3D view stops animating some time after it goes to the background
(View/stopAnimatingIfDeactivated, 3 s by default), so that a view left
spinning under a tab does not spin for ever. The timer is armed when
another view is reported maximized and cancelled when the view itself is
reported active. A switch of views reports both, in an order nobody
controls, and when "another view is maximized" came last the view just
switched TO kept a live timer: three seconds after it was opened it
stopped whatever was running in it. A turn of the camera that happened
to straddle that moment was left where it had got to.

The timer now asks again when its time is up, and stops nothing in a
view that is in sight.

Made to happen every time here by a timeout of 0.4 s and a camera
animation of 1.5 s: a turn started in a view as soon as it opens.

Scored against the tree before the fix: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = "User parameter:BaseApp/Preferences/View"
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


def direction(view):
    d = view.getViewDirection()
    return (d.x, d.y, d.z)


def text(d):
    return "(%.3f, %.3f, %.3f)" % d


def apart(a, b):
    return max(abs(x - y) for x, y in zip(a, b))


def run():
    try:
        params = FreeCAD.ParamGet(VIEW)
        params.SetBool("UseNavigationAnimations", True)
        params.SetInt("stopAnimatingIfDeactivated", 400)
        params.SetInt("AnimationDuration", 1500)
        FreeCADGui.getMainWindow().showMaximized()

        # A view left spinning, which the next view will cover.
        doc_a = FreeCAD.newDocument("AnimBehind")
        doc_a.addObject("Part::Box", "Box")
        doc_a.recompute()
        view_a = FreeCADGui.getDocument("AnimBehind").activeView()
        view_a.viewTop()
        settle(2.0)
        view_a.startAnimating(0.0, 1.0, 0.0, 1.0)
        first = direction(view_a)
        settle(0.3)
        check("the first view spins", apart(direction(view_a), first) > 0.05,
              "%s then %s" % (text(first), text(direction(view_a))))

        # The view in sight: a turn to the front, started as it opens.
        doc_b = FreeCAD.newDocument("AnimInSight")
        doc_b.addObject("Part::Box", "Box")
        doc_b.recompute()
        view_b = FreeCADGui.getDocument("AnimInSight").activeView()
        start = direction(view_b)
        view_b.viewFront()
        settle(3.0)
        end = direction(view_b)
        check("the view in sight starts from above", apart(start, (0.0, 0.0, -1.0)) < 1e-3,
              text(start))
        check("its turn to the front runs to the end",
              apart(end, (0.0, 1.0, 0.0)) < 1e-3, text(end))

        # The control: what the timer is for.
        behind = direction(view_a)
        settle(0.5)
        check("the view behind it has stopped spinning",
              apart(direction(view_a), behind) < 1e-6,
              "%s then %s" % (text(behind), text(direction(view_a))))
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
