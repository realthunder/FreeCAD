"""The status bar's progress bar keeps its place while the bar shows a message.

docs/HandsOnQueue.md entry 55: "sometimes the progress bar in status bar
moved to left side. I saw once when recompute. then when I close document
and open one again it seems back to normal position".

A warning or an error meant for the user is shown in the status bar as
QStatusBar's own temporary message (MainWindow::showStatus), for five
seconds, and while one is up QStatusBar hides every widget that is not a
permanent one. The progress bar was
registered in the left, non-permanent slot behind the preselection label,
whose stretch is what holds it off the left end. A recompute that warns
puts a message up, the label goes, and a progress bar shown then -- it
shows itself two seconds into an operation -- had nothing before it: it sat
at the left end, over the message. It is a permanent widget now, the first
of the right-hand group, which is where it was before the item registry.

Claims:
  - with nothing else going on, a running sequence shows the bar to the
    right of the preselection label, away from the left end;
  - with a warning's message up when the sequence starts, the bar is shown
    all the same, and where it is without one, give or take what the
    notification area beside it grows by;
  - the message is still readable: the bar does not start over it.
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
        sb = mw.statusBar()
        bar = mw.findChild(QtWidgets.QProgressBar, "progressBar")
        label = mw.findChild(QtWidgets.QLabel, "actionLabel")
        if bar is None or label is None:
            raise RuntimeError("no progress bar or no preselection label in the main window")
        if not sb.isVisible():
            raise RuntimeError("the status bar is not shown; nothing to judge")

        def place(tag, before=None):
            """Run a sequence and say where the bar is, in the status bar's
            own pixels, once it has come up."""
            if before:
                before()
            progress = FreeCAD.Base.ProgressIndicator()
            progress.start("working", 10)
            progress.next()
            # the bar's own minimum duration is 2 s
            settle(1.3)
            if before:
                before()
            settle(1.3)
            progress.next()
            settle(0.2)
            shown = bar.isVisible()
            left = bar.mapTo(sb, QtCore.QPoint(0, 0)).x()
            message = sb.currentMessage()
            sb.grab().save(os.path.join(OUT, tag + ".png"))
            note("NOTE %s: bar shown %s at x %d of %d, %d wide; label shown %s; message '%s'" % (
                tag, shown, left, sb.width(), bar.width(), label.isVisible(), message[:40]))
            progress.stop()
            # the indicator stays engaged for a grace period of 200 ms
            settle(1.0)
            return shown, left, message

        settle(1.0)
        shown, plain, _message = place("plain")
        check("a running sequence shows the bar", shown)
        check("to the right of the preselection label, away from the left end",
              plain > sb.width() * 0.2, "at x %d of %d" % (plain, sb.width()))

        def warn():
            # one meant for the user: a developer's warning goes to the report
            # view only
            FreeCAD.Console.PrintTranslatedUserWarning("a warning for the status bar\n")
            settle(0.3)

        shown, left, message = place("warned", warn)
        if check("a warning is shown as the status bar's own message", bool(message), message):
            check("with a message up a running sequence shows the bar all the same", shown)
            # not to the pixel: the notification area beside it counts the
            # warning, and is that much wider
            check("and where it is without one, in the right-hand group",
                  abs(left - plain) <= 80 and left > sb.width() // 2,
                  "at x %d, without a message at x %d" % (left, plain))
            width = sb.fontMetrics().horizontalAdvance(message)
            check("the bar does not start over the message", left >= min(width, sb.width() // 3),
                  "the message is %d px wide, the bar starts at %d" % (width, left))
        # let the message time out before the session ends
        sb.clearMessage()
        settle(0.3)
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))

    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
