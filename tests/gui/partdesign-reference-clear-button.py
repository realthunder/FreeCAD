"""The button that clears a reference row of a PartDesign panel is as high as
its row, and as wide as its glyph makes it at that height.

docs/HandsOnQueue.md entry 91: "part design pocket edit panel, but button to
'temporary clear link references for new selection' got streched. it
shouldn't. this widget layout is reused in dozons of partdesign editing
panels. so should fix for all of them." Then, on the first fix, which only
told the button not to stretch: "regression, the 'Temporary clear links...'
button width in partdesign edit panel is too small now" and "fit the width
by its icon aspect ratio based on row height".

The row is one widget, used by every panel that has a reference to pick: a
caption button, the table of what is picked, and a push button with an icon
and no text. The theme's sheet gives every push button a least width of 80,
for captions: that was the stretch. The row's height is its caption button's.

Claims, with a Pad's panel open, under the Dark theme and under the Light:
  - the panel has such a row, its clear button shown;
  - the clear button is as high as the row's caption button;
  - its glyph fills that height, a small margin apart: the icon is drawn
    no less than 8 lower than the button is high;
  - the button is as wide as the glyph and its margin: no narrower than
    three quarters of its height, no wider than twice its height -- not the
    80 of a caption button;
  - the row is no higher than its caption button: the clear button does not
    make the row taller.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
import Part
import Sketcher
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
STEPS = []
V = FreeCAD.Vector


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=300):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def make():
    doc = FreeCAD.newDocument("ClearButton")
    body = doc.addObject("PartDesign::Body", "Body")
    sketch = body.newObject("Sketcher::SketchObject", "Sketch")
    corners = [V(0, 0, 0), V(10, 0, 0), V(10, 10, 0), V(0, 10, 0)]
    for i in range(4):
        sketch.addGeometry(Part.LineSegment(corners[i], corners[(i + 1) % 4]), False)
    for i in range(4):
        sketch.addConstraint(Sketcher.Constraint("Coincident", i, 2, (i + 1) % 4, 1))
    doc.recompute()
    pad = body.newObject("PartDesign::Pad", "Pad")
    pad.Profile = sketch
    pad.Length = 5
    doc.recompute()
    settle(800)


def look(theme):
    if not check("%s applies" % theme, FreeCADGui.applyTheme(theme)):
        return
    settle(4000)
    FreeCADGui.getDocument("ClearButton").setEdit("Pad")
    settle(2000)
    found = []
    for button in FreeCADGui.getMainWindow().findChildren(QtWidgets.QPushButton):
        if button.isVisible() and button.toolTip().startswith("Temporary clear link references"):
            found.append(button)
    if check("%s: the Pad's panel has a reference row with its clear button" % theme,
             bool(found), len(found)):
        for button in found:
            row = button.parentWidget()
            caption = [b for b in row.findChildren(QtWidgets.QPushButton)
                       if b is not button and b.parentWidget() is row][0]
            w, h = button.width(), button.height()
            sizes = "clear %dx%d, icon %dx%d, caption %dx%d, row %d high" % (
                w, h, button.iconSize().width(), button.iconSize().height(),
                caption.width(), caption.height(), row.height())
            check("%s: the clear button is as high as the row's caption button" % theme,
                  h == caption.height(), sizes)
            check("%s: its glyph fills that height" % theme,
                  button.iconSize().height() >= h - 8, sizes)
            check("%s: it is as wide as its glyph, not as a caption button" % theme,
                  0.75 * h <= w <= 2 * h, sizes)
            check("%s: the row is no higher than its caption button" % theme,
                  row.height() == caption.height(), sizes)
            row.grab().save(os.path.join(OUT, "row-%s.png" % theme))
    FreeCADGui.getMainWindow().grab().save(os.path.join(OUT, "panel-%s.png" % theme))
    FreeCADGui.Control.closeDialog()
    FreeCADGui.getDocument("ClearButton").resetEdit()
    settle(800)


def look_dark():
    look("Dark")


def look_light():
    look("Light")


def finish():
    try:
        FreeCADGui.Control.closeDialog()
    except Exception:
        pass
    try:
        gdoc = FreeCADGui.getDocument("ClearButton")
        if gdoc is not None:
            gdoc.resetEdit()
    except Exception:
        pass
    settle(500)
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


def advance():
    if not STEPS:
        finish()
        return
    step = STEPS.pop(0)
    try:
        step()
    except Exception:
        note("ABORT in %s: %s" % (step.__name__, traceback.format_exc().replace("\n", " | ")))
        STEPS[:] = []
    QtCore.QTimer.singleShot(800, advance)


STEPS[:] = [make, look_dark, look_light]
QtCore.QTimer.singleShot(2500, advance)
