"""Input is held while a blocking sequence runs, and no longer.

The progress indicator stays engaged for a grace period (200 ms) after a
sequence stops, so that a sequence per work item -- one per shape of an
import -- does not set up and tear down the wait cursor, the application
event filter and the status bar each time. The claim on the input was kept
through that period with the rest: the progress bar's own filter lets events
pass once nothing runs, but the wait cursor it holds swallows every key and
mouse button event for as long as it lives. A key or a click within 200 ms
of the end of any operation went nowhere.

It showed as a test that passed on its first run and failed on every other
(sketch-external-tool-hint.py): an Escape sent right after entering a
sketch's edit and starting a tool did not leave the tool, when the program
had started fast enough to send it inside the period.

Claims, with a line edit and a button in a window of their own:

  - with nothing running, a key and a click arrive;
  - while a blocking sequence runs, they do not;
  - right after it stops, they arrive;
  - a sequence started within the grace period holds the input again, and
    gives it back when it stops;
  - after the grace period, they still arrive.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets
from PySide6 import QtTest

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
    window = None
    try:
        mw = FreeCADGui.getMainWindow()
        window = QtWidgets.QWidget(mw, QtCore.Qt.Tool)
        layout = QtWidgets.QVBoxLayout(window)
        edit = QtWidgets.QLineEdit(window)
        button = QtWidgets.QPushButton("press", window)
        layout.addWidget(edit)
        layout.addWidget(button)
        clicks = []
        button.clicked.connect(lambda: clicks.append(1))
        window.show()
        settle(1.0)

        def key(letter):
            QtTest.QTest.keyClick(edit, getattr(QtCore.Qt, "Key_" + letter.upper()))

        def click():
            QtTest.QTest.mouseClick(button, QtCore.Qt.LeftButton)

        def state():
            return edit.text(), len(clicks)

        key("a")
        click()
        if not check("with nothing running, a key and a click arrive",
                     state() == ("a", 1), state()):
            raise RuntimeError("the instrument does not deliver input")

        progress = FreeCAD.Base.ProgressIndicator()
        progress.start("working", 10)
        progress.next()
        key("b")
        click()
        check("while a blocking sequence runs, they do not", state() == ("a", 1), state())
        progress.stop()

        key("c")
        click()
        check("right after it stops, they arrive", state() == ("ac", 2), state())

        # within the grace period: the indicator is still engaged
        progress.start("working again", 10)
        progress.next()
        key("d")
        click()
        check("a sequence started within the grace period holds the input again",
              state() == ("ac", 2), state())
        progress.stop()
        key("e")
        click()
        check("and gives it back when it stops", state() == ("ace", 3), state())

        settle(0.6)
        key("f")
        click()
        check("after the grace period they still arrive", state() == ("acef", 4), state())
    except Exception:
        note("ABORT:\n" + traceback.format_exc())

    if window is not None:
        window.close()
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
