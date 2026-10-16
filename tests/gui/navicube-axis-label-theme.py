"""The navigation cube's axis labels take their colour from the theme; one set
by hand wins.

docs/HandsOnQueue.md entry 156 (the reporter, 2026-10-16 09:59): "make the
navicube axis cross label font color theme aware"; 10:02: "from each theme.
by hand wins. only axis label". The labels were black under every theme,
on a dark 3D background too.

A theme states the colour as NaviCube/ThemeAxisLabelColor, which applying a
theme writes; a colour set by hand is NaviCube/AxisLabelColor, which no theme
carries, and the cube takes it first (NaviCubeShared::getParams). Claims:
  - the Dark theme states a light colour, the Light theme black;
  - a colour set by hand is kept when a theme is applied.

Runs with QT_QPA_PLATFORM=offscreen.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
CUBE = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/NaviCube")
SENTINEL = 0x12345678


def note(msg):
    with open(RESULT, "a", encoding="utf-8") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=300):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def theme_colour():
    return CUBE.GetUnsigned("ThemeAxisLabelColor", SENTINEL)


def lightness(argb):
    return max((argb >> 16) & 255, (argb >> 8) & 255, argb & 255)


def run():
    try:
        CUBE.RemUnsigned("AxisLabelColor")
        check("the Dark theme applies", FreeCADGui.applyTheme("Dark"))
        settle(1500)
        dark = theme_colour()
        check("the Dark theme states a light axis label colour",
              dark != SENTINEL and lightness(dark) >= 180, hex(dark))
        check("the Light theme applies", FreeCADGui.applyTheme("Light"))
        settle(1500)
        light = theme_colour()
        check("the Light theme states a dark axis label colour",
              light != SENTINEL and lightness(light) <= 80, hex(light))
        # by hand
        CUBE.SetUnsigned("AxisLabelColor", 0xFFFF0000)
        FreeCADGui.applyTheme("Dark")
        settle(1500)
        check("a colour set by hand is kept when a theme is applied",
              CUBE.GetUnsigned("AxisLabelColor", SENTINEL) == 0xFFFF0000,
              hex(CUBE.GetUnsigned("AxisLabelColor", SENTINEL)))
        check("and the theme's own is written beside it", theme_colour() == dark,
              hex(theme_colour()))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        CUBE.RemUnsigned("AxisLabelColor")
        note("DONE")
        QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(3000, run)
