"""Leaving a PartDesign feature's edit keeps the change and shows it.

Found 2026-10-04 in a browser: a pattern instance toggled off from the
served view, then the edit left (the `resetEdit` op), left the pattern
with the instance off in SuppressedIndices -- committed, on the undo
stack -- and its old shape: all instances still drawn, the feature
Touched. The fork's preview-on-edit (PartParams PreviewOnEdit, the
panel's preview check box) pauses a PartDesign feature's own recompute
while its panel is open, where a desktop view exists; the panel's OK
recomputes and its Cancel rolls the change back, but a bare resetEdit
-- Esc in a 3D view, a served client leaving the edit, a script -- did
neither. Un-pausing only touched the feature. Now the edit monitor
makes the feature when the edit ends any other way than the dialog's
own OK or Cancel, which is upstream's end state (it never pauses).

Claims, on a linear pattern of a 10 mm box, 2 occurrences, edited in a
desktop view with the preview on:
  - while the panel is open a change does not reach the shape (the
    preview's pause -- else nothing below tests the fix);
  - a bare resetEdit leaves the change applied: one instance, volume
    1000, the pattern not touched, one undo entry for the edit;
  - the panel's OK gives the same, as it did before.

Scored against the tree before the fix: the second claim fails (volume
2000, Touched); the others pass.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Part").SetBool("PreviewOnEdit", True)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
    "AutoSaveEnabled", False)


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


def build(name):
    doc = FreeCAD.newDocument(name)
    body = doc.addObject("PartDesign::Body", "Body")
    box = body.newObject("PartDesign::AdditiveBox", "Box")
    box.Length = box.Width = box.Height = 10
    pattern = body.newObject("PartDesign::LinearPattern", "LinearPattern")
    pattern.Originals = [box]
    pattern.Direction = (doc.getObject("X_Axis"), [""])
    pattern.Length = 100
    pattern.Occurrences = 2
    doc.recompute()
    wait(0.5)
    return doc, pattern


def edit_and_suppress(doc, pattern):
    """What the panel does for a marker click: a property change in the
    panel's transaction, then the feature's own recompute."""
    FreeCADGui.getDocument(doc.Name).setEdit(pattern, 0)
    wait(1)
    doc.openTransaction("Edit LinearPattern")
    pattern.SuppressedIndices = [1]
    pattern.recompute(True)
    wait(0.5)


def press_ok():
    for box in FreeCADGui.getMainWindow().findChildren(QtWidgets.QDialogButtonBox):
        parent = box.parent()
        if parent and "TaskEditControl" in parent.metaObject().className():
            button = box.button(QtWidgets.QDialogButtonBox.Ok)
            if button:
                button.click()
                return True
    return False


def state(pattern):
    return (round(pattern.Shape.Volume), "Touched" in pattern.State)


def run():
    doc, pattern = build("LeaveByReset")
    edit_and_suppress(doc, pattern)
    check("the preview holds the change back while the panel is open",
          state(pattern)[0] == 2000, state(pattern))
    FreeCADGui.getDocument(doc.Name).resetEdit()
    wait(1)
    check("a bare resetEdit leaves the change applied",
          state(pattern) == (1000, False), "volume, touched: %s" % (state(pattern),))
    check("as one undo entry", doc.UndoNames == ["Edit LinearPattern"], doc.UndoNames)
    FreeCAD.closeDocument(doc.Name)
    wait(0.5)

    doc, pattern = build("LeaveByOk")
    edit_and_suppress(doc, pattern)
    pressed = press_ok()
    wait(1)
    check("the panel's OK leaves the change applied",
          pressed and state(pattern) == (1000, False),
          "pressed %s, volume, touched: %s" % (pressed, state(pattern)))
    FreeCAD.closeDocument(doc.Name)
    wait(0.5)


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
