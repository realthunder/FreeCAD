"""'Item background follows the theme' stands beside the tree's item background
colour, and greys it out while it is on.

docs/HandsOnQueue.md entry 87: "what's the setting for auto tree item
background item. why it is not exposed in UI preference dialog, tree view
section", and to the point put: "yes, move below or beside tree item color
selection. should actually disable tree item color selection if enabled".

The setting (View/DockOverlayThemeItemColor, entry 69) was on the page,
among the overlay's settings; the colour it stands in for
(TreeView/ItemBackground) is in the section "Tree view".

Claims, with the preferences open:
  - the check box and the colour button are on the page, in one group box,
    whose title is "Tree view", the check box in the row after the colour's;
  - the check box is on (the default) and the colour button and its label
    cannot be used;
  - off: both can; on again: both cannot.
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
    box = colour = None
    for w in dialog.findChildren(QtWidgets.QCheckBox):
        if entry_of(w) == "DockOverlayThemeItemColor":
            box = w
    for w in dialog.findChildren(QtWidgets.QPushButton):
        if entry_of(w) == "ItemBackground":
            colour = w
    if not check("the check box and the colour button are in the preferences",
                 box is not None and colour is not None, "%s, %s" % (box, colour)):
        return
    group = group_box_of(box)
    check("both are in one group box", group is not None and group is group_box_of(colour))
    check("whose title is 'Tree view'", group is not None and group.title() == "Tree view",
          group.title() if group is not None else None)
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
