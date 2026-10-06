"""Making a view the active window makes its cell the active cell.

A 3D view sits in a cell of a Gui::ViewArea, and the area is the MDI tab
(docs/SplitViews.md). The main window's active view is the embedded view
(sec 20 there); the area has an active CELL of its own, and answers with
that cell's view whenever it is the one asked: the maximize command, the
main window becoming the active window again, a tab switched back to. And
a cell is made the active one by the keyboard focus moving into it.

MainWindow::setActiveWindow(view) -- what "create new view", a "go to
this view" button, an edit starting and Gui.getMainWindow()
.setActiveWindow() all come down to -- recorded the view and left both the
area's active cell and the keyboard where they were. So the border was
drawn round the other cell, "maximize view cell" maximized the other
cell, the next activation of the main window handed the active view back
to it, and a click into the cell that still held the keyboard moved no
focus and so did nothing at all: after "create new view" the first view
could not be clicked back into.

Two views a1 and a2 of one document, in two cells:

  - right after the second view is created, a click into the first makes
    it the active view;
  - a click into either makes that one the active view;
  - with a1 clicked into and a2 then made the active window, a2 is the
    active view, and maximizing the view cell maximizes a2's;
  - a click back into a1 makes it the active view again;
  - after the main window is activated again a2, made the active window,
    is still the active view.

The clicks are the real pointer's, through the window system (XTEST on
X11): a mouse event sent through Qt to a 3D view is not handled as a
click of the pointer is, and does not move the keyboard focus there.

Run through scripts/gui-test.sh, or by hand as `FreeCAD <this script>`
with GT_OUT set.
"""
import ctypes
import ctypes.util
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets
from PySide6 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ActiveCell"
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")


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
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


def cls(w):
    return w.metaObject().className()


def widget_of(view):
    w = view.graphicsView()
    while w is not None and cls(w) != "Gui::View3DInventor":
        w = w.parentWidget()
    return w


def cell_of(view):
    w = widget_of(view)
    while w is not None and cls(w) != "Gui::ViewAreaCell":
        w = w.parentWidget()
    return w


def click_into(view):
    """A click of the real pointer in the middle of the view."""
    widget = widget_of(view)
    at = widget.mapToGlobal(widget.rect().center())
    if hasattr(ctypes, "windll"):
        # No pointer to press here: the nearest thing Qt has
        QtTest.QTest.mouseClick(widget, QtCore.Qt.LeftButton)
        settle(400)
        return
    x11 = ctypes.CDLL(ctypes.util.find_library("X11") or "libX11.so.6")
    xtst = ctypes.CDLL(ctypes.util.find_library("Xtst") or "libXtst.so.6")
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
    x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
    xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                                          ctypes.c_int, ctypes.c_ulong]
    xtst.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int,
                                          ctypes.c_ulong]
    display = x11.XOpenDisplay(None)
    if not display:
        raise RuntimeError("no X display to click on")
    try:
        xtst.XTestFakeMotionEvent(display, -1, at.x(), at.y(), 0)
        x11.XSync(display, 0)
        settle(150)
        xtst.XTestFakeButtonEvent(display, 1, 1, 0)
        x11.XSync(display, 0)
        settle(80)
        xtst.XTestFakeButtonEvent(display, 1, 0, 0)
        x11.XSync(display, 0)
    finally:
        x11.XCloseDisplay(display)
    settle(400)


def active_is(view):
    active = FreeCADGui.ActiveDocument.ActiveView if FreeCADGui.ActiveDocument else None
    return active is not None and (active is view or active == view)


def run():
    try:
        mw = FreeCADGui.getMainWindow()
        VIEW.SetBool("ShowNaviCube", False)
        VIEW.SetBool("UseNavigationAnimations", False)
        doc = FreeCAD.newDocument(DOC)
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        settle(500)
        FreeCADGui.runCommand("Std_ViewCreate")
        settle(600)
        views = FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")
        if len(views) != 2 or cell_of(views[0]) is None or cell_of(views[0]) == cell_of(views[1]):
            note("ABORT the document's two views are not two cells")
            return
        a1, a2 = views
        # The one that is not the active view is the first: the new view
        # was made the active one, and the keyboard was in the first
        if active_is(a1):
            a1, a2 = a2, a1
        click_into(a1)
        check("right after the second view is created, a click into the first makes it "
              "the active view", active_is(a1))
        click_into(a2)
        check("a click into the second makes that one the active view", active_is(a2))
        click_into(a1)
        check("and a click into the first, the first", active_is(a1))

        mw.setActiveWindow(a2)
        settle(400)
        check("a2, made the active window, is the active view", active_is(a2))
        FreeCADGui.runCommand("Std_ViewSplitMaximize")
        settle(500)
        check("maximizing the view cell maximizes a2's: a1's cell is hidden",
              cell_of(a2).isVisible() and not cell_of(a1).isVisible(),
              "a1 visible %s, a2 visible %s" % (cell_of(a1).isVisible(), cell_of(a2).isVisible()))
        FreeCADGui.runCommand("Std_ViewSplitMaximize")
        settle(500)
        check("restored, both cells are shown", cell_of(a1).isVisible() and cell_of(a2).isVisible())

        click_into(a1)
        mw.setActiveWindow(a2)
        settle(400)
        click_into(a1)
        check("a click back into a1 makes it the active view again", active_is(a1))

        mw.setActiveWindow(a2)
        settle(400)
        # What QApplication sends the main window when it becomes the
        # active one again. Without a window manager (a bare xvfb) the
        # window may not be active at all, and then the handler does
        # nothing and the claim below holds either way.
        if not mw.isActiveWindow():
            note("NOTE the main window is not active: its activation handler is not reached")
        QtWidgets.QApplication.sendEvent(mw, QtCore.QEvent(QtCore.QEvent.ActivationChange))
        settle(300)
        check("a2 is still the active view after the main window is activated again",
              active_is(a2))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        for name in list(FreeCAD.listDocuments().keys()):
            FreeCAD.closeDocument(name)
        note("DONE")
        QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
