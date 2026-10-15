"""The icons of TechDraw's Extend Dimensions tool bar draw something.

docs/HandsOnQueue.md entry 128, the reporter 2026-10-14 about 17:27:
"Basically the entire Techdraw Extend Dimensions toolbar has invisible
icons". Fourteen icons wrapped their drawing in an Inkscape filter blending
with BackgroundImage, which Qt 6.11's QtSvg applies and draws as nothing.

Claims: each of the fourteen, rendered by the icons TechDraw's commands
use at 32 by 32, has a fair number of pixels painted; and every command of
the Extend Dimensions tool bar's groups has an icon that paints.

Runs with QT_QPA_PLATFORM=offscreen.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
ICONS = ["TechDraw_ExtensionCascadeHorizDimension", "TechDraw_ExtensionCascadeObliqueDimension",
         "TechDraw_ExtensionCascadeVertDimension", "TechDraw_ExtensionCreateHorizChainDimension",
         "TechDraw_ExtensionCreateVertChainDimension",
         "TechDraw_ExtensionCreateObliqueChainDimension",
         "TechDraw_ExtensionCreateHorizChamferDimension",
         "TechDraw_ExtensionCreateVertChamferDimension",
         "TechDraw_ExtensionCreateHorizCoordDimension",
         "TechDraw_ExtensionCreateVertCoordDimension",
         "TechDraw_ExtensionCreateObliqueCoordDimension", "TechDraw_ExtensionCreateLengthArc",
         "TechDraw_ExtensionInsertDiameter", "TechDraw_ExtensionInsertSquare"]


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def painted(icon):
    pix = icon.pixmap(32, 32)
    img = pix.toImage().convertToFormat(QtGui.QImage.Format_ARGB32)
    return sum(1 for y in range(img.height()) for x in range(img.width())
               if (img.pixel(x, y) >> 24) & 255 > 40)


def run():
    try:
        FreeCADGui.activateWorkbench("TechDrawWorkbench")
        loop = QtCore.QEventLoop()
        QtCore.QTimer.singleShot(1000, loop.quit)
        loop.exec()
        sheet = QtGui.QImage(32 * len(ICONS), 32, QtGui.QImage.Format_ARGB32)
        sheet.fill(QtCore.Qt.white)
        painter = QtGui.QPainter(sheet)
        for i, name in enumerate(ICONS):
            icon = QtGui.QIcon(":/icons/%s.svg" % name)
            n = painted(icon)
            painter.drawPixmap(32 * i, 0, icon.pixmap(32, 32))
            check("%s paints" % name, n > 30, "%d pixels" % n)
        painter.end()
        sheet.save(os.path.join(OUT, "icons.png"))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(2500, run)
