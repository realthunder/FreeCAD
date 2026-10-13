"""The close icon of an overlaid panel is a cross in a square frame.

docs/HandsOnQueue.md entry 96: "add a border to the close icon for overlay
pannel, so it does not look like a icon not found defect" -- a square, a
line only; the close button alone; and wherever else the same icon is
shown, a menu too. So the frame is in the picture, overlay/close.svg, which
the overlay's title bar, a task panel in its view and the commands that
close a panel all take.

Claims, the picture drawn 24 by 24:
  - the middle of each of its four sides is drawn: the frame;
  - its centre is drawn: the cross;
  - between the frame and the cross, beside the middle of the left side,
    nothing is drawn: a line, not a filled ground.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
SIZE = 24


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def alpha(image, x, y):
    return (image.pixel(x, y) >> 24) & 255


def drawn(image, x, y):
    """Something drawn at or right beside a point"""
    return max(alpha(image, x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)
               if 0 <= x + dx < image.width() and 0 <= y + dy < image.height()) > 40


def run():
    icon = QtGui.QIcon("qss:overlay/close.svg")
    image = icon.pixmap(SIZE, SIZE).toImage().convertToFormat(QtGui.QImage.Format_ARGB32)
    image.save(os.path.join(OUT, "close.png"))
    if not check("the picture is found and drawn", not image.isNull() and image.width() >= SIZE,
                 "%dx%d" % (image.width(), image.height())):
        return
    w, h = image.width(), image.height()
    sides = {"top": (w // 2, 1), "bottom": (w // 2, h - 2), "left": (1, h // 2),
             "right": (w - 2, h // 2)}
    missing = [name for name, (x, y) in sides.items() if not drawn(image, x, y)]
    check("the middle of each of its four sides is drawn", not missing, missing)
    check("its centre is drawn", drawn(image, w // 2, h // 2))
    gap = (w * 3 // 24 + 2, h // 2)
    check("beside the middle of the left side, inside the frame, nothing is drawn",
          alpha(image, gap[0], gap[1]) == 0, "%s: alpha %d" % (gap, alpha(image, *gap)))


def start():
    try:
        run()
    except Exception:
        note("ABORT: " + traceback.format_exc().replace("\n", " | "))
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(2500, start)
