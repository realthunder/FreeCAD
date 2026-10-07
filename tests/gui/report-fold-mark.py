"""A report view line that stands in for others has its mark in the margin.

Repeats of a line are held back and shown as one line with their count;
the messages behind it unfold on a click. The line used to be underlined
to say so, and a click anywhere on it unfolded it. Asked by hand
(docs/HandsOnQueue.md entry 13): no underline, a clickable mark ahead of
the message instead, in the margin, the message itself still in line with
the ordinary ones.

Claims, on a line printed six times and an ordinary one:

  - the six come out as one line with a count, which holds messages;
  - that line is not underlined;
  - its text starts in the same column as the ordinary line's;
  - something is drawn in the margin at its height, and nothing at the
    ordinary line's;
  - a click on the line's text unfolds nothing;
  - a click on the mark shows the held messages below the line, and a
    second click takes them away again.

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
REPEATED = "fold mark test: the same line"
PLAIN = "fold mark test: an ordinary line"


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


def block_with(edit, text):
    block = edit.document().begin()
    found = None
    while block.isValid():
        if text in block.text():
            found = block
        block = block.next()
    return found


def lines(edit):
    return edit.document().blockCount()


def row(edit, block):
    """The first row of a block in the viewport: its top, its height, and
    where its text starts."""
    cursor = QtGui.QTextCursor(block)
    r = edit.cursorRect(cursor)
    return r.top(), r.height(), r.left()


def margin_ink(edit, block, name):
    """How far the pixels ahead of a line's text are from the view's own
    colour, at the line's height."""
    top, height, left = row(edit, block)
    image = edit.viewport().grab().toImage().convertToFormat(QtGui.QImage.Format_RGB32)
    image.save(os.path.join(OUT, name + ".png"))
    back = QtGui.QColor(image.pixel(image.width() - 4, top + height // 2)).lightness()
    off = 0
    for y in range(max(0, top), min(image.height(), top + height)):
        for x in range(0, max(0, left - 1)):
            off = max(off, abs(QtGui.QColor(image.pixel(x, y)).lightness() - back))
    return off, left


def click(edit, x, y):
    for kind in (QtCore.QEvent.MouseButtonPress, QtCore.QEvent.MouseButtonRelease):
        p = QtCore.QPointF(x, y)
        QtWidgets.QApplication.sendEvent(
            edit.viewport(),
            QtGui.QMouseEvent(kind, p, QtCore.QPointF(edit.viewport().mapToGlobal(p.toPoint())),
                              Qt.LeftButton, Qt.LeftButton if kind == QtCore.QEvent.MouseButtonPress
                              else Qt.NoButton, Qt.NoModifier))
    settle(0.4)


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
        edit.clear()
        FreeCAD.Console.PrintMessage(PLAIN + "\n")
        for _ in range(6):
            FreeCAD.Console.PrintMessage(REPEATED + "\n")
        # the held repeats are shown when their timeout runs out
        settle(2.5)

        plain = block_with(edit, PLAIN)
        blocks = []
        block = edit.document().begin()
        while block.isValid():
            if REPEATED in block.text():
                blocks.append(block)
            block = block.next()
        note("lines with the repeated text: %s" % [b.text() for b in blocks])
        folded = blocks[-1] if blocks else None
        if not check("six prints come out as a line with a count",
                     folded is not None and len(blocks) <= 2 and "(x" in folded.text(),
                     [b.text() for b in blocks]):
            return
        edit.ensureCursorVisible()
        settle(0.3)

        underlined = [f for f in folded.layout().formats() if f.format.fontUnderline()]
        check("the line is not underlined", not underlined, "%d underlined spans" % len(underlined))

        ink, left = margin_ink(edit, folded, "folded")
        plain_ink, plain_left = margin_ink(edit, plain, "plain")
        check("its text starts in the ordinary line's column", left == plain_left,
              "%d and %d" % (left, plain_left))
        check("a mark is drawn in the margin at its height", ink >= 30 and left >= 8,
              "%d from the view's colour, %d px of margin" % (ink, left))
        check("nothing is drawn ahead of the ordinary line", plain_ink < 30, plain_ink)

        top, height, left = row(edit, folded)
        before = lines(edit)
        click(edit, left + 40, top + height // 2)
        check("a click on the line's text unfolds nothing", lines(edit) == before,
              "%d lines, %d before" % (lines(edit), before))
        if lines(edit) != before:
            # as it was: put it back for the claims below
            click(edit, left + 40, top + height // 2)

        click(edit, left // 2, top + height // 2)
        opened = lines(edit)
        check("a click on the mark shows the held messages", opened > before,
              "%d lines, %d before" % (opened, before))
        ink_open, _ = margin_ink(edit, block_with(edit, "(x"), "unfolded")
        check("the mark is still there, unfolded", ink_open >= 30, ink_open)
        top, height, left = row(edit, block_with(edit, "(x"))
        click(edit, left // 2, top + height // 2)
        check("a second click takes them away again", lines(edit) == before,
              "%d lines, %d before" % (lines(edit), before))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
