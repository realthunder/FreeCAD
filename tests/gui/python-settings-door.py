"""The settings a module's Python code reads are listed by the omni search.

Entry 41 of docs/HandsOnQueue.md, its first step. A setting C++ reads is
listed because a generated class describes it to App::ParamRegistry when its
library loads. A module written in Python has no such class, and what its
Python code reads was in no list. There is a way in from Python now --
FreeCAD.registerParam(), and freecad.params for a definition file in the
form the generated classes use -- and Assembly is the first module through
it: the thirteen settings of Preferences/Mod/Assembly that only its
commands and dialogues read (AssemblyPyParams.py, imported by its Init.py).

Claims:

  - "/param ground first part" lists Mod/Assembly/GroundFirstPart, with the
    Assembly module not loaded: a definition file is read at start;
  - "/param exploded view line" lists Mod/Assembly/StepLineColor and
    Mod/Assembly/StepLineThickness, neither of which is on any page;
  - "/param rigid sub-assemblies" lists Mod/Assembly/InsertRigidSubAssemblies,
    which a dialogue stores when it closes;
  - a setting registered while the session runs is listed by the next query;
  - with the module loaded, the settings its generated class describes are
    listed beside them, each once: "/param Mod/Assembly/" has the thirteen
    and the seven.

Scored against the tree before the change: see the commit message.
"""
import os
import sys
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
Qt = QtCore.Qt
ASSEMBLY = "Preferences/Mod/Assembly/"
PYTHON_READ = (
    "GroundFirstPart", "EnforceOneAssemblyRule", "SolveInJointCreation", "AssemblyConstraints",
    "StepLineThickness", "StepLineColor", "BOMOnlyParts", "BOMDetailParts",
    "BOMDetailSubAssemblies", "PartsAsSingleSolid", "InsertShowOnlyParts",
    "InsertRigidSubAssemblies", "PartInNewFile",
)
CPP_READ = (
    "SolveOnRecompute", "SolveOnMove", "LeaveEditWithEscape", "SwitchToWB",
    "JointHighlightColor", "LogSolverDebug", "BomMirroredSuffix",
)


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
    try:
        loaded = "AssemblyApp" in sys.modules
        rows = param_rows("ground first part")
        check("the omni search lists Mod/Assembly/GroundFirstPart, the module not loaded",
              not loaded and ASSEMBLY + "GroundFirstPart" in rows, (loaded, rows[:5]))
        rows = param_rows("exploded view line")
        check("and the colour and the width of an exploded view's lines, which no page has",
              ASSEMBLY + "StepLineColor" in rows and ASSEMBLY + "StepLineThickness" in rows, rows[:5])
        rows = param_rows("rigid sub-assemblies")
        check("and what the Insert dialogue was last left with",
              ASSEMBLY + "InsertRigidSubAssemblies" in rows, rows[:5])

        call = getattr(FreeCAD, "registerParam", None)
        done = call is not None and call(
            "User parameter:BaseApp/Preferences/Mod/Test", "DoorTestLateSetting", "Bool", True,
            title="A late setting", doc="Registered by a test while the session runs.")
        rows = param_rows("door test late setting")
        check("a setting registered while the session runs is listed by the next query",
              done and "Preferences/Mod/Test/DoorTestLateSetting" in rows, (done, rows[:5]))

        import AssemblyApp  # noqa: F401 -- its generated class describes what C++ reads
        settle(0.5)
        rows = [r for r in param_rows("Mod/Assembly/") if r.startswith(ASSEMBLY)]
        want = sorted(ASSEMBLY + n for n in PYTHON_READ + CPP_READ)
        check("with the module loaded its twenty settings are listed, each once",
              sorted(rows) == want, sorted(set(want) ^ set(rows)) or len(rows))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
