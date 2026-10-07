"""The settings C++ reads in the smaller modules are listed, also of a module loaded late.

Entry 24 of docs/HandsOnQueue.md, the small modules. Assembly, Points and
Fem got a generated class each (Assembly::AssemblyParams,
Points::PointsParams, Fem::FemParams); Mesh and Spreadsheet got three
settings more in theirs. Most of these have no preference page at all: the
omni search is the first place they can be set from. Fem's class holds only
what C++ reads; the many settings its Python code reads are not listed yet.

Two of them have a default that needs escaping in C++ -- the quote and the
escape character of a spreadsheet's text files -- and a third is no ASCII:
TechDraw's diameter sign. The generator wrote a string default as it stood;
it escapes it now.

Start and CAM followed, with Start::StartParams and Path::CAMParams. The
Start page has no preference page at all. In CAM the cycle time estimate
read "WarningsSuppressAllSpeeds" where the Advanced page stores
"WarningSuppressAllSpeeds": its warning of missing feed rates was
suppressed whatever the page said.

Material followed, with Materials::MaterialParams: 22 settings, kept in
six sub-groups, of which what the editor and the selector show are the same
five keys twice.

Writing this test found that the omni search listed the settings of the
modules loaded when its box was first used, and of no module loaded
afterwards: its list was a copy made once. Each claim below loads a module
and then asks, so all but the first failed for that reason alone.

Claims:

  - "/param solve while dragging" lists Mod/Assembly/SolveOnMove;
  - "/param minimum e57 point distance" lists Mod/Points/E57/MinDistance;
  - "/param export inp group data" lists Mod/Fem/Abaqus/AbaqusWriteGroups;
  - "/param compress amf files" lists Mod/Mesh/ExportAmfCompressed;
  - "/param quote character" lists
    Mod/Spreadsheet/ImportExportQuoteCharacter;
  - "/param diameter symbol" lists Mod/TechDraw/Dimensions/DiameterSymbol;
  - "/param close start page after use" lists Mod/Start/closeStart;
  - "/param hide first rapid move" lists Mod/CAM/HideFirstRapid;
  - "/param use built-in materials" lists
    Mod/Material/Resources/UseBuiltInMaterials;
  - with the Advanced page's "suppress all speeds warning" stored off, the
    cycle time estimate of a path without feed rates warns in the report
    view; with nothing stored it does not.

That opening the preferences stores nothing with Fem loaded -- its VTK page
stored the export level each time it was shown -- is a claim of
preferences-cancel-asks-nothing.py.

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


CLAIMS = (
    ("AssemblyApp", "solve while dragging", "Mod/Assembly/SolveOnMove"),
    ("Points", "minimum e57 point distance", "Mod/Points/E57/MinDistance"),
    ("Fem", "export inp group data", "Mod/Fem/Abaqus/AbaqusWriteGroups"),
    ("Mesh", "compress amf files", "Mod/Mesh/ExportAmfCompressed"),
    ("Spreadsheet", "quote character", "Mod/Spreadsheet/ImportExportQuoteCharacter"),
    ("TechDraw", "diameter symbol", "Mod/TechDraw/Dimensions/DiameterSymbol"),
    ("Start", "close start page after use", "Mod/Start/closeStart"),
    ("PathApp", "hide first rapid move", "Mod/CAM/HideFirstRapid"),
    ("Materials", "use built-in materials", "Mod/Material/Resources/UseBuiltInMaterials"),
)
WARNING = "Feed Rate Error"


def report_text():
    text = ""
    for w in QtWidgets.QApplication.allWidgets():
        if w.metaObject().className() == "Gui::DockWnd::ReportOutput":
            text += w.toPlainText()
    return text


def speeds_warning():
    """Whether the cycle time estimate warns: with nothing stored, and with the page's switch stored off."""
    import PathApp

    cam = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/CAM")
    path = PathApp.Path([PathApp.Command("G1", {"X": 10.0})])
    try:
        path.getCycleTime(0.0, 0.0, 0.0, 0.0)
        settle(0.5)
        quiet = WARNING in report_text()
        cam.SetBool("WarningSuppressAllSpeeds", False)
        settle(0.2)
        path.getCycleTime(0.0, 0.0, 0.0, 0.0)
        settle(0.5)
        return quiet, WARNING in report_text()
    finally:
        cam.RemBool("WarningSuppressAllSpeeds")


def run():
    try:
        for module, query, key in CLAIMS:
            try:
                __import__(module)  # a module registers its settings when it is loaded
            except ImportError as e:
                check("the omni search lists " + key, False, "no module %s: %s" % (module, e))
                continue
            settle(0.5)
            rows = param_rows(query)
            check("the omni search lists " + key, any(r.endswith(key) for r in rows), rows[:5])
        quiet, warned = speeds_warning()
        check("the cycle time estimate warns of missing feed rates with the page's switch stored off, and not "
              "with nothing stored", quiet is False and warned is True, (quiet, warned))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
