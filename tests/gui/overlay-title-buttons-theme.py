"""The buttons on a dock's title bar are seen under a dark theme and under a
light one, and the "show this task panel in its view" button is there only
while it has a panel to send.

docs/HandsOnQueue.md entry 73: "the close button and toggle panel in view
button should be dark/light theme aware. also, when there is no view owned
task panel, the toggle button show be hidden", then "also check overlay title
buttons, make them dark/light aware too". The buttons are glyphs drawn in
black; under the Dark theme the close button and the panel button of a docked
panel were black on a dark bar, and three of the four on an overlaid panel's
title. A glyph is drawn in the button's own text colour now
(Gui::OverlayToolButton).

Claims, under Dark and then under Light:
  - every button on the title bar of the docked Tasks panel has a glyph
    lighter than what is behind it under Dark, darker under Light (the button
    pictured with its icon and without, and what differs);
  - the same for the buttons of the title of an overlaid panel, and of the
    header of a task panel shown in its view;
  - with no dialog open the Tasks panel's title has no "in its view" button;
    with a dialog of the active view open it has; closed, it has none again.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
Control = FreeCADGui.Control
STEPS = []
STATE = {}
# how far a glyph's body has to be from what is behind it, of 255, to count as seen
APART = 40


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


def mw():
    return FreeCADGui.getMainWindow()


def cls(w):
    return w.metaObject().className()


def all_widgets(name):
    out = []
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == name:
                out.append(w)
        except RuntimeError:
            pass
    return out


def light(rgb):
    return (((rgb >> 16) & 255) * 299 + ((rgb >> 8) & 255) * 587 + (rgb & 255) * 114) / 1000.0


def stands_out(tag, button):
    """How far the button's glyph is from what is behind it, and which way:
    the button pictured with its icon and again without, and the pixels that
    differ. Above zero the glyph is lighter than its ground, below it darker.
    The strongest quarter of them: the glyph's body, not its smoothed rim."""
    bar = button.parentWidget()
    rect = QtCore.QRect(button.pos(), button.size())
    owner = button.defaultAction() or button
    icon = owner.icon()
    seen = bar.grab(rect).toImage()
    seen.save(os.path.join(OUT, tag + ".png"))
    owner.setIcon(QtGui.QIcon())
    settle(60)
    bare = bar.grab(rect).toImage()
    owner.setIcon(icon)
    settle(60)
    deltas = []
    for y in range(min(seen.height(), bare.height())):
        for x in range(min(seen.width(), bare.width())):
            d = light(seen.pixel(x, y) & 0xffffff) - light(bare.pixel(x, y) & 0xffffff)
            if abs(d) > 10:
                deltas.append(d)
    if len(deltas) < 6:
        return 0.0, "%d pixels of a glyph" % len(deltas)
    deltas.sort(key=abs)
    body = deltas[len(deltas) * 3 // 4:]
    apart = sum(body) / len(body)
    return apart, "the glyph %+.0f from its ground over %d pixels" % (apart, len(deltas))


def title_buttons(root):
    return [b for b in root.findChildren(QtWidgets.QAbstractButton)
            if cls(b) == "Gui::OverlayToolButton" and b.isVisible() and not b.icon().isNull()]


def judge(theme, where, root):
    buttons = title_buttons(root)
    if not check("%s, %s: the title has its buttons" % (theme, where), len(buttons) >= 2,
                 [b.objectName() for b in buttons]):
        return
    for i, b in enumerate(buttons):
        name = b.objectName() or "button %d" % i
        apart, detail = stands_out("%s-%s-%s" % (theme, where.replace(" ", "-"),
                                                 name.replace(" ", "")), b)
        seen(theme, where, name, apart, detail)


def seen(theme, where, name, apart, detail):
    """Light glyphs under the dark theme, dark ones under the light"""
    if theme == "Dark":
        check("%s, %s: %s is lighter than its ground" % (theme, where, name),
              apart >= APART, detail)
    else:
        check("%s, %s: %s is darker than its ground" % (theme, where, name),
              apart <= -APART, detail)


def tasks_dock():
    for dock in mw().findChildren(QtWidgets.QDockWidget):
        if dock.objectName() == "Combo View" and dock.isVisible():
            return dock
    for dock in mw().findChildren(QtWidgets.QDockWidget):
        if dock.isVisible() and dock.findChild(QtWidgets.QAbstractButton, "OBTN TaskHost"):
            return dock
    return None


def host_button():
    """The "in its view" button that is shown, if one is: a title bar made
    again leaves the old one's button behind, hidden."""
    for b in mw().findChildren(QtWidgets.QAbstractButton, "OBTN TaskHost"):
        if b.isVisible():
            return b
    return None


class Panel:
    def __init__(self):
        self.form = QtWidgets.QWidget()
        self.form.setWindowTitle("a panel")
        layout = QtWidgets.QVBoxLayout(self.form)
        layout.addWidget(QtWidgets.QLabel("a line", self.form))

    def accept(self):
        return True

    def reject(self):
        return True


def one_theme(theme):
    if not FreeCADGui.applyTheme(theme):
        check("%s: the theme is applied" % theme, False)
        return
    settle(6000)
    dock = tasks_dock()
    if not check("%s: the Tasks panel is docked" % theme, dock is not None):
        return
    bar = dock.titleBarWidget()

    # docked, no dialog
    VIEW.SetBool("TaskPanelInView", False)
    settle(300)
    button = host_button()
    check("%s: with no dialog open the title has no 'in its view' button" % theme,
          button is None or not button.isVisible())
    judge(theme, "docked", bar)

    # docked, a dialog of the active view in the Tasks tab
    mw().setActiveWindow(FreeCADGui.getDocument("TitleButtons").mdiViewsOfType(
        "Gui::View3DInventor")[0])
    settle(300)
    Control.showDialog(Panel())
    settle(800)
    button = host_button()
    if check("%s: with a dialog of the view open, the button is there" % theme,
             button is not None and button.isVisible()):
        apart, detail = stands_out("%s-docked-TaskHost" % theme, button)
        seen(theme, "docked", "the 'in its view' button", apart, detail)

    # the same dialog in its view: the header of its host
    VIEW.SetBool("TaskPanelInView", True)
    Control.applyPlaceToViews()
    settle(800)
    hosts = [h for h in all_widgets("Gui::TaskView::TaskPanelHost") if not h.isHidden()]
    if check("%s: the panel is in its view" % theme, len(hosts) == 1, len(hosts)):
        judge(theme, "in its view", hosts[0])
    button = host_button()
    check("%s: the panel gone to its view, the Tasks title has no button to send it" % theme,
          button is None or not button.isVisible())
    VIEW.SetBool("TaskPanelInView", False)
    Control.applyPlaceToViews()
    settle(500)
    Control.closeDialog()
    settle(500)
    button = host_button()
    check("%s: the dialog closed, the button is gone again" % theme,
          button is None or not button.isVisible())

    # overlaid: the title a dock shows while it has the focus
    FreeCADGui.runCommand("Std_DockOverlayAll")
    settle(2000)
    shown = 0
    for tab in all_widgets("Gui::OverlayTabWidget"):
        if not (tab.isVisible() and tab.count()):
            continue
        tree = tab.findChild(QtWidgets.QTreeWidget)
        if tree is None or not tree.isVisible():
            continue
        mw().activateWindow()
        tree.setFocus(QtCore.Qt.OtherFocusReason)
        settle(1500)
        judge(theme, "overlaid", tab)
        shown += 1
        break
    check("%s: an overlaid panel with the tree was there to look at" % theme, shown == 1)
    FreeCADGui.runCommand("Std_DockOverlayAll")
    settle(2000)


def start():
    VIEW.SetBool("UseViewArea", True)
    VIEW.SetBool("TaskPanelInView", False)
    doc = FreeCAD.newDocument("TitleButtons")
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    settle(800)


def themes():
    for theme in ("Dark", "Light"):
        one_theme(theme)


def finish():
    VIEW.SetBool("TaskPanelInView", False)
    try:
        if Control.activeDialog():
            Control.closeDialog()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, mw().close)


def advance():
    if not STEPS:
        finish()
        return
    delay, fn = STEPS.pop(0)

    def run():
        try:
            fn()
        except Exception:
            note("ABORT in %s: %s" % (fn.__name__, traceback.format_exc().replace("\n", " | ")))
            STEPS.clear()
        advance()

    QtCore.QTimer.singleShot(delay, run)


STEPS.append((2500, start))
STEPS.append((500, themes))
advance()
