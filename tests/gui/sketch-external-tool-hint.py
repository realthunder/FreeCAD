"""The External geometry tool says what it waits for.

Of the edit tools that got a status bar hint upstream (`94d39087d3`: carbon
copy, extend, external, fillet, split, trim), the External tool was the one
without it here: its handler is the fork's own, and the hint was never
written for it. Starting any of its flavours left the hint bar blank.

The hint read here is the bar's rendered text (see sketch-constraint-hints.py).

Claims:

  - each of External, Defining, Intersection and Intersection Defining
    shows a hint that asks for a pick, and names what the flavour makes of
    it;
  - leaving the tool takes the hint away.

Scored against the tree before the change: see the commit message.
"""
import os
import re
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchExternalToolHint"
V = FreeCAD.Vector


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


def hint_widget():
    mw = FreeCADGui.getMainWindow()
    for label in mw.findChildren(QtWidgets.QLabel):
        if label.metaObject().className() == "Gui::InputHintWidget":
            return label
    return None


def hints():
    widget = hint_widget()
    if widget is None:
        return None
    html = widget.text()
    if not html:
        return []
    cells = re.findall(r"<td valign=bottom>(.*?)</td>", html, re.S)
    return [re.sub(r"<[^>]+>", "", cell).strip() for cell in cells]


def escape():
    """Leave the running tool: a synthetic Escape reaches the viewer."""
    view = FreeCADGui.ActiveDocument.ActiveView
    viewer = None
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        name = w.metaObject().className()
        if "View3DInventorViewer" in name or "Quarter" in name:
            viewer = w
            break
    target = viewer if viewer is not None else view
    for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        QtWidgets.QApplication.sendEvent(
            target, QtGui.QKeyEvent(kind, QtCore.Qt.Key_Escape, QtCore.Qt.NoModifier))
    settle()


def run():
    try:
        import Part
        import SketcherGui  # noqa: F401  -- registers the commands

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/General").SetBool(
            "EnableEscape", False)
        doc = FreeCAD.newDocument(DOC)
        doc.addObject("Part::Box", "Box")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(10, 10, 0), V(30, 10, 0)), False)
        doc.recompute()
        FreeCADGui.ActiveDocument.setEdit(sk, 0)
        settle()
        check("the hint bar is there to read", hint_widget() is not None)

        wanted = [
            ("Sketcher_External", "projection"),
            ("Sketcher_Defining", "defining"),
            ("Sketcher_Intersection", "intersection"),
            ("Sketcher_IntersectionDefining", "defining"),
        ]
        for command, word in wanted:
            FreeCADGui.Selection.clearSelection()
            FreeCADGui.runCommand(command, 0)
            settle()
            current = hints() or []
            first = current[0] if current else ""
            check("%s asks for a pick" % command, first.startswith("pick"), current)
            check("%s names what it makes: %s" % (command, word),
                  word in " ".join(current).lower(), current)
            escape()
            check("leaving %s takes the hint away" % command, not hints(), hints())

        FreeCADGui.Selection.clearSelection()
        FreeCADGui.ActiveDocument.resetEdit()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())

    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
