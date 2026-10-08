"""The navigation cube's settings are listed, and the views still follow the cube's switch.

The keys of Preferences/NaviCube are behind NaviCubeParams
(docs/HandsOnQueue.md entry 24): its size, turning, coordinate system,
border and chamfer, hiding, the two fonts and the eight colours. The cube
reads them all again a moment after any of them changes, and asks the class
now. Nothing was found wrong in this group; the page's defaults are held to
the definitions by preferences-ok-keeps-defaults.py.

Claims:

  - "/param navigation cube size" lists the cube's size, and "/param
    navigation cube face colour" the colour of its faces;
  - a 3D view shows the cube, hides it at once when ShowNaviCube of the
    View group is switched off, and shows it again when it is back;
  - the view survives a change of the cube's size, chamfer and font, and
    the settings are stored as they were set.

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
        if not isinstance(w, QtWidgets.QAbstractItemView) or w.model() is None:
            continue
        found = [str(w.model().index(i, 0).data()) for i in range(w.model().rowCount())]
        # The list is not shown while another application is in front, which a
        # test cannot prevent on a desktop in use; its rows are there all the same.
        if w.isVisible() or any(r.startswith("Preferences/") for r in found):
            rows += found
    QtWidgets.QApplication.sendEvent(edit, QtGui.QKeyEvent(QtCore.QEvent.KeyPress, Qt.Key_Escape, Qt.NoModifier))
    settle(0.5)
    return rows


def run():
    view_group = FreeCAD.ParamGet(PREFS + "View")
    cube = FreeCAD.ParamGet(PREFS + "NaviCube")
    doc = None
    try:
        rows = param_rows("navigation cube size")
        check("the omni search lists the navigation cube's size", any(r.endswith("NaviCube/CubeSize") for r in rows),
              rows[:6])
        rows = param_rows("navigation cube face colour")
        check("and the colour of its faces", any(r.endswith("NaviCube/FrontColor") for r in rows), rows[:6])

        doc = FreeCAD.newDocument("Entry24Cube")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        settle(1.0)
        viewer = FreeCADGui.ActiveDocument.ActiveView.getViewer()
        shown = viewer.isEnabledNaviCube()
        view_group.SetBool("ShowNaviCube", False)
        settle(0.3)
        hidden = not viewer.isEnabledNaviCube()
        view_group.RemBool("ShowNaviCube")
        settle(0.3)
        check("a view shows the cube, hides it at once when it is switched off, and shows it again",
              shown and hidden and viewer.isEnabledNaviCube(), (shown, hidden, viewer.isEnabledNaviCube()))

        cube.SetInt("CubeSize", 200)
        cube.SetFloat("ChamferSize", 0.2)
        cube.SetString("FontString", "Arial")
        settle(1.0)
        FreeCADGui.ActiveDocument.ActiveView.fitAll()
        settle(0.5)
        check("the view survives a change of the cube's size, chamfer and font",
              cube.GetInt("CubeSize", 0) == 200 and abs(cube.GetFloat("ChamferSize", 0) - 0.2) < 1e-9
              and viewer.isEnabledNaviCube())
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        view_group.RemBool("ShowNaviCube")
        cube.RemInt("CubeSize")
        cube.RemFloat("ChamferSize")
        cube.RemString("FontString")
        settle(0.5)
        if doc is not None:
            FreeCAD.closeDocument(doc.Name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
