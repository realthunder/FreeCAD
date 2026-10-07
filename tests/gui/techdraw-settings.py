"""The settings of TechDraw's General group are listed, and a new page still takes them.

Thirty-five keys of Preferences/Mod/TechDraw/General are behind
TechDraw::TechDrawParams (docs/HandsOnQueue.md entry 24). TechDraw reads
its settings through hand-written accessors and at many places straight
from the group, each with a default of its own; they take the class's now.
Its pages are held to the definitions by
preferences-ok-keeps-defaults.py, which named three of them when the group
was defined: the new face finder is off to the program and was shown on by
the Advanced page; the vertex scale is 3 to the program and 5 on the Scale
page; the template mark size is 5 to the program and 3 on the page. OK
stored the page's each time.

Forty-six more followed, of the sub-groups Decorations, Dimensions, HLR,
PAT, Colors, Labels, LeaderLine, Rez, Tracker and debug -- those whose
default is a plain value. Two pages were named again: centre marks are off
to the program and were shown on by the Annotation page; the tolerance text
size is 0.5 to the program and 0.8 on the Dimensions page. The colours, the
line standards and the file names of TechDraw are not done yet.

Claims:

  - "/param vertex scale" and "/param new face finder" list the settings
    of the sub-group General, "/param tol size adjust" and "/param show
    center marks" those of Dimensions and Decorations;
  - a view made with "smooth edges" stored off does not show them, one
    made without the key does;
  - a page made with the page scale stored as 2 has a scale of 2, one made
    without the key 1;
  - a page made with "keep pages up to date" stored off does not keep
    itself updated, one made without the key does.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PREFS = "User parameter:BaseApp/Preferences/"
Qt = QtCore.Qt


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.005)


def param_rows(query):
    FreeCADGui.runCommand("Std_OmniSearch")
    settle(0.6)
    edit = None
    for w in QtWidgets.QApplication.allWidgets():
        if w.objectName() == "OmniSearchEdit" and w.isVisible():
            edit = w
    if edit is None:
        return []
    text = "/param " + query
    edit.setText(text)
    edit.setCursorPosition(len(text))
    edit.textEdited.emit(text)
    settle(0.8)
    rows = []
    for w in QtWidgets.QApplication.topLevelWidgets():
        if isinstance(w, QtWidgets.QAbstractItemView) and w.isVisible() and w.model() is not None:
            rows += [str(w.model().index(i, 0).data()) for i in range(w.model().rowCount())]
    QtWidgets.QApplication.sendEvent(edit, QtGui.QKeyEvent(QtCore.QEvent.KeyPress, Qt.Key_Escape, Qt.NoModifier))
    settle(0.5)
    return rows


def run():
    general = FreeCAD.ParamGet(PREFS + "Mod/TechDraw/General")
    hlr = FreeCAD.ParamGet(PREFS + "Mod/TechDraw/HLR")
    doc = None
    try:
        import TechDraw  # noqa: F401  the module registers its settings when it is loaded
        settle(0.5)

        rows = param_rows("vertex scale")
        check("the omni search lists TechDraw's vertex scale",
              any(r.endswith("Mod/TechDraw/General/VertexScale") for r in rows), rows[:6])
        rows = param_rows("new face finder")
        check("and its face finder switch", any(r.endswith("Mod/TechDraw/General/NewFaceFinder") for r in rows),
              rows[:6])

        rows = param_rows("tol size adjust")
        check("and the tolerance text size of the Dimensions group",
              any(r.endswith("Mod/TechDraw/Dimensions/TolSizeAdjust") for r in rows), rows[:6])
        rows = param_rows("show center marks")
        check("and the centre marks switch of the Decorations group",
              any(r.endswith("Mod/TechDraw/Decorations/ShowCenterMarks") for r in rows), rows[:6])

        doc = FreeCAD.newDocument("Entry24TechDraw")
        settle(0.5)
        hlr.SetBool("SmoothViz", False)
        settle(0.2)
        hidden = doc.addObject("TechDraw::DrawViewPart", "ViewNoSmooth").SmoothVisible
        hlr.RemBool("SmoothViz")
        settle(0.2)
        shown = doc.addObject("TechDraw::DrawViewPart", "ViewSmooth").SmoothVisible
        check("a view made with 'smooth edges' stored off does not show them, one made without the key does",
              hidden is False and shown is True, (hidden, shown))

        general.SetFloat("DefaultScale", 2.0)
        settle(0.2)
        two = doc.addObject("TechDraw::DrawPage", "PageScale2").Scale
        general.RemFloat("DefaultScale")
        settle(0.2)
        one = doc.addObject("TechDraw::DrawPage", "PageScale1").Scale
        check("a page made with the page scale stored as 2 has 2, one made without the key 1",
              abs(two - 2.0) < 1e-9 and abs(one - 1.0) < 1e-9, (two, one))

        general.SetBool("KeepPagesUpToDate", False)
        settle(0.2)
        off = doc.addObject("TechDraw::DrawPage", "PageStill").KeepUpdated
        general.RemBool("KeepPagesUpToDate")
        settle(0.2)
        on = doc.addObject("TechDraw::DrawPage", "PageLive").KeepUpdated
        check("a page made with 'keep pages up to date' stored off does not, one made without the key does",
              off is False and on is True, (off, on))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        general.RemFloat("DefaultScale")
        general.RemBool("KeepPagesUpToDate")
        hlr.RemBool("SmoothViz")
        if doc is not None:
            FreeCAD.closeDocument(doc.Name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
