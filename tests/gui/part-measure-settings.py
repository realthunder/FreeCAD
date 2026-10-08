"""Part's measurements follow their settings at once, and the settings are listed.

The colours and the font of the dimensions that Part's measure commands put
into the 3D view were keys read from the parameter group each time a
dimension was built, so a change showed on the next measurement or after
"Refresh" on the preference page. They are behind PartGui's PartParams now
(docs/HandsOnQueue.md entry 24): listed by the omni search, one default
each, and a change rebuilds the measurements on screen.

Claims, with a distance measured between two boxes:

  - the measurement is in the scene, its direct distance drawn in the
    default colour, red;
  - "/param measurement colour" lists the setting;
  - with another colour stored (as the preference page stores it) the
    measurement on screen has it, with nothing else done;
  - likewise the colour of the three delta lines.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
GROUP = "User parameter:BaseApp/Preferences/Mod/Part"
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


def dimension_colours(view):
    """The colours of the linear dimensions in the view's scene, as #rrggbb, in scene order."""
    root = view.getViewer().getSoRenderManager().getSceneGraph()
    search = coin.SoSearchAction()
    search.setType(coin.SoType.fromName("DimensionLinear"))
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(root)
    found = []
    paths = search.getPaths()
    for i in range(paths.getLength()):
        field = paths[i].getTail().getField("dColor")
        r, g, b = field.getValue().getValue()
        found.append("#%02x%02x%02x" % (int(round(r * 255)), int(round(g * 255)), int(round(b * 255))))
    return found


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
    group = FreeCAD.ParamGet(GROUP)
    doc = None
    try:
        import PartGui  # noqa: F401
        doc = FreeCAD.newDocument("MeasureSettings")
        a = doc.addObject("Part::Box", "Box")
        b = doc.addObject("Part::Box", "Box001")
        b.Placement.Base = FreeCAD.Vector(40, 25, 15)
        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        view.viewIsometric()
        view.fitAll()
        settle(2.0)
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(a, "Vertex1")
        FreeCADGui.Selection.addSelection(b, "Vertex1")
        settle(0.3)
        FreeCADGui.runCommand("Part_Measure_Linear")
        settle(1.5)
        colours = dimension_colours(view)
        if not check("the measurement is in the scene", len(colours) == 4, colours):
            return
        check("its direct distance is drawn red", colours[0] == "#ff0000", colours)

        rows = param_rows("measurement colour")
        check("the omni search lists the setting", any(r.endswith("Mod/Part/Dimensions3dColor") for r in rows), rows[:6])

        group.SetUnsigned("Dimensions3dColor", 0x00aa55ff)
        settle(1.5)
        colours = dimension_colours(view)
        check("with another colour stored the measurement on screen has it",
              len(colours) == 4 and colours[0] == "#00aa55", colours)
        group.SetUnsigned("DimensionsDeltaColor", 0x5500aaff)
        settle(1.5)
        colours = dimension_colours(view)
        check("and the delta lines theirs", len(colours) == 4 and colours[1:] == ["#5500aa"] * 3, colours)
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        for name in ("Dimensions3dColor", "DimensionsDeltaColor"):
            group.RemUnsigned(name)
        try:
            if doc is not None:
                FreeCAD.closeDocument(doc.Name)
        except Exception:
            pass
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
