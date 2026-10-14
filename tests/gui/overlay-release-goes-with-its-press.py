"""A mouse release over a see-through overlay goes to the widget that took the
press, unless the press itself was passed on through the overlay.

docs/HandsOnQueue.md entry 117: "with a split view overlaid by task panel, if
I drag the splitter to close that split and release the mouse inside the
transparent area of the panel, the view is not closed, and the frame visual
stays. Looks like the mouse release event is not passthrough".

The overlays pass a click on what they leave clear on to the view under them.
A release was passed on the same way although its press had not been: the
border of the view cells, dragged and let go over a panel, never got its
release -- the drag went on, its frames stayed on the screen, the cell was not
closed or resized.

A document's view split into two cells, every dock overlaid, the left one
see-through. The mouse goes through the main window as the window system's
would (to the widget under the pointer, or to the one its press grabbed).
Claims, the platform's own pointer (offscreen):
  - a point inside the left overlay where it paints nothing is found;
  - the border pressed and the pointer moved to that point: the drag shows
    its frames; let go there, the frames are gone;
  - and the cells take the border's new place (the drag was carried out);
  - split again, the border dragged and its release never sent: the next
    mouse event that finds the button up ends the drag -- its frames gone,
    the cells as they were, also after a late release (entry 119 by the
    queue's numbering; the user,
    about 11:50: "Will a split drag handle got notified it lost tracking?
    That's the place to hide the tracking frame so they do not stall. Same
    for other handle dragging").

Runs with QT_QPA_PLATFORM=offscreen: the pointer is moved.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from PySide6 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
OPEN_VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/OpenView")
STEPS = []
S = {"released": 0}
Qt = QtCore.Qt


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


def mw():
    return FreeCADGui.getMainWindow()


def cls(w):
    return w.metaObject().className()


def eye(tab, on):
    for button in tab.findChildren(QtWidgets.QAbstractButton):
        if button.objectName() == "OBTN Transparent":
            if button.isChecked() != on:
                button.click()
                settle(800)
            return button.isChecked() == on
    return False


def start():
    doc = FreeCAD.newDocument("ReleaseThrough")
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    settle(800)
    # the document's view the active one: the split splits that
    try:
        mw().setActiveWindow(FreeCADGui.getDocument(doc.Name).activeView())
    except Exception:
        note("INFO the view could not be made the active one: " + traceback.format_exc(0))
    # the platform's screen is 800 wide: two cells of the minimum cell size
    # (300) do not fit beside the panels, and the split would be refused
    OPEN_VIEW.SetInt("MinimumCellSize", 100)
    settle(600)
    FreeCADGui.runCommand("Std_ViewSplitRight")
    settle(1500)
    note("INFO the window %dx%d, view areas %d" % (
        mw().width(), mw().height(),
        len([w for w in mw().findChildren(QtWidgets.QWidget) if cls(w) == "Gui::ViewArea"])))
    tree = None
    for w in QtWidgets.QApplication.allWidgets():
        if cls(w) == "Gui::TreeWidget" and w.isVisible():
            tree = w
    tab = None
    w = tree
    while w is not None:
        if cls(w) == "Gui::OverlayTabWidget":
            tab = w
            break
        w = w.parentWidget()
    if tab is None:
        FreeCADGui.runCommand("Std_DockOverlayAll")
        settle(2500)
        w = tree
        while w is not None:
            if cls(w) == "Gui::OverlayTabWidget":
                tab = w
                break
            w = w.parentWidget()
    S["tab"] = tab
    check("the tree is in an overlay, see-through", tab is not None and eye(tab, True),
          tab.objectName() if tab is not None else None)


def find_spot():
    tab = S["tab"]
    # the border between the two cells: a splitter handle in the view area
    def chain(w):
        names = []
        while w is not None:
            names.append(cls(w))
            w = w.parentWidget()
        return names

    handles = [h for h in mw().findChildren(QtWidgets.QSplitterHandle)
               if h.isVisible() and h.orientation() == Qt.Horizontal
               and any(n == "Gui::ViewArea" for n in chain(h))
               and h.height() > 100]
    check("the view is split in two cells, a border between them", bool(handles),
          [(chain(h)[:5], h.width(), h.height()) for h in mw().findChildren(
              QtWidgets.QSplitterHandle) if h.isVisible()][:6])
    if not handles:
        return
    S["handle"] = handles[0]
    # a point the left overlay leaves clear, inside it and to the left of the
    # border: its lower part, under the tree's few items
    image = tab.grab().toImage().convertToFormat(QtGui.QImage.Format_ARGB32)
    border_x = S["handle"].mapToGlobal(QtCore.QPoint(0, 0)).x()
    spot = None
    for fy in (0.9, 0.8, 0.7, 0.6):
        for fx in (0.5, 0.4, 0.6, 0.3, 0.7):
            x, y = int(image.width() * fx), int(image.height() * fy)
            g = tab.mapToGlobal(QtCore.QPoint(x, y))
            if (image.pixel(x, y) >> 24) & 255 == 0 and g.x() < border_x - 20:
                spot = g
                break
        if spot is not None:
            break
    S["spot"] = spot
    check("a point inside the left overlay where it paints nothing", spot is not None,
          None if spot is None else (spot.x(), spot.y()))


def at(kind, g):
    """A mouse event through the main window, as the window system sends one:
    to the widget under the pointer, or to the one its press grabbed, with
    the application's idea of the buttons kept (QTest's widget events go
    straight to the widget named, past that)"""
    win = mw().windowHandle()
    local = mw().mapFromGlobal(g)
    QtGui.QCursor.setPos(g)
    if kind == "press":
        QtTest.QTest.mousePress(win, Qt.LeftButton, Qt.NoModifier, local)
    elif kind == "move":
        QtTest.QTest.mouseMove(win, local)
    else:
        QtTest.QTest.mouseRelease(win, Qt.LeftButton, Qt.NoModifier, local)


def frames_shown():
    return [w for w in mw().findChildren(QtWidgets.QWidget)
            if w.objectName() == "ViewAreaDragFrames" and w.isVisible()]


def drag():
    handle = S.get("handle")
    spot = S.get("spot")
    if handle is None or spot is None:
        return
    splitter = handle.splitter()
    before = list(splitter.sizes())
    start = handle.mapToGlobal(QtCore.QPoint(handle.width() // 2, handle.height() // 2))
    at("move", start)
    settle(200)
    at("press", start)
    settle(200)
    # towards the spot in a few steps, as a hand does
    for i in range(1, 6):
        at("move", start + (spot - start) * i / 5)
        settle(80)
    settle(200)
    check("dragged over the see-through panel: the drag shows its frames", bool(frames_shown()))
    at("release", spot)
    settle(800)
    check("let go there: the drag's frames are gone", not frames_shown(), len(frames_shown()))
    try:
        after = list(splitter.sizes())
    except RuntimeError:
        after = None
    check("the drag is carried out: the cells are not as they were", after != before,
          "%s -> %s" % (before, after))


def lost():
    """A release that never comes: the drag ends at the next mouse event that
    finds the button up, its frames gone (entry 119, the user's 11:50: "Will
    a split drag handle got notified it lost tracking? That's the place to
    hide the tracking frame so they do not stall")"""
    try:
        mw().setActiveWindow(FreeCADGui.ActiveDocument.ActiveView)
    except Exception:
        pass
    FreeCADGui.runCommand("Std_ViewSplitRight")
    settle(1500)
    handles = [h for h in mw().findChildren(QtWidgets.QSplitterHandle)
               if h.isVisible() and h.orientation() == Qt.Horizontal and h.height() > 100
               and cls(h.splitter()) == "Gui::ViewAreaSplitter"]
    if not check("split again for a second drag", bool(handles)):
        return
    handle = handles[0]
    splitter = handle.splitter()
    before = list(splitter.sizes())
    start = handle.mapToGlobal(QtCore.QPoint(handle.width() // 2, handle.height() // 2))
    at("move", start)
    settle(150)
    at("press", start)
    settle(150)
    for dx in (10, 20, 30):
        at("move", start + QtCore.QPoint(dx, 0))
        settle(80)
    settle(200)
    if not check("the drag shows its frames", bool(frames_shown())):
        at("release", start + QtCore.QPoint(30, 0))
        return
    # the button let go where nothing hears it: the next move finds it up
    point = start + QtCore.QPoint(40, 0)
    QtGui.QCursor.setPos(point)
    under = QtWidgets.QApplication.widgetAt(point) or mw()
    move = QtGui.QMouseEvent(QtCore.QEvent.MouseMove, QtCore.QPointF(under.mapFromGlobal(point)),
                             QtCore.QPointF(point), Qt.NoButton, Qt.NoButton, Qt.NoModifier)
    QtWidgets.QApplication.sendEvent(under, move)
    settle(400)
    check("a move with the button up ends the drag: no frames left", not frames_shown(),
          len(frames_shown()))
    try:
        after = list(splitter.sizes())
    except RuntimeError:
        after = None
    check("and the cells are as they were (given up, not carried out)", after == before,
          "%s -> %s" % (before, after))
    # (the application's idea of the button, put right; the drag is over and
    # takes no notice)
    at("release", point)
    settle(300)
    try:
        after = list(splitter.sizes())
    except RuntimeError:
        after = None
    check("... and stay so after the late release", after == before,
          "%s -> %s" % (before, after))


def finish():
    OPEN_VIEW.RemInt("MinimumCellSize")
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
    QtCore.QTimer.singleShot(800, advance)


STEPS[:] = [start, find_spot, drag, lost]
QtCore.QTimer.singleShot(3000, advance)
