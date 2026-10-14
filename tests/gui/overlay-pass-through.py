"""The clear part of a see-through overlay passes the pointer, a click and the
wheel to the 3D view under it.

docs/HandsOnQueue.md entry 86: "overlay transparent area mouse pass through
no longer works", then, retested: "tree view/python console/report view can
click through, but not wheel through. task panel, no click or wheel
through".

The wheel: OverlayManager's event filter let a wheel event fall through
into its handling of a context menu event and asked it, as if it were one,
for the reason it was raised -- a read of whatever a wheel event holds at
that place. Where that is not 0 the wheel is never passed on.

Runs only with QT_QPA_PLATFORM=offscreen: it moves the pointer.

Claims, two plates under the whole view, all docks overlaid:
  - with an overlay's transparent mode ON, over the clear part of the tree's
    panel: a move preselects the plate under it, a click selects it, and the
    wheel zooms the view (the camera's height changes);
  - the same over the clear part of the Tasks panel, no panel shown in it;
  - with the transparent mode OFF a move over the tree's clear part
    preselects nothing new: the panel takes it, as it is meant to.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from PySide6 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
QTest = QtTest.QTest
STEPS = []


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=400):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def mw():
    return FreeCADGui.getMainWindow()


def cls(w):
    return w.metaObject().className()


def overlays():
    out = {}
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == "Gui::OverlayTabWidget" and w.isVisible() and w.count():
                out[w.objectName()] = w
        except RuntimeError:
            pass
    return out


def tree_view():
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == "Gui::TreeWidget" and w.isVisible():
                return w
        except RuntimeError:
            pass
    return None


def overlay_of(w):
    while w is not None:
        if cls(w) == "Gui::OverlayTabWidget":
            return w
        w = w.parentWidget()
    return None


def rows(view):
    out = []
    index = view.indexAt(QtCore.QPoint(4, 4))
    while index.isValid():
        out.append(index)
        index = view.indexBelow(index)
    return out


def eye(tab, on):
    """The transparent mode of one overlay, by its own button"""
    for button in tab.findChildren(QtWidgets.QAbstractButton):
        if button.objectName() == "OBTN Transparent":
            if button.isChecked() != on:
                button.click()
                settle(800)
            return button.isChecked() == on
    return False


def preselected():
    try:
        pre = FreeCADGui.Selection.getPreselection()
        return pre.Object.Name if pre and pre.Object else None
    except Exception:
        return None


def selected():
    return [o.Name for o in FreeCADGui.Selection.getSelection()]


def pointer(gpos):
    """The pointer to a point of the screen, with a small move at the end: a
    view preselects on a move"""
    QtGui.QCursor.setPos(gpos + QtCore.QPoint(3, 3))
    settle(150)
    QtGui.QCursor.setPos(gpos)
    settle(700)


def camera_height():
    return FreeCADGui.ActiveDocument.ActiveView.getCameraNode().height.getValue()


def wheel(gpos, steps=3):
    """Notches of the wheel at the pointer, sent to the widget under it"""
    for _ in range(steps):
        under = QtWidgets.QApplication.widgetAt(gpos)
        if under is None:
            return
        local = QtCore.QPointF(under.mapFromGlobal(gpos))
        ev = QtGui.QWheelEvent(local, QtCore.QPointF(gpos), QtCore.QPoint(), QtCore.QPoint(0, 120),
                               QtCore.Qt.NoButton, QtCore.Qt.NoModifier,
                               QtCore.Qt.NoScrollPhase, False)
        QtWidgets.QApplication.sendEvent(under, ev)
        settle(250)
    settle(600)


def clear_at(tab, gpos, reach=10):
    """Whether the overlay paints nothing at a point of the screen and within
    reach of it: a click is passed where the next two pixels are clear, the
    wheel where the next eight are"""
    image = tab.grab().toImage().convertToFormat(QtGui.QImage.Format_ARGB32)
    at = tab.mapFromGlobal(gpos)
    for dx in range(-reach, reach + 1):
        for dy in range(-reach, reach + 1):
            p = at + QtCore.QPoint(dx, dy)
            if image.rect().contains(p) and (image.pixel(p) >> 24) & 255:
                return False
    return True


def away():
    """The pointer over the middle of the view, clear of the panels at its sides"""
    FreeCADGui.Selection.clearSelection()
    pointer(mw().mapToGlobal(mw().rect().center()))


def through(name, tab, spot, plate):
    away()
    if not check("%s: the spot is clear, nothing painted next to it" % name,
                 clear_at(tab, spot, 3)):
        return
    pointer(spot)
    check("%s: a move over its clear part preselects the plate under it" % name,
          preselected() == plate, preselected())
    # the wheel before the click: a selection puts things into a Tasks panel
    if clear_at(tab, spot, 10):
        before = camera_height()
        wheel(spot)
        after = camera_height()
        check("%s: the wheel there zooms the view" % name,
              abs(after - before) > 1e-6 * abs(before),
              "camera height %.4f, before %.4f" % (after, before))
        pointer(spot)
    else:
        note("INFO %s: something is painted within the wheel's reach of the spot; the wheel is "
             "not looked at here" % name)
    under = QtWidgets.QApplication.widgetAt(spot)
    if under is not None:
        QTest.mouseClick(under, QtCore.Qt.LeftButton, QtCore.Qt.NoModifier,
                         under.mapFromGlobal(spot))
        settle(600)
    check("%s: a click there selects that plate" % name, selected() == [plate], selected())
    FreeCADGui.Selection.clearSelection()


def start():
    platform = QtGui.QGuiApplication.platformName()
    if platform != "offscreen":
        note("NOT RUN: the platform is %r; this script moves the pointer and runs only with "
             "QT_QPA_PLATFORM=offscreen" % platform)
        STEPS[:] = []
        return
    # The window as it comes, inside this platform's screen of 800 by 800:
    # a point of a larger window that lies beyond the screen is under no
    # window at all, by this platform, and nothing is passed on from there.
    doc = FreeCAD.newDocument("PassThrough")
    for name, x in (("LeftPlate", -400), ("RightPlate", 0)):
        plate = doc.addObject("Part::Box", name)
        plate.Length, plate.Width, plate.Height = 400, 800, 1
        plate.Placement.Base = FreeCAD.Vector(x, -400, 0)
    doc.recompute()
    view = FreeCADGui.ActiveDocument.ActiveView
    view.setCameraType("Orthographic")
    view.viewTop()
    FreeCADGui.SendMsgToActiveView("ViewFit")
    settle(600)
    # close in: the two plates under the WHOLE view, the left one under its left half
    view.getCameraNode().height.setValue(300.0)
    FreeCADGui.Selection.clearSelection()


def overlay_all():
    # GT_THEME: a theme applied first, for a run on a configuration of the
    # reporter's that names an older overlay sheet
    theme = os.environ.get("GT_THEME")
    if theme:
        FreeCADGui.applyTheme(theme)
        settle(5000)
    tree = tree_view()
    if tree is None or overlay_of(tree) is None:
        FreeCADGui.runCommand("Std_DockOverlayAll")


def run():
    tree = tree_view()
    left = overlay_of(tree) if tree is not None else None
    if not check("the tree view is in an overlay", left is not None):
        return
    area = tree.viewport()
    shown = rows(tree)
    bottom = tree.visualRect(shown[-1]).bottom() if shown else 0
    tree_spot = area.mapToGlobal(QtCore.QPoint(area.width() // 2, (bottom + area.height()) // 2))

    away()
    check("clear of any panel a move preselects the plate under the pointer",
          preselected() in ("LeftPlate", "RightPlate"), preselected())

    # the mode off: the panel takes the pointer
    if check("the tree's overlay has its transparent mode off", eye(left, False)):
        away()
        before = preselected()
        pointer(tree_spot)
        check("transparent mode off: a move over the tree's clear part is the panel's",
              preselected() != "LeftPlate" or before == "LeftPlate",
              "%s, before %s" % (preselected(), before))

    if check("the tree's overlay has its transparent mode on", eye(left, True)):
        through("the tree's panel", left, tree_spot, "LeftPlate")

    tasks = None
    for tab in overlays().values():
        for i in range(tab.count()):
            if tab.tabText(i).replace("&", "") in ("Tasks", "Task View", "Task", "Combo View"):
                tasks = tab
    if check("the Tasks panel is in an overlay", tasks is not None,
             {n: [t.tabText(i) for i in range(t.count())] for n, t in overlays().items()}):
        if tasks.currentIndex() != [i for i in range(tasks.count())
                                    if tasks.tabText(i).replace("&", "").startswith(("Task", "Combo"))][0]:
            tasks.setCurrentIndex([i for i in range(tasks.count())
                                   if tasks.tabText(i).replace("&", "").startswith(("Task", "Combo"))][0])
            settle(600)
        if check("the Tasks panel's overlay has its transparent mode on", eye(tasks, True)):
            spot = tasks.mapToGlobal(QtCore.QPoint(tasks.width() // 2, tasks.height() * 2 // 3))
            image = tasks.grab().toImage().convertToFormat(QtGui.QImage.Format_ARGB32)
            at = tasks.mapFromGlobal(spot)
            v = image.pixel(at)
            note("INFO the Tasks panel at the spot is painted (r,g,b,a) %s; sheet %r"
                 % (((v >> 16) & 255, (v >> 8) & 255, v & 255, (v >> 24) & 255),
                    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/MainWindow").GetString(
                        "OverlayActiveStyleSheet", "")))
            plate = "LeftPlate" if spot.x() < mw().mapToGlobal(mw().rect().center()).x() \
                else "RightPlate"
            through("the Tasks panel", tasks, spot, plate)


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
    QtCore.QTimer.singleShot(2500, advance)


STEPS[:] = [start, overlay_all, run]
QtCore.QTimer.singleShot(3000, advance)
