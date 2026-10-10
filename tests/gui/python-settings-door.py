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
  - "/param matrix solver" lists Mod/Fem/Ccx/Solver, "/param default post
    processor" Mod/CAM/PostProcessorDefault and "/param proxy address"
    Addons/ProxyUrl, with neither Fem's nor CAM's library loaded: Fem and
    CAM have a definition file each, the Addon Manager registers from its
    defaults file;
  - "/param translation suffix" lists Mod/Help/Suffix, "/param openscad
    executable" Mod/OpenSCAD/openscadexecutable, "/param b-spline fit
    degree" Mod/ReverseEngineering/BSplineFit/UDegree, "/param navigation
    indicator compact" /Tux/NavigationIndicator/Compact -- which is not
    under Preferences -- and "/param delete card duplicates"
    Mod/Material/Cards/DeleteDuplicates: the small modules have a
    definition file each;
  - a setting registered while the session runs is listed by the next query;
  - with the module loaded, the settings its generated class describes are
    listed beside them, each once: "/param Mod/Assembly/" has the thirteen
    and the seven.

Draft and BIM keep a table of their settings already (draftutils.params),
part of it read from their preference pages; loading it describes them,
with a title, a documentation and the page's editor each:

  - with Draft's table loaded, "/param snap range" lists Mod/Draft/snapRange
    and "/param wall width" Mod/Arch/WallWidth, neither on any page, and
    "/param default working plane" Mod/Draft/defaultWP, which is on one;
  - loading the table reported nothing it could not describe.

A setting has one description. Part's and PartGui's generated classes share
four definitions (the tessellation settings both read), and the omni search
listed each of the four twice:

  - with both loaded, "/param Mod/Part/MeshDeviation" lists it once, and the
    registry holds no path and entry twice.

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
        if w.isVisible() or any(r.startswith(("Preferences/", "/Tux/")) for r in found):
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

        found = [param_rows(q) for q in ("matrix solver", "default post processor", "proxy address")]
        check("and Fem's, CAM's and the Addon Manager's, at the start",
              "Preferences/Mod/Fem/Ccx/Solver" in found[0]
              and "Preferences/Mod/CAM/PostProcessorDefault" in found[1]
              and "Preferences/Addons/ProxyUrl" in found[2]
              and not {"Fem", "PathApp"} & set(sys.modules),
              ([f[:2] for f in found], sorted({"Fem", "PathApp"} & set(sys.modules))))

        found = [param_rows(q) for q in ("translation suffix", "openscad executable",
                                         "b-spline fit degree", "navigation indicator compact",
                                         "delete card duplicates")]
        check("and Help's, OpenSCAD's, ReverseEngineering's, Tux's and Material's card list's",
              "Preferences/Mod/Help/Suffix" in found[0]
              and "Preferences/Mod/OpenSCAD/openscadexecutable" in found[1]
              and "Preferences/Mod/ReverseEngineering/BSplineFit/UDegree" in found[2]
              and "/Tux/NavigationIndicator/Compact" in found[3]
              and "Preferences/Mod/Material/Cards/DeleteDuplicates" in found[4],
              [f[:2] for f in found])

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

        from draftutils import params  # noqa: F401 -- Draft's and BIM's table
        settle(0.5)
        found = [param_rows(q) for q in ("snap range", "wall width", "default working plane")]
        check("with Draft's table loaded its settings are listed, on a page or not",
              "Preferences/Mod/Draft/snapRange" in found[0]
              and "Preferences/Mod/Arch/WallWidth" in found[1]
              and "Preferences/Mod/Draft/defaultWP" in found[2], [f[:3] for f in found])
        report = ""
        for w in QtWidgets.QApplication.allWidgets():
            if w.metaObject().className() == "Gui::DockWnd::ReportOutput":
                report += w.toPlainText()
        check("and loading it reported nothing it could not describe",
              "is not described" not in report and "is described already" not in report,
              [line for line in report.splitlines() if "described" in line][:3])

        import Part  # noqa: F401
        import PartGui  # noqa: F401 -- its class shares four definitions with Part's
        settle(0.5)
        rows = [r for r in param_rows("Mod/Part/MeshDeviation") if r.endswith("/MeshDeviation")]
        seen = {}
        for r in (FreeCAD.listParams() if hasattr(FreeCAD, "listParams") else []):
            seen[(r["path"], r["entry"])] = seen.get((r["path"], r["entry"]), 0) + 1
        twice = sorted(k[1] for k, n in seen.items() if n > 1)
        check("a setting two classes describe is listed once",
              rows == ["Preferences/Mod/Part/MeshDeviation"] and seen and not twice,
              (rows, twice[:6]))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
