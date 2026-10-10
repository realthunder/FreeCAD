"""Bringing the omni search up for the first time makes no command's icon.

The box's completers show their rows in lists of their own. Qt lays a list
out the moment it becomes a popup window and asks the delegate it has then
for the size of every row; the stock delegate answers by reading the row's
icon. So the first bring-up of a session loaded and rendered the icon of
every command there is before anything was shown: 0.7 s, and another 0.5 s
of layout after it, on the reporter's configuration -- "an obvious freeze
time" (the reporter, 2026-10-07). The lists are made with the box's own
delegate in place first, whose row size is that of its text.

Claims, in a session that has not brought the box up:

  - the command brings the box up;
  - the command list is there, with hundreds of rows;
  - no icon was made for it: reading every row's icon afterwards still
    costs what making them costs -- more than the whole bring-up did (had
    they been made, reading them back would take no time);
  - "/cmd pad" still lists commands, the first row with its icon, and the
    rows are the two-line ones of the box's delegate.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
Qt = QtCore.Qt


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.005)


def widget(name):
    for w in QtWidgets.QApplication.allWidgets():
        if w.objectName() == name:
            return w
    return None


def popup_lists():
    """The completers' lists: top-level list views with a model."""
    return [w for w in QtWidgets.QApplication.allWidgets()
            if isinstance(w, QtWidgets.QListView) and w.isWindow() and w.model() is not None]


def run():
    try:
        settle(1.0)
        check("the box was not brought up before", widget("OmniSearchBox") is None)
        t = time.perf_counter()
        FreeCADGui.runCommand("Std_OmniSearch")
        settle(0.0)
        up = time.perf_counter() - t
        t = time.perf_counter()
        settle(1.0)
        edit = widget("OmniSearchEdit")
        if not check("the command brings the box up", edit is not None and edit.isVisible(),
                     "%.0f ms" % (up * 1000)):
            return
        lists = popup_lists()
        # the list whose rows name commands, "Title (Std_Name)". Not the
        # longest one: the parameters outnumber the commands since the
        # Python modules' settings are listed (1400 to some 600), and their
        # rows have no icon to make.
        commands = None
        for w in lists:
            first = str(w.model().index(0, 0).data(Qt.DisplayRole) or "")
            if w.model().rowCount() and "_" in first and first.rstrip().endswith(")"):
                commands = w
        rows = commands.model().rowCount() if commands else 0
        if not check("the command list is there", rows > 300, "%d lists, the commands' %d rows" % (len(lists), rows)):
            return
        t = time.perf_counter()
        with_icon = 0
        model = commands.model()
        for row in range(rows):
            icon = model.index(row, 0).data(Qt.DecorationRole)
            if icon is not None and not icon.isNull():
                with_icon += 1
        icons = time.perf_counter() - t
        check("no icon was made when the box came up", icons > up and icons > 0.05,
              "the bring-up %.0f ms; reading the icons of %d rows afterwards %.0f ms (%d have one)" % (
                  up * 1000, rows, icons * 1000, with_icon))

        text = "/cmd pad"
        edit.setText(text)
        edit.setCursorPosition(len(text))
        edit.textEdited.emit(text)
        settle(0.8)
        shown = [w for w in popup_lists() if w.isVisible()]
        if check("asking for a command shows a list", len(shown) == 1, "%d lists shown" % len(shown)):
            view = shown[0]
            count = view.model().rowCount()
            first = view.model().index(0, 0)
            icon = first.data(Qt.DecorationRole)
            height = view.visualRect(first).height()
            line = QtGui.QFontMetrics(view.font()).height()
            check("it lists commands for the word", 0 < count < rows, "%d rows, first %r" % (count, first.data(Qt.DisplayRole)))
            check("its first row has an icon", icon is not None and not icon.isNull())
            check("its rows are the two-line ones", height >= 2 * line - 2, "%d px, a line is %d" % (height, line))
            view.grab().save(os.path.join(OUT, "list.png"))
        QtWidgets.QApplication.sendEvent(edit, QtGui.QKeyEvent(QtCore.QEvent.KeyPress, Qt.Key_Escape, Qt.NoModifier))
        settle(0.5)
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
