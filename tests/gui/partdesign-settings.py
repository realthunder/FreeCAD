"""PartDesign's settings are listed, and new features and datums still take them.

The keys of Preferences/Mod/PartDesign are behind
PartDesign::PartDesignParams (docs/HandsOnQueue.md entry 24): refine after
a feature, the default profile type of a hole, the attachment panel for a
new sketch, what removing a body from a Boolean does, switching to the
workbench and its task panel, the colour of new datums and the look of new
local coordinate systems. Each is read when it is used. Nothing was found
wrong in this group; the behaviour claims hold the conversion to what the
readers did before.

Claims:

  - "/param refine model" lists PartDesign's refine switch, and "/param
    datum colour" the colour of new datums;
  - a Pad made with the refine switch on has Refine on, one made without
    the key has it off;
  - a datum plane made with a datum colour stored has that colour;
  - a Hole made with the default profile type set to 2 takes points only,
    one made without the key takes points, circles and arcs (the property
    is a set of bits: the first is a part of the second).

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
PREFS = "User parameter:BaseApp/Preferences/"
Qt = QtCore.Qt


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


def param_rows(query):
    FreeCADGui.runCommand("Std_OmniSearch")
    settle(0.6)
    edit = None
    for w in QtWidgets.QApplication.allWidgets():
        if w.objectName() == "OmniSearchEdit" and w.isVisible():
            edit = w
    if edit is None:
        return []
    text = "/param " + query
    edit.setText(text)
    edit.setCursorPosition(len(text))
    edit.textEdited.emit(text)
    settle(0.8)
    rows = []
    for w in QtWidgets.QApplication.topLevelWidgets():
        if isinstance(w, QtWidgets.QAbstractItemView) and w.isVisible() and w.model() is not None:
            rows += [str(w.model().index(i, 0).data()) for i in range(w.model().rowCount())]
    QtWidgets.QApplication.sendEvent(edit, QtGui.QKeyEvent(QtCore.QEvent.KeyPress, Qt.Key_Escape, Qt.NoModifier))
    settle(0.5)
    return rows


def run():
    group = FreeCAD.ParamGet(PREFS + "Mod/PartDesign")
    doc = None
    try:
        import PartDesign  # noqa: F401  the module registers its settings when it is loaded
        import PartDesignGui  # noqa: F401
        settle(0.5)

        rows = param_rows("refine model")
        check("the omni search lists PartDesign's refine switch",
              any(r.endswith("Mod/PartDesign/RefineModel") for r in rows), rows[:6])
        rows = param_rows("datum colour")
        check("and the colour of new datums", any(r.endswith("Mod/PartDesign/DefaultDatumColor") for r in rows),
              rows[:6])

        doc = FreeCAD.newDocument("Entry24PartDesign")
        settle(0.5)
        group.SetBool("RefineModel", True)
        settle(0.2)
        on = doc.addObject("PartDesign::Pad", "PadRefined").Refine
        group.RemBool("RefineModel")
        settle(0.2)
        off = doc.addObject("PartDesign::Pad", "PadPlain").Refine
        check("a Pad made with the refine switch on has Refine on, one made without the key has it off",
              on is True and off is False, (on, off))

        group.SetUnsigned("DefaultDatumColor", 0x00FF0000)
        settle(0.2)
        plane = doc.addObject("PartDesign::Plane", "DatumGreen")
        settle(0.3)
        colour = tuple(round(c, 3) for c in plane.ViewObject.ShapeColor[:3])
        group.RemUnsigned("DefaultDatumColor")
        check("a datum plane made with a datum colour stored has that colour", colour == (0.0, 1.0, 0.0), colour)

        group.SetInt("defaultBaseTypeHole", 2)
        settle(0.2)
        points = doc.addObject("PartDesign::Hole", "HolePoints").BaseProfileType
        group.RemInt("defaultBaseTypeHole")
        settle(0.2)
        default = doc.addObject("PartDesign::Hole", "HoleDefault").BaseProfileType
        check("a Hole made with the profile type set to 2 takes points only, without the key points, circles and arcs",
              points != default and (points & default) == points, (points, default))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        group.RemBool("RefineModel")
        group.RemUnsigned("DefaultDatumColor")
        group.RemInt("defaultBaseTypeHole")
        if doc is not None:
            FreeCAD.closeDocument(doc.Name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
