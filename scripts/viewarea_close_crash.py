"""Reproduce the _purecall abort when a split view's document closes.

    FreeCAD scripts/viewarea_close_crash.py       (or run it over MCP)

THE CRASH.  Deleting a View3DInventor that a ViewArea hosts aborts
through VCRUNTIME140!_purecall -- cdb reports it as c0000409 "stack
buffer overrun", subcode 7 FAST_FAIL_FATAL_APP_EXIT, which is what the
CRT raises from abort().

    ucrtbase!abort
    VCRUNTIME140!_purecall
    QtPrivate::QCallableObject<void (Gui::MDIView::*)(QWidget*)>::impl
    Qt6Core!QMetaObject::activate
    Gui::MainWindow::windowStateChanged        moc_MainWindow.cpp:435
    Gui::MainWindow::eventFilter               MainWindow.cpp:1452
    ... Qt MDI: setWindowState / showMaximized / changeEvent ...
    Gui::MainWindow::setActiveWindow           MainWindow.cpp:1649
    Gui::ViewArea::setActiveCell               ViewArea.cpp:1258
    Gui::ViewArea::collapseCell                ViewArea.cpp:1214
    Gui::ViewArea::childViewGone               ViewArea.cpp:1235
    Qt6Core!QObject::destroyed
    Qt6Widgets!QWidget::~QWidget
    Gui::View3DInventor::`vector deleting destructor'

The view being destroyed emits destroyed() from inside ~QWidget -- by
which point ~View3DInventor and ~MDIView have already run and the vtable
is QWidget's.  ViewAreaCell's destroyed handler collapses the cell,
which re-enters MDI activation, maximizes a sibling sub-window, and that
WindowStateChange makes MainWindow re-emit windowStateChanged.  The
dying view is STILL CONNECTED to it: the connection is made in
ViewAreaCell::hostView (ViewArea.cpp:390) and MainWindow::addWindow
(MainWindow.cpp:1541), and Qt's own automatic disconnect does not happen
until ~QObject, which is later than the ~QWidget we are inside.  So the
signal dispatches MDIView::windowStateChanged through a vtable that no
longer carries it.

WHY IT LOOKED INTERMITTENT.  It needs a ViewArea holding MORE THAN ONE
cell: with a single cell childViewGone takes the deleteSelf branch and
never reaches collapseCell.  A plain unsplit view never hits it.  Split
first and it reproduces on the first close.
"""
import time

import FreeCAD
import FreeCADGui
from PySide import QtWidgets

ROUNDS = 2


def say(m):
    FreeCAD.Console.PrintMessage("[vc] %s\n" % m)


def pump(n=8):
    for _ in range(n):
        time.sleep(0.05)
        FreeCADGui.updateGui()


def split_active(target=3):
    """Split the active view until its ViewArea holds `target` cells.

    ! The command acts on getMainWindow()->activeWindow(), and a freshly
    created document's sub-window is not active yet -- an immediate
    split silently does nothing and the round then proves nothing.
    Activate the sub-window by hand first and verify the count grew.
    """
    mdi = FreeCADGui.getMainWindow().findChild(QtWidgets.QMdiArea)
    subs = mdi.subWindowList()
    if subs:
        mdi.setActiveSubWindow(subs[-1])
        # ! MAXIMIZED, and that is load-bearing. MainWindow::addWindow
        # maximizes only the FIRST window ("if (isempty)"); the rest are
        # plain show(). Activating a sub-window while the outgoing one
        # is maximized is what makes Qt call showMaximized on the
        # incoming one -- the QWidget::setWindowState / showMaximized
        # frames in the crash stack. Without it no window state changes,
        # MainWindow never re-emits windowStateChanged, and the round
        # passes while testing nothing.
        subs[-1].showMaximized()
        pump()
    for _ in range(target + 3):
        n = len(FreeCADGui.ActiveDocument.mdiViewsOfType(
            "Gui::View3DInventor"))
        if n >= target:
            break
        FreeCADGui.runCommand("Std_ViewSplitRight", 0)
        pump()
    return len(FreeCADGui.ActiveDocument.mdiViewsOfType(
        "Gui::View3DInventor"))


def main():
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document") \
        .SetBool("AutoSaveEnabled", False)
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    pump()

    for r in range(ROUNDS):
        # SEVERAL documents, so the MDI area holds several sub-windows.
        # With only one, ViewArea::setActiveCell re-activates the very
        # sub-window that is already active, Qt changes no window state,
        # and MainWindow never re-emits windowStateChanged -- the round
        # passes and proves nothing. The original crash had six stale
        # probe documents open.
        names = []
        for i in range(3):
            doc = FreeCAD.newDocument("AreaCrash%d" % i)
            box = doc.addObject("Part::Box", "Box")
            box.Length = box.Width = box.Height = 10.0
            doc.recompute()
            pump()
            n = split_active(3)
            names.append(doc.Name)
            say("round %d: %s has %d views" % (r + 1, doc.Name, n))

        for name in names:
            say("round %d: closing %s" % (r + 1, name))
            FreeCAD.closeDocument(name)
            pump(10)
            say("round %d: %s closed, still alive" % (r + 1, name))

    say("ALL %d ROUNDS SURVIVED" % ROUNDS)


main()
