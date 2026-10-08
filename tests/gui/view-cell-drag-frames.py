"""A drag in a split view is shown as frames and carried out at the release.

docs/HandsOnQueue.md entry 29, docs/SplitViews.md sec 21. Dragging a
cell's corner zone or the border between cells shows translucent frames
over every cell the drag changes, at the size each will have; the layout
is not touched until the button is released. A split that would leave a
cell under the minimum cell size is refused.

Claims, on a document with a box, in a main window of 1400 x 900 (the
gestures with the minimum cell size set to 200, to have room in it):
  default - with nothing set the minimum is 300: of "split right" and "split
            down" on the one cell, the one that would leave cells under 300
            is refused and the other is carried out -- and one of them is
            a split a minimum of 200 would have allowed;
  split   - a corner zone dragged into its cell: still one cell, and two
            frames, "kept" and "fresh", that tile it with the border
            under the cursor; dragged back to where it was pressed, the
            frames go; dragged in again and released, two cells of the
            sizes the frames had;
  border  - the border dragged: no cell changes size while the button is
            down, each of the two cells has a frame at its new size, and
            after the release the cells have the sizes of their frames;
            dragged further than the cell on that side can give, the frame
            and then the cell stop at the minimum cell size;
  nested  - with two cells stacked on one side of a border, a drag of that
            border frames all three cells, and all three end at their
            frame;
  join    - a corner zone dragged into the cell below: the cell that stays
            is framed over both, the other is "going"; released, one cell
            less;
  minimum - with one cell left and the minimum cell size above half of it
            either way: the drag shows one "refused" frame, the release splits nothing, the split
            command splits nothing, the refusal is in the report view once
            for all of them, and a spreadsheet opened then goes to a tab;
  chrome  - a corner zone under the cursor is painted on an opaque ground,
            the cell's menu button too, which at rest is not; the border
            between cells is 3 pixels.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
import shiboken6
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "DragFrames"
Qt = QtCore.Qt
BORDER = 3
OPEN_VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/OpenView")


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.4):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.005)


def area():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        if shiboken6.isValid(w) and w.metaObject().className() == "Gui::ViewArea" \
                and w.isVisible():
            return w
    return None


def place(widget):
    """The widget's rectangle in the view area's coordinates"""
    return QtCore.QRect(widget.mapTo(area(), QtCore.QPoint(0, 0)), widget.size())


def cells():
    """The cells on screen, left to right then top to bottom"""
    res = []
    for button in area().findChildren(QtWidgets.QWidget, "ViewAreaMenuButton"):
        cell = button.parentWidget()
        if cell is not None and cell.isVisible():
            res.append(cell)
    res.sort(key=lambda c: (place(c).left(), place(c).top()))
    return res


def rects():
    return [place(c) for c in cells()]


def show(rs):
    return ["%d,%d %dx%d" % (r.left(), r.top(), r.width(), r.height()) for r in rs]


def frames():
    """(operation, frames, kinds) of the drag shown now"""
    for w in area().findChildren(QtWidgets.QWidget, "ViewAreaDragFrames"):
        if w.isVisible():
            return (w.property("operation"), list(w.property("frames") or []),
                    list(w.property("kinds") or []))
    return ("", [], [])


def zone(cell, top_right=True):
    zones = [w for w in cell.findChildren(QtWidgets.QWidget)
             if w.metaObject().className() == "Gui::ViewAreaZone" and w.parentWidget() is cell]
    zones.sort(key=lambda w: w.x())
    return zones[-1] if top_right else zones[0]


def handles():
    """The borders between cells on screen, the longest first"""
    res = [h for h in area().findChildren(QtWidgets.QSplitterHandle)
           if h.isVisible() and h.width() > 0 and h.height() > 0]
    res.sort(key=lambda h: -max(h.width(), h.height()))
    return res


def send(widget, kind, where, button, buttons):
    local = widget.mapFromGlobal(where)
    ev = QtGui.QMouseEvent(kind, QtCore.QPointF(local), QtCore.QPointF(where), button, buttons,
                           Qt.NoModifier)
    QtWidgets.QApplication.sendEvent(widget, ev)


class Drag:
    """A drag of a widget, by offsets from the point it was pressed at"""

    def __init__(self, widget):
        self.widget = widget
        self.start = widget.mapToGlobal(widget.rect().center())
        send(widget, QtCore.QEvent.MouseButtonPress, self.start, Qt.LeftButton, Qt.LeftButton)
        self.last = self.start

    def to(self, dx, dy):
        self.last = self.start + QtCore.QPoint(dx, dy)
        send(self.widget, QtCore.QEvent.MouseMove, self.last, Qt.NoButton, Qt.LeftButton)
        settle(0.15)
        return self

    def release(self):
        send(self.widget, QtCore.QEvent.MouseButtonRelease, self.last, Qt.LeftButton,
             Qt.NoButton)
        settle(0.6)


def near(a, b, slack=4):
    return (abs(a.left() - b.left()) <= slack and abs(a.top() - b.top()) <= slack
            and abs(a.width() - b.width()) <= slack and abs(a.height() - b.height()) <= slack)


def all_near(now, then, slack=4):
    """Every rectangle of `then` has a cell of `now` on it"""
    return all(any(near(n, t, slack) for n in now) for t in then)


def split_scenario():
    cell = cells()[0]
    was = place(cell)
    drag = Drag(zone(cell)).to(-300, 12)
    op, fr, kinds = frames()
    check("split: nothing is split while the button is down", len(cells()) == 1, len(cells()))
    ok = check("split: two frames, the cell kept and the fresh one",
               op == "split" and kinds == ["kept", "fresh"], (op, kinds, show(fr)))
    if ok:
        kept, fresh = fr
        check("split: the two frames tile the cell, a border apart",
              kept.left() == was.left() and kept.top() == was.top()
              and kept.height() == was.height() and fresh.height() == was.height()
              and fresh.left() == kept.left() + kept.width() + BORDER
              and fresh.left() + fresh.width() == was.left() + was.width(),
              (show([was]), show(fr)))
        cursor = area().mapFromGlobal(drag.last).x()
        check("split: the border is under the cursor",
              abs(kept.left() + kept.width() - cursor) <= 2, (show(fr), cursor))
    drag.to(-3, 0)
    check("split: dragged back to where it was pressed, the frames go", frames()[0] == "",
          frames())
    drag.to(-300, 12)
    op, fr, kinds = frames()
    drag.release()
    check("split: released, the frames are gone and there are two cells",
          frames()[0] == "" and len(cells()) == 2, (frames(), show(rects())))
    check("split: ... of the sizes the frames had", len(fr) == 2 and all_near(rects(), fr),
          (show(rects()), show(fr)))


def border_scenario():
    before = rects()
    drag = Drag(handles()[0]).to(80, 0)
    op, fr, kinds = frames()
    check("border: no cell changes size while the button is down", rects() == before,
          (show(before), show(rects())))
    ok = check("border: each of the two cells has a frame", op == "resize" and len(fr) == 2
               and kinds == ["kept", "kept"], (op, kinds, show(fr)))
    if ok:
        fr.sort(key=lambda r: r.left())
        check("border: ... at the size the move gives it",
              fr[0].width() == before[0].width() + 80
              and fr[1].width() == before[1].width() - 80
              and fr[1].left() == before[1].left() + 80, (show(before), show(fr)))
    drag.release()
    check("border: released, the cells have the sizes of their frames",
          frames()[0] == "" and len(fr) == 2 and all_near(rects(), fr),
          (show(rects()), show(fr)))
    # ... and further than the cell on that side can give
    least = OPEN_VIEW.GetInt("MinimumCellSize", 200)
    drag = Drag(handles()[0]).to(700, 0)
    op, fr, kinds = frames()
    fr.sort(key=lambda r: r.left())
    check("border: dragged past it, the frame stops at the minimum cell size",
          len(fr) == 2 and fr[1].width() == least, (least, show(fr)))
    drag.release()
    check("border: ... and so does the cell", len(fr) == 2 and all_near(rects(), fr),
          (show(rects()), show(fr)))


def nested_scenario():
    # the right cell divided top and bottom, by the gesture
    right = cells()[-1]
    Drag(zone(right)).to(-8, 250).release()
    if not check("nested: the right side is two cells, one over the other", len(cells()) == 3
                 and rects()[1].left() == rects()[2].left(), show(rects())):
        return False
    before = rects()
    tall = [h for h in handles() if h.height() > h.width()]
    drag = Drag(tall[0]).to(-100, 0)
    op, fr, kinds = frames()
    ok = check("nested: a drag of the border frames all three cells",
               op == "resize" and len(fr) == 3, (op, kinds, show(fr)))
    if ok:
        fr.sort(key=lambda r: (r.left(), r.top()))
        want = [before[0].adjusted(0, 0, -100, 0)] + [b.adjusted(-100, 0, 0, 0)
                                                      for b in before[1:]]
        check("nested: ... the left one narrower, the two on the right wider and moved",
              all(near(f, w, 1) for f, w in zip(fr, want)), (show(want), show(fr)))
    drag.release()
    check("nested: released, all three cells are at their frames",
          len(fr) == 3 and all_near(rects(), fr), (show(rects()), show(fr)))
    return True


def join_scenario():
    top, bottom = cells()[1], cells()[2]
    stays, goes = place(top), place(bottom)
    # out of the top cell, into the one below
    drag = Drag(zone(top, top_right=False))
    below = bottom.mapToGlobal(bottom.rect().center()) - drag.start
    drag.to(below.x(), below.y())
    op, fr, kinds = frames()
    ok = check("join: two frames, the cell that stays and the one going",
               op == "join" and kinds == ["kept", "going"], (op, kinds, show(fr)))
    if ok:
        check("join: the one that stays is framed over both, the other where it is",
              fr[0] == stays.united(goes) and fr[1] == goes, (show(fr), show([stays, goes])))
    check("join: nothing is joined while the button is down", len(cells()) == 3, len(cells()))
    drag.release()
    check("join: released, one cell less and no frames",
          len(cells()) == 2 and frames()[0] == "", (show(rects()), frames()))


def refusals():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QTextEdit):
        if w.metaObject().className() == "Gui::DockWnd::ReportOutput":
            return w.toPlainText().count("is not split")
    return -1


def activate_3d():
    """Make a 3D view of the document the active one: the split commands act on it"""
    view3d = FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")[0]
    FreeCADGui.getMainWindow().setActiveWindow(view3d)
    settle(0.1)


def default_scenario():
    """With nothing set the minimum is 300: told by what it refuses"""
    check("default: the minimum cell size is not set", OPEN_VIEW.GetInt("MinimumCellSize", -1) == -1,
          OPEN_VIEW.GetInt("MinimumCellSize", -1))
    cell = rects()[0]
    halves = {"Std_ViewSplitRight": (cell.width() - BORDER) // 2,
              "Std_ViewSplitDown": (cell.height() - BORDER) // 2}
    check("default: one of the two splits would give cells between 200 and 300",
          any(200 <= h < 300 for h in halves.values()), halves)
    for command, half in halves.items():
        activate_3d()
        said = refusals()
        FreeCADGui.runCommand(command)
        settle()
        if half >= 300:
            check("default: %s, cells of %d, is carried out" % (command, half),
                  len(cells()) == 2, show(rects()))
        else:
            check("default: %s, cells of %d, is refused, and says so" % (command, half),
                  len(cells()) == 1 and refusals() == said + 1,
                  (show(rects()), said, refusals()))
        while len(cells()) > 1:
            activate_3d()
            FreeCADGui.runCommand("Std_ViewSplitClose")
            settle()


def minimum_scenario():
    # down to one cell, and a minimum of more than half of it either way: every
    # split is refused, and the cell is not pushed wider by its own minimum
    view3d = FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")[0]
    FreeCADGui.getMainWindow().setActiveWindow(view3d)
    while len(cells()) > 1:
        FreeCADGui.runCommand("Std_ViewSplitClose")
        settle()
    if not check("minimum: one cell to start from", len(cells()) == 1, show(rects())):
        return
    least = max(rects()[0].width(), rects()[0].height()) // 2 + 40
    OPEN_VIEW.SetInt("MinimumCellSize", least)
    settle()
    try:
        before = rects()
        said = refusals()
        cell = cells()[0]
        drag = Drag(zone(cell)).to(-150, 10)
        op, fr, kinds = frames()
        check("minimum: the drag shows one frame, refused, over the cell",
              op == "split" and kinds == ["refused"] and fr == [place(cell)],
              (op, kinds, show(fr)))
        drag.release()
        check("minimum: the release splits nothing", len(rects()) == len(before), show(rects()))
        view3d = FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")[0]
        FreeCADGui.getMainWindow().setActiveWindow(view3d)
        FreeCADGui.runCommand("Std_ViewSplitRight")
        settle()
        FreeCADGui.runCommand("Std_ViewSplitDown")
        settle()
        check("minimum: the split commands split nothing", len(rects()) == len(before),
              show(rects()))
        check("minimum: the refusal is in the report view, once for the three",
              refusals() == said + 1, (said, refusals()))
        FreeCADGui.getDocument(DOC).getObject("Numbers").doubleClicked()
        settle(1.0)
        sheets = [w for w in QtWidgets.QApplication.allWidgets()
                  if shiboken6.isValid(w) and w.isVisible()
                  and w.metaObject().className() == "SpreadsheetGui::SheetView"]
        in_tab = [isinstance(w.parentWidget(), QtWidgets.QMdiSubWindow) for w in sheets]
        check("minimum: a spreadsheet opened then goes to a tab, the cells as they were",
              in_tab == [True] and (area() is None or len(rects()) == len(before)),
              (in_tab, show(rects()) if area() else "another tab is in front"))
    finally:
        OPEN_VIEW.RemInt("MinimumCellSize")


def ground(widget, point):
    """The alpha of one pixel of the widget painted by itself, on nothing"""
    pix = QtGui.QPixmap(widget.size())
    pix.fill(Qt.transparent)
    widget.render(pix, QtCore.QPoint(), QtGui.QRegion(), QtWidgets.QWidget.RenderFlags())
    return pix.toImage().pixelColor(point).alpha()


def hover(widget, on):
    if on:
        centre = QtCore.QPointF(widget.rect().center())
        ev = QtGui.QEnterEvent(centre, centre, QtCore.QPointF(widget.mapToGlobal(centre.toPoint())))
    else:
        ev = QtCore.QEvent(QtCore.QEvent.Leave)
    QtWidgets.QApplication.sendEvent(widget, ev)
    settle(0.1)


def chrome_scenario():
    cell = cells()[0]
    z = zone(cell)
    hover(z, True)
    # a pixel the grip's strokes do not pass through
    check("chrome: a corner zone under the cursor is painted on an opaque ground",
          ground(z, QtCore.QPoint(3, 10)) == 255, ground(z, QtCore.QPoint(3, 10)))
    hover(z, False)
    button = cell.findChild(QtWidgets.QWidget, "ViewAreaMenuButton")
    check("chrome: the menu button at rest is not", ground(button, QtCore.QPoint(8, 2)) == 0,
          ground(button, QtCore.QPoint(8, 2)))
    hover(button, True)
    check("chrome: ... and under the cursor it is", ground(button, QtCore.QPoint(8, 2)) == 255,
          ground(button, QtCore.QPoint(8, 2)))
    hover(button, False)
    widths = sorted(set(sp.handleWidth() for sp in area().findChildren(QtWidgets.QSplitter)))
    check("chrome: the border between cells is 3 pixels", widths == [BORDER], widths)


def run():
    try:
        import SpreadsheetGui  # noqa: F401  the view provider
        mw = FreeCADGui.getMainWindow()
        mw.showNormal()
        mw.resize(1400, 900)
        doc = FreeCAD.newDocument(DOC)
        doc.addObject("Part::Box", "Box")
        doc.addObject("Spreadsheet::Sheet", "Numbers")
        doc.recompute()
        settle(1.5)
        if not check("one cell, at least 900 x 500, to start from",
                     area() is not None and len(cells()) == 1
                     and rects()[0].width() >= 900 and rects()[0].height() >= 500,
                     show(rects()) if area() else None):
            return
        default_scenario()
        # the gestures below on a minimum of 200: the window a test can count on
        # has no room for two cells of 300 one over the other
        OPEN_VIEW.SetInt("MinimumCellSize", 200)
        settle()
        split_scenario()
        if len(cells()) == 2:
            border_scenario()
            if nested_scenario():
                join_scenario()
        chrome_scenario()
        minimum_scenario()
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        try:
            OPEN_VIEW.RemInt("MinimumCellSize")
            FreeCAD.closeDocument(DOC)
        except Exception:
            pass
        note("DONE")
        QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
