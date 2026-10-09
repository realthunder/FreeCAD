"""A view cell drag can be given up, and its frames and handles can be seen.

docs/HandsOnQueue.md entry 56, after entry 29 (view-cell-drag-frames.py):
"for view cell menu and handle, when shown on hover the color does not look
good with light gray and white background. no contrast. Further more, if the
view has scroll bar, move the handles off the scroll bar to show it more
clearly. Also the frame color when draging does not look good. I think it is
because the trasnparency is too high. Add some white borders for the frame.
does the color follow current theme accent color? if no theme then use the
pallete color for selection highlight. Do the same for overlay drag frame.
also, view cell drag frame should respond to esc and any mouse click to mean
cancel. only left release means commit." Then: "same settings for the
overlay drag frame, except the color of the face", and of a join: "removed
cell shouldn't have any frame right? the frame is the new one and occupies
the old cell area. draw a big red stop sign in the center of removing cell
instead of pointed triangle".

Claims, in a main window of 1400 x 900 with two 3D cells side by side and a
spreadsheet in a third, the minimum cell size at 120:
  cancel  - a split drag, a border drag and a join drag each: Escape takes
            the frames away, and the release of the left button after it
            changes nothing; the same with the right button pressed while
            the left is held, with the middle one, and when another
            program comes to the front ("when I am dragging and mouse grag
            got interrupted by another application popping, the cell frame
            is still visible when mouse button released"); a right click
            that gives a border drag up does not bring the border's menu;
  look    - a frame's face is the accent colour at the overlay drag
            frame's opacity, 0.3, inside a white border with a dark line
            round it; the accent is the palette's selection highlight
            while no theme is applied, and Themes/ThemeAccentColor1 under a
            theme, followed when it changes;
  join    - the cell that goes has no cover of its own -- away from its
            middle it shows the face of the frame of the cell that stays
            -- and a red sign in its middle;
  chrome  - a corner zone and the menu button under the cursor are painted
            on the accent colour, their strokes white;
  close   - "split drag that would make a cell too small shall be
            interpreted as closing that view": the border dragged well past
            a cell's minimum shows that cell as going, under the red sign,
            and the release closes it;
  refusal - "corner drag semantics stay, i.e. drag in itself only create
            and never close": a corner drag that would leave a cell under
            the minimum is refused, closes nothing, and says why once, as
            an error ("should print a one time error message saying the
            reason");
  scroll  - in the spreadsheet's cell no zone lies on a scroll bar: the
            one whose corner a bar runs through has stepped aside to the
            bar's edge, the other is in its corner; in a 3D cell both are
            in their corners;
  other   - "split view on some view type did nothing, like techdraw page
            [...] create a 3d view instead": the spreadsheet's cell split
            by the gesture gives one cell more, showing a 3D view of the
            document.
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
DOC = "DragCancel"
Qt = QtCore.Qt
OPEN_VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/OpenView")
THEMES = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Themes")
MAIN = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/MainWindow")
ZONE = 14
LEAST = 120     # the minimum cell size the gestures are run with


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


def place(widget, of=None):
    """The widget's rectangle in the view area's coordinates, or in `of`'s"""
    return QtCore.QRect(widget.mapTo(of or area(), QtCore.QPoint(0, 0)), widget.size())


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


def frames_widget():
    for w in area().findChildren(QtWidgets.QWidget, "ViewAreaDragFrames"):
        if w.isVisible():
            return w
    return None


def frames():
    """(operation, frames, kinds) of the drag shown now"""
    w = frames_widget()
    if w is None:
        return ("", [], [])
    return (w.property("operation"), list(w.property("frames") or []),
            list(w.property("kinds") or []))


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

    def other(self, button):
        """Another button pressed and let go while the left one is held"""
        send(self.widget, QtCore.QEvent.MouseButtonPress, self.last, button,
             Qt.LeftButton | button)
        send(self.widget, QtCore.QEvent.MouseButtonRelease, self.last, button, Qt.LeftButton)
        if button == Qt.RightButton:
            # what the window system sends with a right click
            local = self.widget.mapFromGlobal(self.last)
            ev = QtGui.QContextMenuEvent(QtGui.QContextMenuEvent.Mouse, local, self.last)
            QtWidgets.QApplication.sendEvent(self.widget, ev)
        settle(0.15)

    def release(self):
        send(self.widget, QtCore.QEvent.MouseButtonRelease, self.last, Qt.LeftButton,
             Qt.NoButton)
        settle(0.6)


def escape():
    mw = FreeCADGui.getMainWindow()
    for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        QtWidgets.QApplication.sendEvent(
            mw, QtGui.QKeyEvent(kind, Qt.Key_Escape, Qt.NoModifier))
    settle(0.15)


def away():
    # what Qt tells the application when another program takes the front, the
    # mouse and its release with it
    app = QtWidgets.QApplication.instance()
    QtWidgets.QApplication.sendEvent(app, QtCore.QEvent(QtCore.QEvent.ApplicationDeactivate))
    settle(0.15)


MENUS = [0]


def close_menus():
    # a menu that should not have come up would hold the test in its own loop
    for w in QtWidgets.QApplication.topLevelWidgets():
        if isinstance(w, QtWidgets.QMenu) and w.isVisible():
            MENUS[0] += 1
            w.close()


SWEEPER = QtCore.QTimer()
SWEEPER.timeout.connect(close_menus)
SWEEPER.start(300)


def cancel_scenario():
    left, right = cells()[0], cells()[1]
    gestures = (
        ("split", lambda: Drag(zone(left)).to(-200, 20)),
        ("resize", lambda: Drag(handles()[0]).to(-90, 0)),
        ("join", lambda: Drag(zone(left)).to(
            right.mapToGlobal(right.rect().center()).x()
            - zone(left).mapToGlobal(zone(left).rect().center()).x(), 60)),
    )
    ways = (("Escape", lambda drag: escape()),
            ("the right button", lambda drag: drag.other(Qt.RightButton)),
            ("the middle button", lambda drag: drag.other(Qt.MiddleButton)),
            ("another program coming to the front", lambda drag: away()))
    for operation, start in gestures:
        for way, give_up in ways:
            before = rects()
            menus = MENUS[0]
            drag = start()
            shown = frames()[0]
            if not check("cancel: a %s drag shows its frames" % operation, shown == operation,
                         frames()):
                drag.release()
                settle(0.3)
                return False
            give_up(drag)
            check("cancel: %s during a %s drag takes the frames away" % (way, operation),
                  frames()[0] == "", frames()[0])
            drag.release()
            settle(0.4)
            check("cancel: ... and the release of the left button then changes nothing",
                  rects() == before, (show(before), show(rects())))
            if way == "the right button":
                check("cancel: ... and no menu comes up for that right click",
                      MENUS[0] == menus, MENUS[0] - menus)
            if rects() != before:
                return False
    return True


def render(widget):
    pix = QtGui.QPixmap(widget.size())
    pix.fill(Qt.transparent)
    # the frames widget is masked to its frames: without this flag it is drawn
    # from the mask's corner, not from its own
    widget.render(pix, QtCore.QPoint(), QtGui.QRegion(), QtWidgets.QWidget.IgnoreMask)
    return pix.toImage().convertToFormat(QtGui.QImage.Format_ARGB32)


def rgba(image, x, y):
    c = image.pixelColor(int(x), int(y))
    return (c.red(), c.green(), c.blue(), c.alpha())


def close_to(a, b, slack):
    return all(abs(a[i] - b[i]) <= slack for i in range(len(b)))


def themed():
    # a theme is a style sheet chosen for the main window; the application has
    # a sheet of its own in every session
    return bool(MAIN.GetString("StyleSheet", ""))


def accent():
    """The accent the frames are to have, by the rule asked for"""
    if themed():
        packed = THEMES.GetUnsigned("ThemeAccentColor1", 0x557BB6FF)
        return ((packed >> 24) & 255, (packed >> 16) & 255, (packed >> 8) & 255), "the theme's"
    c = area().palette().color(QtGui.QPalette.Highlight)
    return (c.red(), c.green(), c.blue()), "the palette's highlight"


def face_of_kept(tag):
    """The face of the kept frame of a split drag, read off the frames
    themselves: (rgba in its middle, rgba 2 px in from its edge, rgba on its edge)"""
    cell = max(cells(), key=lambda c: c.width() * c.height())
    if cell.width() >= 2 * LEAST + 20:
        drag = Drag(zone(cell)).to(-cell.width() // 2, 12)
    else:
        drag = Drag(zone(cell)).to(-10, cell.height() // 2)
    op, fr, kinds = frames()
    res = None
    if op == "split" and "kept" in kinds:
        r = fr[kinds.index("kept")]
        image = render(frames_widget())
        image.save(os.path.join(OUT, tag + ".png"))
        y = r.top() + r.height() * 3 // 4
        res = (rgba(image, r.center().x(), y), rgba(image, r.left() + 2, y),
               rgba(image, r.left(), y))
    else:
        NOTES.append("the drag of a cell of %d x %d showed %s %s" % (
            cell.width(), cell.height(), op, kinds))
    escape()
    drag.release()
    return res


NOTES = []


def look_scenario(tag):
    want, whose = accent()
    got = face_of_kept("frames-" + tag)
    if not check("look (%s): a split drag shows a kept frame" % tag, got is not None,
                 "; ".join(NOTES[-1:])):
        return
    face, border, edge = got
    check("look (%s): the frame's face is the accent colour, %s" % (tag, whose),
          close_to(face, want, 8), "painted %s, the accent %s" % (face[:3], want))
    check("look (%s): ... at the overlay drag frame's opacity, 0.3" % tag,
          abs(face[3] - 77) <= 3, "alpha %d of 255" % face[3])
    check("look (%s): the frame has a white border" % tag,
          close_to(border, (255, 255, 255, 255), 6), border)
    check("look (%s): ... inside a dark line" % tag,
          max(edge[:3]) < 70 and edge[3] > 150, edge)


def join_scenario():
    left, right = cells()[0], cells()[1]
    stays, goes = place(left), place(right)
    z = zone(left)
    drag = Drag(z).to(right.mapToGlobal(right.rect().center()).x()
                      - z.mapToGlobal(z.rect().center()).x(), 60)
    op, fr, kinds = frames()
    if check("join: the cell that stays is framed over both, the other is listed as going",
             op == "join" and kinds == ["kept", "going"] and fr[0] == stays.united(goes)
             and fr[1] == goes, (op, kinds, show(fr))):
        image = render(frames_widget())
        image.save(os.path.join(OUT, "join.png"))
        want, _whose = accent()
        middle = rgba(image, goes.center().x(), goes.center().y())
        check("join: a red sign in the middle of the cell that goes",
              middle[0] > 170 and middle[1] < 70 and middle[2] < 70 and middle[3] == 255, middle)
        corner = rgba(image, goes.left() + goes.width() // 8, goes.top() + goes.height() // 8)
        check("join: away from the sign that cell shows the face of the frame over it, "
              "no cover of its own", close_to(corner[:3], want, 8) and abs(corner[3] - 77) <= 3,
              "painted %s, the accent %s" % (corner, want))
        # the sign is an octagon: as wide at a third of its height as in the middle
        radius = 0
        while radius < 80 and rgba(image, goes.center().x() + radius + 1,
                                   goes.center().y())[0] > 170 \
                and rgba(image, goes.center().x() + radius + 1, goes.center().y())[1] < 70:
            radius += 1
        check("join: the sign is big, between 12 and 48 pixels from its middle to its side",
              12 <= radius <= 48, radius)
    escape()
    drag.release()


def hover(widget, on):
    if on:
        centre = QtCore.QPointF(widget.rect().center())
        ev = QtGui.QEnterEvent(centre, centre, QtCore.QPointF(widget.mapToGlobal(centre.toPoint())))
    else:
        ev = QtCore.QEvent(QtCore.QEvent.Leave)
    QtWidgets.QApplication.sendEvent(widget, ev)
    settle(0.1)


def chrome_scenario():
    want, _whose = accent()
    cell = cells()[0]
    for name, widget, point in (("a corner zone", zone(cell), (3, 10)),
                                ("the menu button", cell.findChild(QtWidgets.QWidget,
                                                                   "ViewAreaMenuButton"), (8, 2))):
        hover(widget, True)
        image = render(widget)
        hover(widget, False)
        ground = rgba(image, *point)
        check("chrome: %s under the cursor is painted on the accent colour" % name,
              close_to(ground, want + (255,), 8), "painted %s, the accent %s" % (ground, want))
        white = sum(1 for y in range(image.height()) for x in range(2, image.width() - 2)
                    if 2 <= y < image.height() - 2
                    and close_to(rgba(image, x, y), (255, 255, 255, 255), 40))
        check("chrome: ... its strokes white", white >= 6, "%d white pixels inside it" % white)


def scroll_scenario():
    sheet_cell = None
    plain_cell = None
    for cell in cells():
        names = [w.metaObject().className() for w in cell.findChildren(QtWidgets.QWidget)]
        if "SpreadsheetGui::SheetView" in names:
            sheet_cell = cell
        elif plain_cell is None:
            plain_cell = cell
    if not check("scroll: a spreadsheet in a cell, and a 3D cell", sheet_cell is not None
                 and plain_cell is not None, show(rects())):
        return
    # as the cursor coming into the cell: the zones are placed then at the latest
    hover(sheet_cell, True)
    settle(0.3)
    bars = [place(b, sheet_cell) for b in sheet_cell.findChildren(QtWidgets.QScrollBar)
            if b.isVisible()]
    w, h = sheet_cell.width(), sheet_cell.height()
    corners = {"top right": QtCore.QRect(w - ZONE, 0, ZONE, ZONE),
               "bottom left": QtCore.QRect(0, h - ZONE, ZONE, ZONE)}
    in_the_way = dict((name, [b for b in bars if b.intersects(corner)])
                      for name, corner in corners.items())
    if not check("scroll: a scroll bar of the sheet runs through a corner a zone belongs in",
                 any(in_the_way.values()), "the cell %d x %d, its bars %s" % (
                     w, h, show(bars))):
        return
    zones = {"top right": zone(sheet_cell).geometry(),
             "bottom left": zone(sheet_cell, top_right=False).geometry()}
    for name in ("top right", "bottom left"):
        z = zones[name]
        check("scroll: the %s zone lies on no scroll bar" % name,
              not any(b.intersects(z) for b in bars), (show([z]), show(bars)))
        if not in_the_way[name]:
            check("scroll: ... and is in its corner, no bar being there", z == corners[name],
                  show([z]))
        elif name == "top right":
            check("scroll: ... and ends where the bar begins, at the top",
                  z.top() == 0 and z.right() + 1 == min(b.left() for b in in_the_way[name]),
                  (show([z]), show(in_the_way[name])))
        else:
            check("scroll: ... and sits on top of the bar, at the left",
                  z.left() == 0 and z.bottom() + 1 == min(b.top() for b in in_the_way[name]),
                  (show([z]), show(in_the_way[name])))
    top = zone(plain_cell).geometry()
    bottom = zone(plain_cell, top_right=False).geometry()
    check("scroll: in a 3D cell both zones are in their corners",
          top == QtCore.QRect(plain_cell.width() - ZONE, 0, ZONE, ZONE)
          and bottom == QtCore.QRect(0, plain_cell.height() - ZONE, ZONE, ZONE),
          (show([top, bottom]), plain_cell.width(), plain_cell.height()))


def close_scenario():
    """Point (i): the border pushed well past a cell's minimum closes it"""
    left, right = cells()[0], cells()[1]
    before = rects()
    drag = Drag(handles()[0]).to(before[1].width() - LEAST + 300, 0)
    op, fr, kinds = frames()
    if check("close: the border dragged well past the right cell's minimum shows it as going, "
             "the left one framed over both",
             op == "close" and kinds == ["kept", "going"] and fr[0] == before[0].united(before[1])
             and fr[1] == before[1], (op, kinds, show(fr))):
        image = render(frames_widget())
        image.save(os.path.join(OUT, "close.png"))
        middle = rgba(image, before[1].center().x(), before[1].center().y())
        check("close: a red sign in the middle of the cell that goes",
              middle[0] > 170 and middle[1] < 70 and middle[2] < 70, middle)
    check("close: nothing is closed while the button is down", len(cells()) == 2, len(cells()))
    drag.release()
    settle(0.6)
    views = FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")
    check("close: released, one cell and one 3D view are left, no frames",
          len(cells()) == 1 and len(views) == 1 and frames()[0] == "",
          (show(rects()), len(views), frames()[0]))


def other_view_scenario():
    """Point (j): a cell whose view there is only one of is split all the same"""
    sheet_cell = None
    for cell in cells():
        names = [w.metaObject().className() for w in cell.findChildren(QtWidgets.QWidget)]
        if "SpreadsheetGui::SheetView" in names:
            sheet_cell = cell
    if not check("other: a spreadsheet in a cell", sheet_cell is not None, show(rects())):
        return
    count = len(cells())
    views = len(FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor"))
    was = place(sheet_cell)
    if sheet_cell.width() >= 2 * LEAST + 20:
        Drag(zone(sheet_cell)).to(-sheet_cell.width() // 2, 12).release()
    else:
        Drag(zone(sheet_cell)).to(-10, sheet_cell.height() // 2).release()
    settle(1.0)
    now = len(FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor"))
    sheets = [w for w in area().findChildren(QtWidgets.QWidget)
              if w.metaObject().className() == "SpreadsheetGui::SheetView" and w.isVisible()]
    check("other: the cell of a spreadsheet, %d x %d, is split by the gesture" % (
        was.width(), was.height()), len(cells()) == count + 1, show(rects()))
    check("other: ... the new cell showing a 3D view of the document, the sheet where it was",
          now == views + 1 and len(sheets) == 1, "3D views %d then %d; sheet views %d" % (
              views, now, len(sheets)))


SAID = []


def listen(notifier, message, level):
    if "is not split" in message:
        SAID.append(str(level))


def refusal_scenario():
    """Point (h): a split refused for the minimum cell size says so as an error"""
    cell = max(cells(), key=lambda c: c.width() * c.height())
    count = len(cells())
    OPEN_VIEW.SetInt("MinimumCellSize", 4000)
    settle()
    FreeCAD.Console.AttachObserver(listen)
    try:
        drag = Drag(zone(cell)).to(-150, 10)
        op, fr, kinds = frames()
        check("refusal: a corner drag that would leave a cell under the minimum shows it refused",
              op == "split" and kinds == ["refused"], (op, kinds))
        drag.release()
        settle(0.6)
        check("refusal: the release splits nothing and closes nothing", len(cells()) == count,
              show(rects()))
        check("refusal: ... and says why once, as an error", SAID == ["Error"], SAID)
    finally:
        FreeCAD.Console.DetachObserver(listen)
        OPEN_VIEW.SetInt("MinimumCellSize", LEAST)
        settle()


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
        OPEN_VIEW.SetInt("MinimumCellSize", LEAST)
        settle(1.5)
        view3d = FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")[0]
        mw.setActiveWindow(view3d)
        FreeCADGui.runCommand("Std_ViewSplitRight")
        settle()
        if not check("two cells side by side to start from", area() is not None
                     and len(cells()) == 2, show(rects()) if area() else None):
            return
        check("no theme is applied to start from",
              not themed(), MAIN.GetString("StyleSheet", ""))
        cancel_scenario()
        look_scenario("no theme")
        join_scenario()
        chrome_scenario()
        close_scenario()
        refusal_scenario()
        view3d = FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")[0]
        mw.setActiveWindow(view3d)
        FreeCADGui.runCommand("Std_ViewSplitRight")
        settle()
        FreeCADGui.getDocument(DOC).getObject("Numbers").doubleClicked()
        settle(1.0)
        scroll_scenario()
        other_view_scenario()
        # under a theme the accent is the theme's, and is followed
        if FreeCADGui.applyTheme("Light"):
            settle(3.0)
            if check("a theme is applied", themed(), MAIN.GetString("StyleSheet", "")):
                look_scenario("Light")
                THEMES.SetUnsigned("ThemeAccentColor1", 0xC8321EFF)
                settle(3.0)
                look_scenario("Light, another accent")
        else:
            check("the Light theme can be applied", False)
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        try:
            SWEEPER.stop()
            OPEN_VIEW.RemInt("MinimumCellSize")
            FreeCAD.closeDocument(DOC)
        except Exception:
            pass
        note("DONE")
        QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
