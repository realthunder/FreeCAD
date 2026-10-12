"""The Document Recovery dialog has a check box for all its rows.

docs/HandsOnQueue.md entry 80: "in file recovery prompt dialog, add check
box in first column to toggle select all", and to the three points put: in
the header of the first column; half ticked with some rows ticked, a click
then ticks all; "check all" ticks the rows with a warning too.

The dialog comes up by itself at start-up when a session left documents
behind. This script runs before that check and leaves three such documents
in the cache directory: two of a session whose lock file nobody holds (a
crashed one: ticked when the dialog comes up) and one no lock file leads to
(listed with a warning, not ticked). Nothing is recovered; the dialog is
closed with its Cancel.

Claims:
  - the dialog comes up with the three rows, two ticked, the one with the
    warning not;
  - the first column's header has a check box (an icon), and it is half
    ticked;
  - a click on it ticks every row, the one with the warning too, and the
    box is ticked; the list is not sorted by that click;
  - a click more unticks them all, the box is empty and "Start Recovery"
    cannot be pressed;
  - one row ticked by hand: the box is half ticked;
  - a click on the header's text sorts the list and ticks nothing.
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
LEFT = QtCore.Qt.LeftButton
STATE = {"tries": 0}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=150):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def leave_behind():
    """Three documents a dead session left in the cache directory"""
    cache = FreeCAD.getUserCachePath()
    exe = FreeCAD.ConfigGet("ExeName") or "FreeCAD"
    dead, gone = 99999001, 99999002
    with open(os.path.join(cache, "%s_%d.lock" % (exe, dead)), "w"):
        pass
    for label, pid in (("Bracket", dead), ("Housing", dead), ("Orphan", gone)):
        home = os.path.join(cache, "%s_Doc_%s_abcdef_%d" % (exe, label.lower() * 2, pid))
        os.makedirs(os.path.join(home, "fc_recovery_files"))
        with open(os.path.join(home, "fc_recovery_files", "Document.xml"), "w") as f:
            f.write("<?xml version='1.0' encoding='utf-8'?>\n<Document SchemaVersion=\"4\"/>\n")
        with open(os.path.join(home, "fc_recovery_file.xml"), "w") as f:
            f.write("<?xml version='1.0' encoding='utf-8'?>\n"
                    "<AutoRecovery SchemaVersion=\"1\">\n"
                    "  <Status>Created</Status>\n"
                    "  <Label>%s</Label>\n"
                    "  <FileName></FileName>\n"
                    "</AutoRecovery>\n" % label)


def dialog():
    w = QtWidgets.QApplication.activeModalWidget()
    if w is not None and w.metaObject().className() == "Gui::Dialog::DocumentRecovery":
        return w
    for w in QtWidgets.QApplication.topLevelWidgets():
        if w.isVisible() and w.metaObject().className() == "Gui::Dialog::DocumentRecovery":
            return w
    return None


def ticks(tree):
    """label -> ticked, of the rows that have a check box"""
    out = {}
    for i in range(tree.topLevelItemCount()):
        item = tree.topLevelItem(i)
        if item.flags() & QtCore.Qt.ItemIsUserCheckable:
            out[item.text(0)] = item.checkState(0) == QtCore.Qt.Checked
    return out


NONE = QtCore.Qt.Unchecked.value
SOME = QtCore.Qt.PartiallyChecked.value
ALL = QtCore.Qt.Checked.value


def box(tree):
    """What the header's check box shows"""
    value = tree.property("allChecked")
    return int(value) if value is not None else -1


def click_box(tree):
    header = tree.header()
    style = header.style()
    margin = style.pixelMetric(QtWidgets.QStyle.PM_HeaderMargin, None, header)
    icon = style.pixelMetric(QtWidgets.QStyle.PM_SmallIconSize, None, header)
    x = header.sectionViewportPosition(0) + margin + icon // 2
    QTest.mouseClick(header.viewport(), LEFT, QtCore.Qt.NoModifier,
                     QtCore.QPoint(x, header.height() // 2))
    settle()


def click_text(tree):
    header = tree.header()
    x = header.sectionViewportPosition(0) + header.sectionSize(0) - 30
    QTest.mouseClick(header.viewport(), LEFT, QtCore.Qt.NoModifier,
                     QtCore.QPoint(x, header.height() // 2))
    settle()


def run(dlg):
    tree = dlg.findChild(QtWidgets.QTreeWidget)
    dlg.grab().save(os.path.join(OUT, "dialog.png"))
    first = ticks(tree)
    check("the dialog lists the three documents left behind",
          sorted(first) == ["Bracket", "Housing", "Orphan"], first)
    check("the two of the crashed session ticked, the one with a warning not",
          first == {"Bracket": True, "Housing": True, "Orphan": False}, first)
    check("the first column's header has a check box", not tree.headerItem().icon(0).isNull())
    check("which is half ticked", box(tree) == SOME, box(tree))
    start = None
    for button in dlg.findChildren(QtWidgets.QPushButton):
        if button.text().replace("&", "") == "Start Recovery":
            start = button

    click_box(tree)
    now = ticks(tree)
    check("a click on it ticks every row, the one with the warning too",
          all(now.values()) and len(now) == 3, now)
    check("and the box is ticked", box(tree) == ALL, box(tree))
    check("the list is not sorted by that click", tree.header().sortIndicatorSection() != 0,
          tree.header().sortIndicatorSection())
    dlg.grab().save(os.path.join(OUT, "dialog-all.png"))

    click_box(tree)
    now = ticks(tree)
    check("a click more unticks them all", not any(now.values()) and len(now) == 3, now)
    check("and the box is empty", box(tree) == NONE, box(tree))
    check("'Start Recovery' cannot be pressed then",
          start is not None and not start.isEnabled())

    for i in range(tree.topLevelItemCount()):
        if tree.topLevelItem(i).text(0) == "Housing":
            tree.topLevelItem(i).setCheckState(0, QtCore.Qt.Checked)
    settle()
    check("one row ticked by hand: the box is half ticked",
          box(tree) == SOME, box(tree))

    before = ticks(tree)
    click_text(tree)
    check("a click on the header's text sorts the list",
          tree.header().sortIndicatorSection() == 0, tree.header().sortIndicatorSection())
    check("and ticks nothing", ticks(tree) == before, ticks(tree))


def poll():
    dlg = dialog()
    if dlg is None:
        STATE["tries"] += 1
        if STATE["tries"] > 100:
            check("the recovery dialog comes up at start-up", False, "not in 20 s")
            finish()
            return
        QtCore.QTimer.singleShot(200, poll)
        return
    try:
        run(dlg)
    except Exception:
        note("ABORT: " + traceback.format_exc().replace("\n", " | "))
    dlg.reject()
    QtCore.QTimer.singleShot(500, finish)


def finish():
    # a box of any kind left up would wait on the screen
    for _ in range(3):
        w = QtWidgets.QApplication.activeModalWidget()
        if w is None:
            break
        w.reject() if hasattr(w, "reject") else w.close()
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


try:
    leave_behind()
except Exception:
    note("ABORT in leave_behind: " + traceback.format_exc().replace("\n", " | "))
QtCore.QTimer.singleShot(1500, poll)
