"""The settings and the state C++ read without a definition are listed by the omni search.

Entry 42 of docs/HandsOnQueue.md. After entry 24 about 300 keys that C++ reads
still had no definition: what the program keeps for itself (state), and
settings that entry had not reached. They go behind generated classes group
by group, and a claim is added here for each.

The 3D mouse, Gui::SpaceballParams. Its group is BaseApp/Spaceball/Motion,
not under Preferences, and its page -- Customize, Spaceball Motion -- only
shows its widgets while a device is present, so on most machines these
settings could not be seen at all:

  - "/param 3d mouse dominant" lists Spaceball/Motion/Dominant, and
    "/param spin sensitivity" Spaceball/Motion/SpinSensitivity;
  - "/param axis remapping" lists Spaceball/Motion/Remapping, which no page
    has, and "/param device model" Spaceball/Model, of the group above;
  - the calibration the program stores is listed too:
    Spaceball/Motion/CalibrationXr;
  - all 32 are in the registry once, each with a title and a documentation.

The expression sandbox, App::SandboxParams (Preferences/Expression/Sandbox
and Security). Only its switch could be reached from the interface:

  - "/param sandbox time budget" lists Expression/Sandbox/BudgetMs and
    "/param enforce document permissions" Expression/Security/Enforce;
  - the class follows its group at once: with Security/Enforce stored off,
    FreeCAD.ExpressionSecurity.enforced() is False, and True again with the
    key removed -- the reader asks the class now, where it read the group;
  - all 14 are in the registry once, each with a title and a documentation.

Small groups, in Gui::MiscParams, MainWindowParams and ViewParams -- 30
keys: the rest of the gizmos' group, the property view's settings and what
it and the Add Property dialog keep, the panel mirror, the selection view,
the DAG view, the custom orientation of a new document's view, and two
single ones:

  - "/param fine snap modifier" lists Gui/Gizmos/FineSnapModifier, which a
    page of Part shows, and "/param property view expand data"
    PropertyView/AutoExpandData, which nothing showed;
  - what the program keeps is listed too: "/param add property last group"
    lists PropertyView/NewPropertyGroup;
  - the property view follows its settings: with HideHeader stored on, the
    header of its Data tab is hidden a moment later, and back with the key
    removed;
  - "/param crosshair cursor colour" lists View/CursorCrosshairColor and
    "/param clear the menu bar" MainWindow/ClearMenuBar;
  - none of the 30 is in the registry twice, each has a title and a
    documentation.

What the program keeps for itself in Gui's groups, and the last of Gui's
settings without a definition -- 105 keys, in GeneralParams,
MainWindowParams, ViewParams, MacroParams, App's DocumentParams and
MiscParams. Their readers are left as they are; the keys are described:

  - "/param last folder of the file dialogs" lists
    General/FileOpenSavePath, and "/param main window layout"
    MainWindow/MainWindowState;
  - the four overlay panels' keys, 13 each, are listed from their groups
    outside Preferences: "/param overlay left dock windows" lists
    MainWindow/DockWindows/OverlayLeft/Widgets;
  - "/param share port" lists SceneShare/Port;
  - what was to stay out is not in the registry: the share token, the
    recent files and macros themselves, the order and the switching off of
    the workbenches.

The modules -- 30 keys in the classes of Sketcher, Material, TechDraw,
Part, Mesh and Start, in Draft's table and in a definition file for
Inspection:

  - "/param visual inspection search distance" lists
    Mod/Inspection/Inspection/SearchDistance with the module not loaded;
  - with their libraries loaded, "/param sketch panel constraints open"
    lists Mod/Sketcher/ExpandedConstraintsWidget, "/param materials editor
    width" Mod/Material/Editor/EditorWidth, "/param welding symbol tile
    colour" Mod/TechDraw/Colors/TileColor, "/param most occurrences of a
    pattern" Mod/Part/MaximumPatternOccurrences, "/param mesh from shape
    last surface deviation" Mod/Mesh/Meshing/Standard/LinearDeflection and
    "/param start first start" Mod/Start/FirstStart2024;
  - with Draft's table loaded, "/param dxf export points" lists
    Mod/Draft/ExportPoints, which the C++ exporter reads from there since
    entry 44;
  - the registry still holds no path and entry twice.

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


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))


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
        if w.isVisible() or any("/" in r for r in found):
            rows += found
    QtWidgets.QApplication.sendEvent(
        edit, QtGui.QKeyEvent(QtCore.QEvent.KeyPress, Qt.Key_Escape, Qt.NoModifier)
    )
    settle(0.5)
    return rows


def described(context):
    rows = FreeCAD.listParams() if hasattr(FreeCAD, "listParams") else []
    return [r for r in rows if r["context"] == context]


def spaceball():
    found = [param_rows(q) for q in ("3d mouse dominant", "spin sensitivity")]
    check("the omni search lists the 3D mouse's settings, with no device present",
          "Spaceball/Motion/Dominant" in found[0]
          and "Spaceball/Motion/SpinSensitivity" in found[1], [f[:3] for f in found])
    found = [param_rows(q) for q in ("axis remapping", "device model")]
    check("and the two no page of it shows",
          "Spaceball/Motion/Remapping" in found[0] and "Spaceball/Model" in found[1],
          [f[:3] for f in found])
    rows = param_rows("calibration tilt")
    check("and the calibration the program stores",
          "Spaceball/Motion/CalibrationXr" in rows, rows[:3])
    rows = described("SpaceballParams")
    keys = {(r["path"], r["entry"]) for r in rows}
    bare = [r["entry"] for r in rows if not r["title"] or not 0 < len(r["doc"]) <= 400]
    check("its 32 are in the registry once, each with a title and a documentation",
          len(rows) == 32 and len(keys) == 32 and not bare, (len(rows), len(keys), bare[:4]))


def sandbox():
    found = [param_rows(q) for q in ("sandbox time budget", "enforce document permissions")]
    check("the omni search lists the expression sandbox's settings",
          "Preferences/Expression/Sandbox/BudgetMs" in found[0]
          and "Preferences/Expression/Security/Enforce" in found[1], [f[:3] for f in found])
    security = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Expression/Security")
    had = "Enforce" in security.GetBools()
    old = security.GetBool("Enforce", True)
    try:
        security.SetBool("Enforce", False)
        off = FreeCAD.ExpressionSecurity.enforced()
        security.RemBool("Enforce")
        back = FreeCAD.ExpressionSecurity.enforced()
    finally:
        if had:
            security.SetBool("Enforce", old)
    check("and the class follows its group at once", off is False and back is True, (off, back))
    rows = described("SandboxParams")
    keys = {(r["path"], r["entry"]) for r in rows}
    bare = [r["entry"] for r in rows if not r["title"] or not 0 < len(r["doc"]) <= 400]
    check("its 14 are in the registry once, each with a title and a documentation",
          len(rows) == 14 and len(keys) == 14 and not bare, (len(rows), len(keys), bare[:4]))


SMALL = {
    "Gui/Gizmos": ("EnableGizmos", "DelayedGizmoUpdate", "EnableCoarseSnap", "FineSnapModifier",
                   "DefaultCoarseDragBehavior"),
    "PropertyView": ("AutoTransactionView", "AutoTransactionData", "AutoExpandView",
                     "AutoExpandData", "HideHeader", "ViewSectionSize", "DataSectionSize",
                     "LastTabIndex", "NewPropertyType", "NewPropertyGroup", "NewPropertyAppend"),
    "Fw": ("PanelMirror", "PanelPollMs"),
    "Selection": ("AutoShowSelectionView", "singleClickFeatureSelect"),
    "DAGView": ("SelectionMode", "FontPointSize", "Direction"),
    "View/Custom": ("Q0", "Q1", "Q2", "Q3"),
    "DependencyGraph": ("GeoFeatureSubgraphs",),
    "MainWindow": ("ClearMenuBar",),
    "View": ("CursorCrosshairColor",),
}


def small_groups():
    found = [param_rows(q) for q in ("fine snap modifier", "property view expand data")]
    check("the omni search lists the gizmos' and the property view's settings",
          "Preferences/Gui/Gizmos/FineSnapModifier" in found[0]
          and "Preferences/PropertyView/AutoExpandData" in found[1], [f[:3] for f in found])
    rows = param_rows("add property last group")
    check("and what the Add Property dialog keeps",
          "Preferences/PropertyView/NewPropertyGroup" in rows, rows[:3])

    group = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/PropertyView")
    had = "HideHeader" in group.GetBools()
    old = group.GetBool("HideHeader", False)
    doc = FreeCAD.newDocument("Entry42PropertyView")
    try:
        doc.addObject("App::FeaturePython", "Thing")
        FreeCADGui.Selection.addSelection(doc.Name, "Thing")
        settle(1.0)
        editors = [w for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QTreeView)
                   if w.metaObject().className() == "Gui::PropertyEditor::PropertyEditor"]
        shown = [e.isHeaderHidden() for e in editors]
        group.SetBool("HideHeader", True)
        settle(1.0)
        hidden = [e.isHeaderHidden() for e in editors]
        group.RemBool("HideHeader")
        settle(1.0)
        back = [e.isHeaderHidden() for e in editors]
    finally:
        if had:
            group.SetBool("HideHeader", old)
        FreeCAD.closeDocument(doc.Name)
    check("the property view follows HideHeader a moment after it changes",
          bool(editors) and not any(shown) and all(hidden) and not any(back),
          (len(editors), shown, hidden, back))

    found = [param_rows(q) for q in ("crosshair cursor colour", "clear the menu bar")]
    check("and the two single ones are listed",
          "Preferences/View/CursorCrosshairColor" in found[0]
          and "Preferences/MainWindow/ClearMenuBar" in found[1], [f[:3] for f in found])

    count = {}
    bare = []
    for r in FreeCAD.listParams():
        for group_name, entries in SMALL.items():
            if r["path"].endswith("Preferences/" + group_name) and r["entry"] in entries:
                key = (group_name, r["entry"])
                count[key] = count.get(key, 0) + 1
                if not r["title"] or not 0 < len(r["doc"]) <= 400:
                    bare.append(r["entry"])
    want = sum(len(v) for v in SMALL.values())
    check("the 30 are in the registry once each, with a title and a documentation",
          want == 30 and len(count) == 30 and set(count.values()) == {1} and not bare,
          (want, len(count), sorted(k for k, n in count.items() if n != 1), bare[:4]))


def kept_by_the_program():
    found = [param_rows(q) for q in ("last folder of the file dialogs", "main window layout")]
    check("the omni search lists what the file dialogs and the main window keep",
          "Preferences/General/FileOpenSavePath" in found[0]
          and "Preferences/MainWindow/MainWindowState" in found[1], [f[:3] for f in found])
    rows = param_rows("overlay left dock windows")
    listed = FreeCAD.listParams()
    overlay = [r for r in listed
               if r["path"].startswith("User parameter:BaseApp/MainWindow/DockWindows/Overlay")]
    check("and the overlay panels' keys, 13 for each of the four",
          "MainWindow/DockWindows/OverlayLeft/Widgets" in rows and len(overlay) == 52
          and len({(r["path"], r["entry"]) for r in overlay}) == 52, (rows[:3], len(overlay)))
    rows = param_rows("share port")
    check("and the Share dialog's fields", "Preferences/SceneShare/Port" in rows, rows[:3])
    out = [r["path"].split("Preferences/")[-1] + "/" + r["entry"] for r in listed
           if (r["path"].endswith("/SceneShare") and r["entry"] == "Token")
           or r["entry"].startswith("MRU")
           or (r["path"].endswith("/Workbenches") and r["entry"] in ("Ordered", "Disabled"))]
    check("what was to stay out is not in the registry", bool(listed) and not out, out)
    bare = [r["entry"] for r in listed
            if r["context"] in ("MiscParams", "GeneralParams", "MainWindowParams", "MacroParams")
            and (not r["title"] or not 0 < len(r["doc"]) <= 400)]
    seen = {}
    for r in listed:
        seen[(r["path"], r["entry"])] = seen.get((r["path"], r["entry"]), 0) + 1
    twice = sorted(k[1] for k, n in seen.items() if n > 1)
    check("nothing is described twice, and each has a title and a documentation",
          not bare and not twice, (bare[:4], twice[:4]))


def modules():
    loaded = "Inspection" in sys.modules
    rows = param_rows("visual inspection search distance")
    check("the omni search lists what the Visual Inspection dialog keeps, the module not loaded",
          not loaded and "Preferences/Mod/Inspection/Inspection/SearchDistance" in rows,
          (loaded, rows[:3]))
    for name in ("Part", "Sketcher", "Materials", "TechDraw", "Mesh", "Start"):
        try:
            __import__(name)
        except ImportError as e:
            note("no %s: %s" % (name, e))
    settle(0.5)
    wanted = (
        ("sketch panel constraints open", "Preferences/Mod/Sketcher/ExpandedConstraintsWidget"),
        ("materials editor width", "Preferences/Mod/Material/Editor/EditorWidth"),
        ("welding symbol tile colour", "Preferences/Mod/TechDraw/Colors/TileColor"),
        ("most occurrences of a pattern", "Preferences/Mod/Part/MaximumPatternOccurrences"),
        ("mesh from shape last surface deviation",
         "Preferences/Mod/Mesh/Meshing/Standard/LinearDeflection"),
        ("start first start", "Preferences/Mod/Start/FirstStart2024"),
    )
    missing = [row for query, row in wanted if row not in param_rows(query)]
    check("and what Sketcher, Material, TechDraw, Part, Mesh and Start had without a definition",
          not missing, missing)
    from draftutils import params  # noqa: F401 -- Draft's table

    settle(0.5)
    rows = param_rows("dxf export points")
    check("and the DXF exporter's options, with Draft's table loaded",
          "Preferences/Mod/Draft/ExportPoints" in rows, rows[:3])
    seen = {}
    for r in FreeCAD.listParams():
        seen[(r["path"], r["entry"])] = seen.get((r["path"], r["entry"]), 0) + 1
    twice = sorted(k[1] for k, n in seen.items() if n > 1)
    check("the registry holds no path and entry twice", bool(seen) and not twice,
          (len(seen), twice[:6]))


def run():
    try:
        spaceball()
        sandbox()
        small_groups()
        kept_by_the_program()
        modules()
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
