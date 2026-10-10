"""The status bar's view dimension says a shared unit once.

The dimension of the 3D view is shown at the right of the status bar as
width x height. Each side was translated to the user's unit schema with
its unit, "100 mm x 80 mm"; asked for by hand: "100 x 80 mm".

Claims, on an orthographic view whose height is set here:

  - with both sides in one unit, the unit appears once, at the end;
  - both numbers are still there, and they are the view's;
  - with the sides in different units (1.x m wide, 700 mm high) each
    keeps its own.

Scored against the tree before the change: see the commit message.
"""
import os
import re
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
        label = mw.findChild(QtWidgets.QWidget, "sizeLabel")
        if label is None:
            raise RuntimeError("no dimension label in the status bar")
        # the standard schema: mm below ten metres, m above
        FreeCAD.Units.setSchema(0)

        doc = FreeCAD.newDocument("Dimension")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        view.setCameraType("Orthographic")
        camera = view.getCameraNode()

        def text_at(height):
            camera.height.setValue(height)
            view.redraw()
            settle(0.6)
            return label.text()

        text = text_at(80.0)
        match = re.fullmatch(r"([0-9.,]+) x ([0-9.,]+) (\S+)", text)
        check("with both sides in one unit, the unit appears once, at the end",
              match is not None and match.group(3) == "mm", repr(text))
        if match:
            width = float(match.group(1).replace(",", "."))
            height = float(match.group(2).replace(",", "."))
            size = view.getSize()
            aspect = float(size[0]) / float(size[1])
            check("both numbers are still there, and they are the view's",
                  abs(height - 80.0) < 0.01 and abs(width - 80.0 * aspect) < 0.5,
                  (width, height, aspect))

        text = text_at(7000.0)
        match = re.fullmatch(r"([0-9.,]+) (\S+) x ([0-9.,]+) (\S+)", text)
        check("with the sides in different units each keeps its own",
              match is not None and match.group(2) == "m" and match.group(4) == "mm",
              repr(text))
    except Exception:
        note("ABORT:\n" + traceback.format_exc())

    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
