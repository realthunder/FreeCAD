"""A change of the 3D view's background pattern asks the view to draw again.

docs/HandsOnQueue.md entry 115: "3d view background color and gradient pattern
does not follow settings. my preference is radial, what shows is linear and
does not response to setting change" -- "color change is responsive instantly.
gradient pattern change not, but it does apply if I move mouse over 3d view".

The pattern is a member of the background's node that nothing was told of,
and the background is not under the scene a change of which draws the view
again: the view showed the old pattern until something else drew it. A
change of the colours asked for the redraw itself; a change of the pattern
does now.

Claims, counting the paint events the view's own widget gets:
  - the pattern changed from linear to radial: the view is painted again;
  - back to linear, to one colour, to radial from one colour: again each
    time;
  - the same pattern set again: it is not (no redraw for nothing).

Runs with QT_QPA_PLATFORM=offscreen as well as on a screen; the pointer is not
moved. Not by pictures: a picture is taken by drawing the view again, which is
the very thing the fault left out.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
STEPS = []
S = {"told": 0}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=300):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


class Painted(QtCore.QObject):
    """Counts the paint events of the widget it is put on"""

    def eventFilter(self, _watched, event):
        if event.type() == QtCore.QEvent.Paint:
            S["told"] += 1
        return False


def pattern(linear, radial):
    """Set the two keys as the preference page writes them and say how often
    the view was painted meanwhile"""
    settle(600)
    S["told"] = 0
    VIEW.SetBool("Gradient", linear)
    VIEW.SetBool("RadialGradient", radial)
    settle(600)
    return S["told"]


def start():
    S["was"] = (VIEW.GetBool("Gradient", True), VIEW.GetBool("RadialGradient", False))
    VIEW.SetBool("Gradient", True)
    VIEW.SetBool("RadialGradient", False)
    doc = FreeCAD.newDocument("PatternRedraw")
    view = FreeCADGui.getDocument(doc.Name).mdiViewsOfType("Gui::View3DInventor")[0]
    settle(1500)
    S["widget"] = view.graphicsView().viewport()
    S["filter"] = Painted()
    S["widget"].installEventFilter(S["filter"])
    settle(800)
    S["told"] = 0
    settle(1200)
    check("left alone, the view is not painted (the premise)", S["told"] == 0, S["told"])


def change():
    n = pattern(False, True)
    check("linear to radial: the view is painted again", n >= 1, n)
    n = pattern(True, False)
    check("radial to linear: painted again", n >= 1, n)
    n = pattern(True, False)
    check("the same pattern set again: not painted", n == 0, n)
    n = pattern(False, False)
    check("to one colour: painted again", n >= 1, n)
    n = pattern(False, True)
    check("and to radial from one colour: painted again", n >= 1, n)


def finish():
    if "filter" in S:
        S["widget"].removeEventFilter(S["filter"])
    VIEW.SetBool("Gradient", S["was"][0])
    VIEW.SetBool("RadialGradient", S["was"][1])
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


def advance():
    if not STEPS:
        finish()
        return
    step = STEPS.pop(0)
    try:
        step()
    except Exception:
        note("ABORT in %s: %s" % (step.__name__, traceback.format_exc().replace("\n", " | ")))
        STEPS[:] = []
    QtCore.QTimer.singleShot(800, advance)


STEPS[:] = [start, change]
QtCore.QTimer.singleShot(2500, advance)
