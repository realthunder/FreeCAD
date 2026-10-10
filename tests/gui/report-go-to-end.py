"""The report view follows its newest line unless told not to.

"Go to end" in the report view's options keeps the newest line in sight as
output arrives. It was off unless switched on, so a profile that never
touched it had a report view that stayed where it was while messages piled
up below. Asked by hand (docs/HandsOnQueue.md entry 31): on by default.

Claims, on a profile that never stored the option:

  - more lines than the view shows are printed, and the last one is in
    sight afterwards;
  - the option is stored nowhere: the default did it, not a write;
  - switched off (the stored key, which is what the menu entry writes), the
    view stays where it is put while more lines arrive;
  - switched on again, it follows.

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
GROUP = "User parameter:BaseApp/Preferences/OutputWindow"


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


def report_edit():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QTextEdit):
        if "ReportOutput" in w.metaObject().className():
            return w
    return None


def last_line_in_sight(edit):
    """Whether the document's last block with text lies inside the viewport."""
    block = edit.document().lastBlock()
    while block.isValid() and not block.text().strip():
        block = block.previous()
    rect = edit.document().documentLayout().blockBoundingRect(block)
    top = rect.top() - edit.verticalScrollBar().value()
    return 0 <= top and top + rect.height() <= edit.viewport().height() + 2, block.text()


def print_lines(tag, count):
    """Lines that begin differently: the view holds back a line that starts like a recent one."""
    for i in range(count):
        word = "".join(chr(ord("a") + (i // n) % 26) for n in (676, 26, 1))
        FreeCAD.Console.PrintMessage("%s%s go to end test: line %d of %d\n" % (word, tag, i + 1, count))
    settle(1.5)


def run():
    try:
        mw = FreeCADGui.getMainWindow()
        mw.showNormal()
        mw.resize(1200, 800)
        edit = report_edit()
        if edit is None:
            raise RuntimeError("no report view")
        for dock in mw.findChildren(QtWidgets.QDockWidget):
            if dock.isAncestorOf(edit):
                dock.show()
                dock.raise_()
        settle(1.0)
        group = FreeCAD.ParamGet(GROUP)
        stored = [name for kind, name, value in group.GetContents() or [] if name == "checkGoToEnd"]
        check("the profile never stored the option", not stored, stored)

        edit.clear()
        print_lines("a", 300)
        bar = edit.verticalScrollBar()
        check("more lines were printed than the view shows", bar.maximum() > 0,
              "%d blocks, scroll range %d" % (edit.document().blockCount(), bar.maximum()))
        seen, text = last_line_in_sight(edit)
        check("the last line is in sight", seen and "line 300 of 300" in text,
              "scroll %d of %d, last line %r" % (bar.value(), bar.maximum(), text))
        stored = [name for kind, name, value in group.GetContents() or [] if name == "checkGoToEnd"]
        check("the option is still stored nowhere", not stored, stored)

        group.SetBool("checkGoToEnd", False)
        settle(0.3)
        bar.setValue(0)
        settle(0.3)
        print_lines("b", 200)
        seen, text = last_line_in_sight(edit)
        check("switched off, the view stays where it was put", bar.value() == 0 and not seen,
              "scroll %d of %d" % (bar.value(), bar.maximum()))

        group.SetBool("checkGoToEnd", True)
        settle(0.3)
        print_lines("c", 50)
        seen, text = last_line_in_sight(edit)
        check("switched on again, it follows", seen and "line 50 of 50" in text,
              "scroll %d of %d, last line %r" % (bar.value(), bar.maximum(), text))
        group.RemBool("checkGoToEnd")
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
