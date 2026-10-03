"""Every drawing command's icon follows the construction mode.

With construction mode on, a drawing tool's button shows the construction
variant of its icon (`..._Constr`). The commands are told of the mode through
a list they are entered in by name. Upstream found the two "B-spline by
knots" commands missing from its list (`581dee4d48`, issue 13181), and they
are missing from this one, as is the line group, `Sketcher_CompLine`.

Here that costs nothing: a group command hands the mode on to its members
(Gui::Command::updateAction), and each of the three is reached through a
group that is on the list, or is a group of members that are. This test is
the measurement of that, kept so that it stays true -- it passed before the
commit that added it, which only renamed the periodic command's icon to
upstream's name.

Claims, the mode switched by Toggle Construction with nothing selected:

  - each of these commands has an icon;
  - it differs between the two modes, as the line command's does (the
    control);
  - switching back gives the first icon again.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchConstructionIcons"
V = FreeCAD.Vector

COMMANDS = [
    ("Sketcher_CreateLine", "the line command (control)"),
    ("Sketcher_CreateBSplineByInterpolation", "B-spline by knots"),
    ("Sketcher_CreatePeriodicBSplineByInterpolation", "periodic B-spline by knots"),
    ("Sketcher_CompLine", "the line group"),
]


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle():
    for _ in range(5):
        QtWidgets.QApplication.processEvents()


def icon_of(command):
    """The command's icon as pixels, or None with no action or no icon."""
    cmd = FreeCADGui.Command.get(command)
    if cmd is None:
        return None
    actions = cmd.getAction()
    if not actions:
        return None
    icon = actions[0].icon()
    if icon.isNull():
        return None
    image = icon.pixmap(32, 32).toImage()
    return bytes(image.constBits())[: image.sizeInBytes()]


def run():
    try:
        import Part
        import SketcherGui  # noqa: F401  -- registers the commands

        FreeCADGui.activateWorkbench("SketcherWorkbench")
        settle()
        doc = FreeCAD.newDocument(DOC)
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(10, 10, 0), V(30, 10, 0)), False)
        doc.recompute()
        FreeCADGui.ActiveDocument.setEdit(sk, 0)
        settle()
        FreeCADGui.Selection.clearSelection()
        settle()

        normal = {name: icon_of(name) for name, _ in COMMANDS}
        for name, what in COMMANDS:
            check("%s has an icon" % what, normal[name] is not None)

        FreeCADGui.runCommand("Sketcher_ToggleConstruction", 0)
        settle()
        construction = {name: icon_of(name) for name, _ in COMMANDS}
        for name, what in COMMANDS:
            check("%s shows another icon in construction mode" % what,
                  construction[name] is not None and construction[name] != normal[name])

        FreeCADGui.runCommand("Sketcher_ToggleConstruction", 0)
        settle()
        for name, what in COMMANDS:
            check("%s shows the first icon again" % what, icon_of(name) == normal[name])

        FreeCADGui.ActiveDocument.resetEdit()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())

    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
