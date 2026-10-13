"""The custom title bar keeps its room at the top of the main window, and the
button that reveals the menu keeps its width.

docs/HandsOnQueue.md entry 110: "sometimes the margin before the toolbar
docked in customized title bar gets bigger", "the marging is completely gone
now", "the logo and the menu reveal button does not seem to move, only the
docked toolbar moved further and sometimes closer"; and "the first normal
toolbar row is stacked underneath what the customized title bar's place"
(entry 6 seen again).

Two things.

The button: every sheet gives a push button a least width of 80, and the
style wrote that over the button's own width whenever it polished the button
anew, so it was 50 wide or 88 by which had the last word, and the tool bar
docked after it stood nearer or further.

The room: with a custom title bar the main window's menu-widget slot holds a
spacer as high as the bar, which is laid over it. A plain
QMainWindow::menuBar() -- every call from Python is one -- makes an empty
menu bar INTO that slot. The window has a guard that puts the spacer back,
but Qt hides the widget it displaces, takes it out of the window and deletes
it a moment later: the guard put back a spacer that reserved nothing and was
gone, and the first row of tool bars lay under the title bar for good.

Claims, where the main window has a custom title bar (elsewhere: NOT RUN):
  - the first row of tool bars begins below the title bar;
  - the menu reveal button has the width it asks for, and has it still
    after its style is unpolished and polished again;
  - after mainWindow.menuBar() from this script, at once and three seconds
    later, the first row of tool bars is where it was, and so is the first
    view.

The pointer is not moved.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
STEPS = []
S = {}


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


def cls(w):
    return w.metaObject().className()


def mw():
    return FreeCADGui.getMainWindow()


def title_bar():
    for child in mw().children():
        if isinstance(child, QtWidgets.QWidget) and cls(child).endswith("TitleBarWidget") \
                and child.isVisible():
            return child
    return None


def first_row():
    """y of the first row of tool bars that are the window's own"""
    rows = [bar.mapTo(mw(), QtCore.QPoint(0, 0)).y()
            for bar in mw().findChildren(QtWidgets.QToolBar)
            if bar.isVisible() and bar.parentWidget() is mw()
            and mw().toolBarArea(bar) == QtCore.Qt.TopToolBarArea]
    return min(rows) if rows else None


def mdi_top():
    mdi = mw().findChild(QtWidgets.QMdiArea)
    return mdi.mapTo(mw(), QtCore.QPoint(0, 0)).y() if mdi else None


def start():
    bar = title_bar()
    if bar is None:
        note("NOT RUN: the main window has no custom title bar here (platform %r)"
             % QtGui.QGuiApplication.platformName())
        STEPS[:] = []
        return
    S["bar"] = bar.height()
    S["row"] = first_row()
    S["mdi"] = mdi_top()
    if not check("the window has tool bars in its top area", S["row"] is not None):
        STEPS[:] = []
        return
    check("the first row of tool bars begins below the title bar", S["row"] >= S["bar"],
          "row at %s, the title bar %s high" % (S["row"], S["bar"]))


def button():
    logo = mw().findChild(QtWidgets.QPushButton, "titleBarLogo")
    if logo is None or not logo.isVisible():
        note("INFO no menu reveal button shown: the menu is not folded here")
        return
    want = logo.sizeHint().width()
    check("the menu reveal button has the width it asks for", logo.width() == want,
          "%d wide, asks for %d" % (logo.width(), want))
    logo.style().unpolish(logo)
    logo.style().polish(logo)
    settle(600)
    check("and has it still after it is polished again", logo.width() == want,
          "%d wide, least %d, asks for %d" % (logo.width(), logo.minimumWidth(), want))


def menu_bar():
    bar = mw().menuBar()
    note("INFO mainWindow.menuBar() answered a %s with %d actions"
         % (cls(bar), len(bar.actions())))
    settle(200)
    check("right after menuBar() the first row of tool bars is where it was",
          first_row() == S["row"], (first_row(), S["row"]))


def later():
    settle(3000)
    check("three seconds later it still is", first_row() == S["row"], (first_row(), S["row"]))
    check("and so is the first view", mdi_top() == S["mdi"], (mdi_top(), S["mdi"]))
    mw().grab(QtCore.QRect(0, 0, min(mw().width(), 900), 130)).save(
        os.path.join(OUT, "top.png"))


def finish():
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, mw().close)


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
    QtCore.QTimer.singleShot(1000, advance)


STEPS[:] = [start, button, menu_bar, later]
QtCore.QTimer.singleShot(3000, advance)
