"""The preferences opened and cancelled, nothing touched: no setting is written, Cancel asks nothing.

The dialog copies the user parameters when it opens and takes ANY
parameter written while it is open for a change: Cancel then asks "Do you
want to revert back to previous settings before exit?". A page that writes
while it merely loads therefore puts that question to a user who changed
nothing. One did: Part's Measure page made "defaultFont" the current entry
of its font box after the restore, and with preferences applied as they
are made (the default) the box stored the key at once.

Preference widgets save on a change of their value when preferences are
applied at once, so a page must not set a widget's value after its restore
without blocking the widget's signals -- and a page that does shows up
here, whichever it is.

Claims, on a fresh profile, after the start has settled:

  - with the preferences open for three seconds and nothing touched, no
    key below Preferences is written (the keys are named when one is);
  - Cancel closes the dialog and asks nothing.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
KEEP = []


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


def path_of(group):
    names = []
    try:
        for _ in range(20):
            if group is None:
                break
            names.append(group.GetGroupName())
            group = group.Parent()
    except Exception:
        names.append("?")
    return "/".join(reversed([n for n in names if n]))


class Watch:
    """Every parameter written, by the manager's own signal: the one the dialog listens to."""

    def __init__(self):
        self.seen = []

    def slotParamChanged(self, param, tp, name, value):
        self.seen.append("%s/%s = %s" % (path_of(param), name, str(value)[:60]))


def open_and_cancel(watch):
    """Opens the preferences, leaves them alone for three seconds, presses Cancel.

    Returns what was written with the dialog open and the texts of the boxes
    Cancel raised. A click is always sent from a timer of its own: a box
    raised by the click is a nested event loop, and the timer that looks for
    the box does not fire while its own slot is still inside one. The button
    is looked up when that timer fires, from the dialog: a button taken from
    a button box that is no longer referenced is a dead object to PySide.
    """
    state = {"ticks": 0, "cancelled": 0, "open": [], "boxes": []}
    timer = QtCore.QTimer()

    def click(owner, find):
        QtCore.QTimer.singleShot(0, lambda: find(owner).click())

    def act():
        state["ticks"] += 1
        widget = QtWidgets.QApplication.activeModalWidget()
        if widget is None:
            return
        if isinstance(widget, QtWidgets.QMessageBox):
            state["boxes"].append(widget.text())
            click(widget, lambda box: box.button(QtWidgets.QMessageBox.No))
            return
        if state["ticks"] < 6:
            return
        if not state["cancelled"]:
            state["open"] = list(watch.seen)
        if state["cancelled"] % 10 == 0:
            click(widget, lambda dialog: dialog.findChild(QtWidgets.QDialogButtonBox).button(
                QtWidgets.QDialogButtonBox.Cancel))
        state["cancelled"] += 1

    timer.timeout.connect(act)
    timer.start(500)
    FreeCADGui.showPreferences()
    timer.stop()
    settle(0.5)
    return state["open"], state["boxes"]


def run():
    try:
        # A module's pages are in the dialog only once the module is loaded.
        # Fem's are here because one of them stored a setting when it was
        # shown: its VTK page saved the export level where it meant to load it.
        try:
            __import__("FemGui")
        except ImportError as e:
            note("INFO not loaded: FemGui (%s)" % e)
        watch = Watch()
        root = FreeCAD.ParamGet("User parameter:BaseApp")
        root.AttachManager(watch)
        KEEP.extend([root, watch])
        # The start writes for a while: the window's geometry, the dock
        # windows, the workbench. None of that is the dialog's.
        settle(4.0)
        note("INFO written while the start settled: %d" % len(watch.seen))
        del watch.seen[:]

        written, boxes = open_and_cancel(watch)
        settings = [w for w in written if w.startswith("BaseApp/Preferences/")]
        check("the preferences open and untouched write no setting", not settings, settings[:8])
        if len(written) != len(settings):
            note("INFO written outside Preferences meanwhile: %s"
                 % [w for w in written if w not in settings][:8])
        check("Cancel asks nothing", not boxes, boxes)

        del watch.seen[:]
        written, boxes = open_and_cancel(watch)
        settings = [w for w in written if w.startswith("BaseApp/Preferences/")]
        check("nor does a second time", not settings and not boxes, (settings[:8], boxes))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
