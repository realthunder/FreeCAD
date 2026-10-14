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

The same entry, on the stage of 13:56: "the close icon looks fine on overlay
title bar. but when viewing in menu, Views -> Dock window overlays, in dark
theme, hence invisible. light theme looks fine". The file is drawn in black;
a title button tints it when it paints and a menu does not. The four
commands that show or hide the overlays of a side now give their action the
glyph in the colour a title button paints it.

Claims, for Std_DockOverlayToggleLeft, Right, Top and Bottom:
  - under the Dark theme the action's icon is drawn light;
  - under the Light theme it is drawn dark;
  - and Dark again, after Light, light again: it follows a change of theme.
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


SIDES = ("Left", "Right", "Top", "Bottom")


def settle(ms=300):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def lightness(icon):
    """Mean lightness, 0 to 255, of what an icon draws; None if it draws nothing"""
    image = icon.pixmap(SIZE, SIZE).toImage().convertToFormat(QtGui.QImage.Format_ARGB32)
    total = count = 0
    for y in range(image.height()):
        for x in range(image.width()):
            if alpha(image, x, y) > 40:
                pixel = image.pixel(x, y)
                total += (((pixel >> 16) & 255) * 11 + ((pixel >> 8) & 255) * 16
                          + (pixel & 255) * 5) // 32
                count += 1
    return total / count if count else None


def menu_icons(tag, light):
    if not check("%s applies" % tag, FreeCADGui.applyTheme(tag.split()[0])):
        return
    settle(4000)
    for side in SIDES:
        command = FreeCADGui.Command.get("Std_DockOverlayToggle" + side)
        actions = command.getAction() if command else []
        value = lightness(actions[0].icon()) if actions else None
        if light:
            ok = value is not None and value > 200
        else:
            ok = value is not None and value < 60
        check("%s: the menu icon of 'Toggle %s' is drawn %s"
              % (tag, side.lower(), "light" if light else "dark"), ok,
              "mean lightness %s" % (None if value is None else round(value)))


def start():
    try:
        run()
        menu_icons("Dark", True)
        menu_icons("Light", False)
        menu_icons("Dark again", True)
    except Exception:
        note("ABORT: " + traceback.format_exc().replace("\n", " | "))
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(2500, start)
