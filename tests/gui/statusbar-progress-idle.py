"""The status bar's progress bar is on screen only while something runs.

The status bar's item registry (MainWindow::addStatusBarItem) lays the bar
out again whenever an item is registered or removed, and QStatusBar shows
whatever it is handed. The registry therefore asks each widget whether it
owns its own visibility -- the progress bar does, the sequencer shows it
while an operation runs -- and for those it restores what was on screen
instead of showing them. It asks by looking for a userEnabled property.
The registry was ported from upstream without the property on the bar, so
the answer was always no: every registration showed an idle progress bar,
empty, and it stayed until the next operation ended. Seen by hand on a
freshly started program with no document open.

Claims:

  - after start-up, with nothing running, the bar is hidden;
  - registering a status bar item leaves it hidden, and so does removing it;
  - a running sequence shows it, a registration meanwhile keeps it shown,
    and it is hidden again once the sequence is over;
  - the bar has a userEnabled property; with it off a running sequence
    does not show the bar and neither does a registration, and with it
    back on a sequence shows it again.

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


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def run():
    try:
        mw = FreeCADGui.getMainWindow()
        bar = mw.findChild(QtWidgets.QProgressBar, "progressBar")
        if bar is None:
            raise RuntimeError("no progress bar in the main window")
        if not mw.statusBar().isVisible():
            raise RuntimeError("the status bar is not shown; nothing to judge")

        def register(item_id):
            label = QtWidgets.QLabel("probe", mw.statusBar())
            mw.addStatusBarItem(label, id=item_id, title="Probe", slot="Right", order=600)
            settle()
            return label

        def run_sequence(body=None):
            """Start a sequence, report whether the bar came up, stop it."""
            progress = FreeCAD.Base.ProgressIndicator()
            progress.start("working", 10)
            progress.next()
            # the bar's own minimum duration is 2 s
            settle(2.6)
            progress.next()
            settle(0.2)
            shown = bar.isVisible()
            during = body() if body else None
            progress.stop()
            # the indicator stays engaged for a grace period of 200 ms
            settle(1.0)
            return shown, during

        settle(1.0)
        check("after start-up, with nothing running, the bar is hidden", not bar.isVisible())

        register("GuiTestProbe")
        check("registering a status bar item leaves it hidden", not bar.isVisible())
        mw.removeStatusBarItem("GuiTestProbe")
        settle()
        check("removing one leaves it hidden", not bar.isVisible())

        def relayout_meanwhile():
            register("GuiTestProbe2")
            return bar.isVisible()

        shown, kept = run_sequence(relayout_meanwhile)
        check("a running sequence shows it", shown)
        check("a registration meanwhile keeps it shown", kept)
        check("it is hidden again once the sequence is over", not bar.isVisible())
        mw.removeStatusBarItem("GuiTestProbe2")
        settle()

        has_property = bar.metaObject().indexOfProperty("userEnabled") >= 0
        if check("the bar has a userEnabled property", has_property):
            bar.setProperty("userEnabled", False)
            shown, _ = run_sequence()
            check("with it off a running sequence does not show the bar", not shown)
            register("GuiTestProbe3")
            check("and neither does a registration", not bar.isVisible())
            mw.removeStatusBarItem("GuiTestProbe3")
            bar.setProperty("userEnabled", True)
            shown, _ = run_sequence()
            check("with it back on a sequence shows it again", shown)
            check("and it is hidden once that is over", not bar.isVisible())
    except Exception:
        note("ABORT:\n" + traceback.format_exc())

    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
