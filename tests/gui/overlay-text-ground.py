"""In a see-through overlay the Python console and the report view back their
text, line by line, with the ground the tree view gives its items -- and what
stands on such a ground is not outlined.

docs/HandsOnQueue.md entry 98: "when in overlay transparent mode, add text
background (same color for their widget background) for python console and
report view with same overlay background alpha", "like tree item background,
turn of text outline when having text background"; and as answered: behind
the text only; the same background the tree's items use; these two panels;
and where there is a background the outline is dropped, for a tree item too.

Claims, every dock overlaid and each overlay's transparent mode on, some
lines written to the console and to the report view:
  - the console's panel paints a ground at the overlay's opacity, in the
    colour of the tree's item ground, over a good part of the rows its text
    is on -- and nothing at all on a stretch below its last line;
  - the same for the report view;
  - around the text on that ground there is no outline: no pixel of the
    panel that is darker than the ground and more opaque than it, beyond a
    handful (an outline laid a dark rim under the edge of every glyph).

Runs with QT_QPA_PLATFORM=offscreen as well as on a screen; the pointer is not
moved.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
STEPS = []


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


def overlay_of(w):
    while w is not None:
        if cls(w) == "Gui::OverlayTabWidget":
            return w
        w = w.parentWidget()
    return None


def eye(tab, on):
    for button in tab.findChildren(QtWidgets.QAbstractButton):
        if button.objectName() == "OBTN Transparent":
            if button.isChecked() != on:
                button.click()
                settle(800)
            return button.isChecked() == on
    return False


def find(class_name):
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == class_name:
                return w
        except RuntimeError:
            pass
    return None


def through(tab, widget):
    """What a widget of an overlay shows, as the overlay paints it: taken
    from the overlay, whose outline is an effect on what holds the panels"""
    image = tab.grab().toImage().convertToFormat(QtGui.QImage.Format_ARGB32)
    return image.copy(QtCore.QRect(widget.mapTo(tab, QtCore.QPoint(0, 0)), widget.size()))


def ground_colour():
    """The ground the tree view gives its items: the colour most of its
    pixels at the overlay's opacity have"""
    tree = find("Gui::TreeWidget")
    tab = overlay_of(tree) if tree is not None else None
    if tab is None or not eye(tab, True):
        return None
    settle(800)
    image = through(tab, tree.viewport())
    alpha = VIEW.GetInt("DockOverlayAlpha", 180)
    seen = {}
    for y in range(0, image.height(), 2):
        for x in range(0, image.width(), 2):
            v = image.pixel(x, y)
            if abs(((v >> 24) & 255) - alpha) <= 3:
                rgb = ((v >> 16) & 255, (v >> 8) & 255, v & 255)
                seen[rgb] = seen.get(rgb, 0) + 1
    return max(seen, key=seen.get) if seen else None


def look(name, edit):
    tab = overlay_of(edit)
    if not check("%s: it is in an overlay" % name, tab is not None):
        return
    # the tab of this panel in front
    for i in range(tab.count()):
        if tab.widget(i) is not None and tab.widget(i).isAncestorOf(edit):
            tab.setCurrentIndex(i)
    if not check("%s: its overlay's transparent mode is on" % name, eye(tab, True)):
        return
    settle(1200)
    image = through(tab, edit.viewport())
    image.save(os.path.join(OUT, name.replace(" ", "-") + ".png"))
    alpha = VIEW.GetInt("DockOverlayAlpha", 180)
    want = ground_colour()
    if not check("%s: the tree view beside it has items with a ground to compare with" % name,
                 want is not None):
        return
    w, h = image.width(), image.height()
    ground = clear = rim = 0
    rows_with_ground = set()
    last = -1
    for y in range(h):
        for x in range(w):
            v = image.pixel(x, y)
            a = (v >> 24) & 255
            if a == 0:
                clear += 1
                continue
            rgb = ((v >> 16) & 255, (v >> 8) & 255, v & 255)
            if abs(a - alpha) <= 3 and max(abs(rgb[i] - want[i]) for i in range(3)) <= 6:
                ground += 1
                rows_with_ground.add(y)
                last = y
            elif a > alpha + 10 and sum(rgb) < sum(want) - 45 and a < 250:
                # darker than the ground and more opaque: an outline's rim
                rim += 1
    note("INFO %s: %dx%d, ground pixels %d on %d rows (the last at y %d), clear %d, rim %d; "
         "the ground wanted %s at %d" % (name, w, h, ground, len(rows_with_ground), last, clear,
                                         rim, want, alpha))
    if not check("%s: its text has a ground at the overlay's opacity, in the item ground's colour"
                 % name, ground > 400 and len(rows_with_ground) >= 20,
                 "%d pixels on %d rows" % (ground, len(rows_with_ground))):
        return
    below = [(image.pixel(x, y) >> 24) & 255
             for y in range(min(last + 12, h - 1), min(last + 40, h)) for x in range(0, w, 3)]
    if below:
        check("%s: below its last line nothing is painted" % name, max(below) == 0,
              "the most opaque %d" % max(below))
    check("%s: no outline around the text on its ground" % name, rim <= ground // 200 + 10,
          "%d rim pixels beside %d of ground" % (rim, ground))


def start():
    doc = FreeCAD.newDocument("TextGround")
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    settle(600)
    console = find("Gui::PythonConsole")
    report = find("Gui::DockWnd::ReportOutput")
    if console is None or overlay_of(console) is None:
        FreeCADGui.runCommand("Std_DockOverlayAll")
        settle(2500)
    for dock in mw().findChildren(QtWidgets.QDockWidget):
        if dock.objectName() in ("Python console", "Report view"):
            dock.toggleViewAction().setChecked(True)
            dock.show()
    for i in range(6):
        FreeCAD.Console.PrintMessage("A line of the report view, number %d, to read\n" % i)
        FreeCAD.Console.PrintWarning("And a warning of some length beside it, %d\n" % i)
    settle(1500)


def console_step():
    console = find("Gui::PythonConsole")
    if not check("there is a Python console", console is not None):
        return
    FreeCADGui.doCommand("Marker = 'some text for the console to show'")
    FreeCADGui.doCommand("Other = [Marker] * 2")
    settle(800)
    look("the Python console", console)


def report_step():
    report = find("Gui::DockWnd::ReportOutput")
    if not check("there is a report view", report is not None):
        return
    look("the report view", report)
    mw().grab().save(os.path.join(OUT, "window.png"))


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


STEPS[:] = [start, console_step, report_step]
QtCore.QTimer.singleShot(3000, advance)
