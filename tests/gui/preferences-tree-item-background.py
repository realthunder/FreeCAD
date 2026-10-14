"""'Item background follows the theme' stands beside the tree's item background
colour, and greys it out while it is on.

docs/HandsOnQueue.md entry 87: "what's the setting for auto tree item
background item. why it is not exposed in UI preference dialog, tree view
section", and to the point put: "yes, move below or beside tree item color
selection. should actually disable tree item color selection if enabled".

The setting (View/DockOverlayThemeItemColor, entry 69) was on the page,
among the overlay's settings; the colour it stands in for
(TreeView/ItemBackground) is in the section "Tree view".

Both moved 2026-10-14 (entry 98): "move the item background color and auto
settings from UI setting page to Display -> Colors page, in a group box named
Overlay. Also create a groupbox in that page named 'Display Style', and put
color related settings in Display -> Display Style page there (note, do not
move, just duplicate those settings)."

Claims, with the preferences open:
  - the check box and the colour button are in the preferences once each, in
    one group box, whose title is "Overlay", the check box in the row after
    the colour's;
  - the check box is on (the default) and the colour button and its label
    cannot be used;
  - off: both can; on again: both cannot;
  - a group box "Display Style" beside it holds the colours of the display
    styles page -- the hidden line face, line and background colours with
    their override boxes, the shadow's light and ground colours -- and each
    of those is still on the display styles page too.
The dialog is left with its close button: nothing is asked.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
GOT = {"ticks": 0, "done": False}


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


def entry_of(widget):
    value = widget.property("prefEntry")
    if value is None:
        return ""
    try:
        return bytes(value).decode()
    except Exception:
        return str(value)


def group_box_of(widget):
    p = widget.parentWidget()
    while p is not None and not isinstance(p, QtWidgets.QGroupBox):
        p = p.parentWidget()
    return p


def look(dialog):
    boxes = [w for w in dialog.findChildren(QtWidgets.QCheckBox)
             if entry_of(w) == "DockOverlayThemeItemColor"]
    colours = [w for w in dialog.findChildren(QtWidgets.QPushButton)
               if entry_of(w) == "ItemBackground"]
    if not check("the check box and the colour button are in the preferences, once each",
                 len(boxes) == 1 and len(colours) == 1, "%d, %d" % (len(boxes), len(colours))):
        return
    box, colour = boxes[0], colours[0]
    group = group_box_of(box)
    check("both are in one group box", group is not None and group is group_box_of(colour))
    check("whose title is 'Overlay'", group is not None and group.title() == "Overlay",
          group.title() if group is not None else None)
    display_style(dialog, group)
    label = None
    for w in group.findChildren(QtWidgets.QLabel) if group is not None else []:
        if w.text().replace("&", "") == "Item background color":
            label = w
    check("the colour has its label", label is not None)
    check("the check box says 'Item background follows the theme'",
          box.text().replace("&", "") == "Item background follows the theme", box.text())
    # one row after the other: the rows of the group box, layouts that hold
    # widgets, in the order the box lays them out
    rows = []

    def collect(layout):
        for i in range(layout.count()):
            sub = layout.itemAt(i).layout()
            if sub is None:
                continue
            if any(sub.itemAt(j).widget() is not None for j in range(sub.count())):
                rows.append(sub)
            collect(sub)

    collect(group.layout())

    def row_of(widget):
        # a grid's rows are its own (the group is made in code on its page)
        grid = group.layout()
        if isinstance(grid, QtWidgets.QGridLayout) and grid.indexOf(widget) >= 0:
            return grid.getItemPosition(grid.indexOf(widget))[0]
        for i, row in enumerate(rows):
            if row.indexOf(widget) >= 0:
                return i
        return None

    check("the check box is in the row after the colour's",
          row_of(colour) is not None and row_of(box) == row_of(colour) + 1,
          "colour in row %s, check box in row %s" % (row_of(colour), row_of(box)))

    check("the check box is on, the default", box.isChecked())
    check("on: the colour button and its label cannot be used",
          not colour.isEnabled() and (label is None or not label.isEnabled()))
    box.setChecked(False)
    settle(200)
    check("off: both can", colour.isEnabled() and (label is None or label.isEnabled()))
    box.setChecked(True)
    settle(200)
    check("on again: both cannot",
          not colour.isEnabled() and (label is None or not label.isEnabled()))


def display_style(dialog, overlay_group):
    """The colours of the display styles page, duplicated in a group beside"""
    page = overlay_group.parentWidget() if overlay_group is not None else None
    group = None
    for g in (page.findChildren(QtWidgets.QGroupBox) if page is not None else []):
        if g.title() == "Display Style":
            group = g
    if not check("a group box 'Display Style' on the same page", group is not None):
        return
    wanted = ("HiddenLineFaceColor", "HiddenLineOverrideFaceColor", "HiddenLineColor",
              "HiddenLineOverrideColor", "HiddenLineBackground", "HiddenLineOverrideBackground",
              "ShadowLightColor", "ShadowGroundColor")
    inside = set(entry_of(w) for w in group.findChildren(QtWidgets.QWidget))
    check("it holds the display styles' colours and override boxes",
          all(name in inside for name in wanted),
          [name for name in wanted if name not in inside])
    everywhere = [entry_of(w) for w in dialog.findChildren(QtWidgets.QWidget)]
    check("each of them is on the display styles page as well (twice in the preferences)",
          all(everywhere.count(name) == 2 for name in wanted),
          dict((name, everywhere.count(name)) for name in wanted))


def act():
    GOT["ticks"] += 1
    dialog = QtWidgets.QApplication.activeModalWidget()
    if dialog is None:
        return
    if isinstance(dialog, QtWidgets.QMessageBox):
        note("INFO a box came up: %s" % dialog.text())
        QtCore.QTimer.singleShot(0, lambda: dialog.button(QtWidgets.QMessageBox.No).click())
        return
    if GOT["done"]:
        if GOT["ticks"] % 6 == 0:
            QtCore.QTimer.singleShot(0, dialog.close)
        return
    GOT["done"] = True
    try:
        look(dialog)
    except Exception:
        note("ABORT: " + traceback.format_exc().replace("\n", " | "))
    QtCore.QTimer.singleShot(0, dialog.close)


def run():
    timer = QtCore.QTimer()
    timer.timeout.connect(act)
    timer.start(500)
    try:
        FreeCADGui.showPreferences()
    except Exception:
        note("ABORT: " + traceback.format_exc().replace("\n", " | "))
    timer.stop()
    settle(500)
    check("the preferences were looked at", GOT["done"])
    VIEW.RemBool("DockOverlayThemeItemColor")
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(2500, run)
