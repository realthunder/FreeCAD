"""Std_Paste is active by what the clipboard holds, without asking it each time.

A command's isActive() runs on every pass over the commands: each change of
the selection, each object a load brings in. Std_Paste asked the clipboard
five questions there and Material_Paste one, and a question to the
clipboard is a call into another process. Where the clipboard does not
answer at once -- a process refused it on a managed Windows box gets its
answer after 170 ms -- one pass over the commands took 0.76 to 0.9 s, with
a document open or none, and 13 of them ran between the slices of one load
of a 686-object file (docs/HandsOnLog.md, entry 47). Both commands read
Gui::ClipboardFormats now: the formats, asked of the clipboard once for
each change of it.

A list that is remembered can be out of date, and that is what this guards:
  - nothing on the clipboard: Std_Paste is not active;
  - after Std_Copy of an object it is;
  - after the clipboard is cleared it is not again;
  - after something the application takes is put there from outside the
    commands (a file's URL), it is;
  - after a text nothing here takes is put there, it is not.

What the change is for, the cost, this cannot see where the clipboard
answers at once: every check here holds on the tree before too.

The clipboard is the session's. Under scripts/gui-test.sh that is the
test's own display. Run by hand on a desktop it is the user's: the test
writes it only if it finds it empty, leaves it empty, and says SKIP
otherwise -- as it does where the session cannot write the clipboard at
all.
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


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def wait(seconds):
    t = time.perf_counter()
    while time.perf_counter() - t < seconds:
        QtCore.QCoreApplication.processEvents()


def formats(cb):
    mime = cb.mimeData()
    return list(mime.formats()) if mime is not None else []


def paste():
    return FreeCADGui.Command.get("Std_Paste").isActive()


def run():
    cb = QtWidgets.QApplication.clipboard()
    found = formats(cb)
    if found:
        note("SKIP the clipboard holds something, and is not written to: %s" % found[:6])
        return

    doc = FreeCAD.newDocument("PasteFollows")
    box = doc.addObject("Part::Box", "Box")
    doc.recompute()
    wait(0.3)
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(box)
    wait(0.3)

    check("nothing on the clipboard: Std_Paste is not active", paste() is False)

    FreeCADGui.runCommand("Std_Copy")
    wait(0.5)
    if not formats(cb):
        note("SKIP this session cannot write the clipboard")
        return
    check("after Std_Copy, Std_Paste is active", paste() is True, formats(cb))

    cb.clear()
    wait(0.5)
    check("after the clipboard is cleared, Std_Paste is not active", paste() is False,
          formats(cb))

    mime = QtCore.QMimeData()
    mime.setUrls([QtCore.QUrl.fromLocalFile(os.path.join(OUT, "nothing.FCStd"))])
    cb.setMimeData(mime)
    wait(0.5)
    check("after a file's URL is put on the clipboard, Std_Paste is active", paste() is True,
          formats(cb))

    cb.setText("paste-follows-the-clipboard")
    wait(0.5)
    check("after a text is put there instead, Std_Paste is not active", paste() is False,
          formats(cb))

    cb.clear()
    wait(0.3)


def main():
    try:
        run()
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, main)
