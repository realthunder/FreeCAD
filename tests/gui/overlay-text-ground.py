"""In a see-through overlay the Python console and the report view back their
text, line by line, with the ground the tree view gives its items -- the 3D
view's background colour where the overlay lies over it -- and the text is
edged on that ground.

docs/HandsOnQueue.md entry 98: "when in overlay transparent mode, add text
background (same color for their widget background) for python console and
report view with same overlay background alpha", "like tree item background,
turn of text outline when having text background"; and as answered: behind
the text only; the same background the tree's items use; these two panels;
and where there is a background the outline is dropped, for a tree item too.

And as asked 2026-10-14: "when 3d view background is radial, then auto item
background (including python console and report view) color should select
the 'End'. if gradiant is Linear, then when overlay dock in top or left or
right, use 'Start' color. when docked bottom, use 'Stop' color. If no
gradiant, just use the single color"; and "is it possible to draw text
outline even with the background?" -- "the outline yes, gate it to the
existing outline system".

Claims, every dock overlaid and each overlay's transparent mode on, some
lines written to the console and to the report view:
  - the console's panel paints a ground at the overlay's opacity over a good
    part of the rows its text is on -- and nothing at all on a stretch below
    its last line. Its colour is the 3D view's background where the overlay
    is: the pattern linear, the first colour for an overlay at the top, the
    left or the right and the last for one at the bottom; radial, the last
    colour; one colour, that colour;
  - the same for the report view, and the tree's items follow the same rule;
  - with "Item background follows the theme" off (View/
    DockOverlayThemeItemColor) each of the two stands on its OWN background
    instead, at the same opacity: the console's colour where it has one
    (Editor/Background), else the base colour of the application's palette
    (docs/HandsOnQueue.md entry 111, as ruled: "gate by auto tree background
    color setting. Follow tree item background color if enabled, else use
    its own widget background" -- the Classic theme keeps a black text in
    the console and gives the tree's items a dark ground);
  - the text is edged on its ground: beside the letters the panel has
    pixels more opaque than the ground -- the outline laid over the ground
    and under the letters (it was dropped there once, when it was laid under
    the whole ground).

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


def rgb_of(packed):
    return ((packed >> 24) & 255, (packed >> 16) & 255, (packed >> 8) & 255)


def ground_for(tab):
    """The colour of the 3D view's background where an overlay lies over it,
    by the pattern and by the overlay's side (its name says the side)"""
    one = rgb_of(VIEW.GetUnsigned("BackgroundColor", 0xEAE5DCFF))
    first = rgb_of(VIEW.GetUnsigned("BackgroundColor2", 0x333365FF))
    last = rgb_of(VIEW.GetUnsigned("BackgroundColor3", 0xABABC1FF))
    if VIEW.GetBool("Gradient", True):
        return last if tab.objectName() == "OverlayBottom" else first
    if VIEW.GetBool("RadialGradient", False):
        return last
    return one


def commonest_ground(tab, widget):
    """The colour most pixels of a widget at the overlay's opacity have"""
    image = through(tab, widget)
    alpha = VIEW.GetInt("DockOverlayAlpha", 180)
    seen = {}
    for y in range(0, image.height(), 2):
        for x in range(0, image.width(), 2):
            v = image.pixel(x, y)
            if abs(((v >> 24) & 255) - alpha) <= 3:
                rgb = ((v >> 16) & 255, (v >> 8) & 255, v & 255)
                seen[rgb] = seen.get(rgb, 0) + 1
    return max(seen, key=seen.get) if seen else None


def look(name, edit, own=None):
    """The ground behind the text of a panel: the tree items' -- or own,
    a colour, with the setting off"""
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
    if own is None:
        want = ground_for(tab)
        which = "in the 3D background's colour for an overlay at %s" % tab.objectName()[7:].lower()
    else:
        want = own
        which = "in its own background's colour"
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
            elif a > alpha + 10 and a < 250:
                # more opaque than the ground and not ink: the outline on it
                rim += 1
    note("INFO %s: %dx%d, ground pixels %d on %d rows (the last at y %d), clear %d, rim %d; "
         "the ground wanted %s at %d" % (name, w, h, ground, len(rows_with_ground), last, clear,
                                         rim, want, alpha))
    if not check("%s: its text has a ground at the overlay's opacity, %s" % (name, which),
                 ground > 400 and len(rows_with_ground) >= 20,
                 "%d pixels on %d rows" % (ground, len(rows_with_ground))):
        return
    below = [(image.pixel(x, y) >> 24) & 255
             for y in range(min(last + 12, h - 1), min(last + 40, h)) for x in range(0, w, 3)]
    if below:
        check("%s: below its last line nothing is painted" % name, max(below) == 0,
              "the most opaque %d" % max(below))
    check("%s: the text is edged on its ground" % name, rim >= 40,
          "%d pixels of an outline beside %d of ground" % (rim, ground))


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


def pattern_step():
    """The ground follows the pattern of the 3D view's background, for the
    text panels and for the tree's items alike"""
    was = (VIEW.GetBool("Gradient", True), VIEW.GetBool("RadialGradient", False))
    tree = find("Gui::TreeWidget")
    console = find("Gui::PythonConsole")
    panels = [("the tree", tree.viewport() if tree is not None else None),
              ("the Python console", console.viewport() if console is not None else None)]
    try:
        for label, linear, radial in (("linear", True, False), ("radial", False, True),
                                      ("one colour", False, False)):
            VIEW.SetBool("Gradient", linear)
            VIEW.SetBool("RadialGradient", radial)
            settle(1200)
            for name, widget in panels:
                tab = overlay_of(widget) if widget is not None else None
                if tab is None or not eye(tab, True):
                    continue
                settle(400)
                got = commonest_ground(tab, widget)
                want = ground_for(tab)
                check("%s, the background %s: its ground is the colour for an overlay at %s"
                      % (name, label, tab.objectName()[7:].lower()),
                      got is not None and max(abs(got[i] - want[i]) for i in range(3)) <= 6,
                      "%s for %s" % (got, want))
    finally:
        VIEW.SetBool("Gradient", was[0])
        VIEW.SetBool("RadialGradient", was[1])
        settle(800)


def own_step():
    """The setting off: each panel on its own background"""
    VIEW.SetBool("DockOverlayThemeItemColor", False)
    settle(1500)
    base = QtWidgets.QApplication.palette().color(QtGui.QPalette.Base)
    base = (base.red(), base.green(), base.blue())
    packed = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Editor").GetUnsigned(
        "Background", 0)
    mine = ((packed >> 24) & 255, (packed >> 16) & 255, (packed >> 8) & 255) if packed else base
    note("INFO the setting off: the palette's base %s, the console's own %s" % (base, mine))
    console = find("Gui::PythonConsole")
    report = find("Gui::DockWnd::ReportOutput")
    try:
        if console is not None:
            look("the Python console, the setting off", console, mine)
        if report is not None:
            look("the report view, the setting off", report, base)
    finally:
        VIEW.RemBool("DockOverlayThemeItemColor")
        settle(600)


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


STEPS[:] = [start, console_step, report_step, pattern_step, own_step]
QtCore.QTimer.singleShot(3000, advance)
