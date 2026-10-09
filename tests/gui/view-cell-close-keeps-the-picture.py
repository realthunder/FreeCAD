"""A 3D view that takes a closed cell's room is still on the screen.

docs/HandsOnQueue.md entry 60: "delete a view sometime resulting the
expanding old view to be black", and "surviving resized view is black and
will only back to normal if I resize it. camera move has no effect".

"Sometimes" is: whenever closing the cell un-nests a splitter. The cell
tree is a tree of splitters; a cell split across its splitter's direction
gets a nested splitter, and closing one of the two cells in it moves the
other back up. That was done with QSplitter::replaceWidget(), which takes
the widget it replaces -- the nested splitter, the surviving cell still
inside it -- out of the window before putting the cell in. A QOpenGLWidget
that leaves its window drops the texture the window composes it from, and
an initialized one gets it back only at its next resize of its own. So the
view went on drawing, correctly, into a framebuffer that nothing put on
the screen.

That is why it has to be judged by the SCREEN: the view's own surface
(grabFramebuffer, the engine's capture) is right the whole time. The test
reads the main window's pixels off the screen.

Claims, on a document with a box, the main window 1400 x 900 and in front:
  - to start from, the 3D view is not black on the screen (the screen can
    be read);
  - for each of four ways to nest and close -- split right then the first
    cell down, or down then right; then the new cell or the old one of the
    nested pair closed: before the close three views are on the screen,
    none black; after it two, none black, at once and 1.5 s later;
  - closed down to one cell again, that one is not black either.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
import shiboken6
from PySide import QtCore, QtWidgets
from PySide6 import QtOpenGLWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "CellClose"
OPEN_VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/OpenView")
BLACK = 0.5     # the share of a view's pixels under which it is not "black"


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.4):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.005)


def surfaces():
    """The surfaces of the 3D views on screen, left to right then top to bottom"""
    res = []
    mw = FreeCADGui.getMainWindow()
    for w in mw.findChildren(QtWidgets.QMainWindow):
        if shiboken6.isValid(w) and w.metaObject().className() == "Gui::View3DInventor" \
                and w.isVisible():
            for gl in w.findChildren(QtOpenGLWidgets.QOpenGLWidget):
                if gl.isVisible():
                    res.append(gl)
    res.sort(key=lambda g: (g.mapTo(mw, QtCore.QPoint(0, 0)).x(),
                            g.mapTo(mw, QtCore.QPoint(0, 0)).y()))
    return res


def on_screen(tag):
    """Per 3D view: its place, and the share of it that is black ON THE SCREEN.
    Returns (texts, the blackest share, how many views)."""
    mw = FreeCADGui.getMainWindow()
    shot = mw.screen().grabWindow(int(mw.winId())).toImage()
    scale = shot.width() / float(max(mw.width(), 1))
    texts = []
    worst = 0.0
    views_seen = surfaces()
    for gl in views_seen:
        r = QtCore.QRect(gl.mapTo(mw, QtCore.QPoint(0, 0)), gl.size())
        dark = 0
        count = 0
        for y in range(r.top() + 20, r.bottom() - 20, 8):
            for x in range(r.left() + 20, r.right() - 20, 8):
                px, py = int(x * scale), int(y * scale)
                if px >= shot.width() or py >= shot.height():
                    continue
                p = shot.pixel(px, py)
                count += 1
                if ((p >> 16) & 255) + ((p >> 8) & 255) + (p & 255) < 24:
                    dark += 1
        s = dark / float(max(count, 1))
        worst = max(worst, s)
        texts.append("%dx%d at %d,%d black %.2f" % (r.width(), r.height(), r.left(), r.top(), s))
    if worst >= BLACK:
        shot.save(os.path.join(OUT, tag + ".png"))
    return texts, worst, len(views_seen)


def views():
    return FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")


def run():
    try:
        mw = FreeCADGui.getMainWindow()
        mw.showNormal()
        mw.resize(1400, 900)
        mw.raise_()
        mw.activateWindow()
        doc = FreeCAD.newDocument(DOC)
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        OPEN_VIEW.SetInt("MinimumCellSize", 100)
        settle(2.0)
        view = views()[0]
        view.viewIsometric()
        view.fitAll()
        settle(1.0)
        texts, worst, count = on_screen("start")
        if not check("to start from, the 3D view is not black on the screen",
                     count == 1 and worst < BLACK, texts):
            return
        command = {"right": "Std_ViewSplitRight", "down": "Std_ViewSplitDown"}
        for first, second, close in (("right", "down", "new"), ("right", "down", "old"),
                                     ("down", "right", "new"), ("down", "right", "old")):
            tag = "%s-%s-%s" % (first, second, close)
            name = "split %s, the first cell %s, the %s one closed" % (first, second, close)
            mw.setActiveWindow(views()[0])
            settle(0.2)
            FreeCADGui.runCommand(command[first])
            settle(1.0)
            # the first cell split the other way: a splitter inside the root one
            mw.setActiveWindow(views()[0])
            settle(0.2)
            FreeCADGui.runCommand(command[second])
            settle(1.2)
            texts, worst, count = on_screen(tag + "-before")
            if not check("%s: three views on the screen before, none black" % name,
                         count == 3 and worst < BLACK, texts):
                break
            mw.setActiveWindow(views()[-1] if close == "new" else views()[0])
            settle(0.2)
            FreeCADGui.runCommand("Std_ViewSplitClose")
            settle(0.4)
            texts, worst, count = on_screen(tag + "-at-once")
            check("%s: two views after, none black on the screen" % name,
                  count == 2 and worst < BLACK, texts)
            settle(1.5)
            texts, worst, count = on_screen(tag + "-later")
            check("%s: ... nor 1.5 s later" % name, count == 2 and worst < BLACK, texts)
            guard = 0
            while len(views()) > 1 and guard < 5:
                guard += 1
                mw.setActiveWindow(views()[-1])
                settle(0.2)
                FreeCADGui.runCommand("Std_ViewSplitClose")
                settle(0.6)
            texts, worst, count = on_screen(tag + "-one")
            check("%s: closed down to one cell, that one is not black" % name,
                  count == 1 and worst < BLACK, texts)
            if worst >= BLACK:
                # what the reporter did to get the picture back, so that the next
                # round starts from one
                mw.resize(1400, 901)
                settle(0.3)
                mw.resize(1400, 900)
                settle(0.6)
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        try:
            OPEN_VIEW.RemInt("MinimumCellSize")
            FreeCAD.closeDocument(DOC)
        except Exception:
            pass
        note("DONE")
        QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
