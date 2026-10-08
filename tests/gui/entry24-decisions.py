"""What the reporter decided about entry 24's defaults and findings holds.

Entry 24 of docs/HandsOnQueue.md put every setting C++ reads behind a
generated class. On the way a default was chosen wherever a page and the
program disagreed, and sixteen findings were left open. The reporter went
through the list (..\\dl\\handson\\2026-10-08\\entry24-decisions.md) on
2026-10-08: where a default had been chosen, upstream's program decides;
most of the findings are to be fixed. docs/HandsOnLog.md, entry 24, "The
decisions", has every item. This test holds the ones a script can see.

Claims, on a profile with nothing stored:

  - the report view is drawn in Courier (A2: the reporter kept the fork's
    default over upstream's system font), in a stored family once one is
    stored, and in Courier again when that is removed;
  - a mesh written as an Asymptote file says size(500) (A9: the height is
    empty to upstream's program; it was 500 here);
  - "/param" lists TechDraw's preselection and selection colour (D3), the
    Sketcher's label font size and constraint symbol size (D6), Assembly's
    joint highlight colour (D14) and Gmsh's thread count (D16);
  - "/param datum colour" lists ONE DefaultDatumColor, Part's (D12:
    PartDesign had a second one, which only the datums read);
  - with that colour stored, a new shape binder has it, as a new datum
    plane does;
  - the section line of a new TechDraw view is line 4 of the line standard,
    and line 2 with the Annotation page's list stored at its second entry
    (D1, as upstream: the default came from a key no page stores);
  - choosing Diameter in the Sketcher's radius or diameter button stores
    the choice (D5: path and key were one string, by a missing comma).

That OK in the preferences stores no editor colour, no editor font, none of
the Macro page's two dead keys and neither of TechDraw's two following
colours is a claim of preferences-ok-keeps-defaults.py.

Scored against the tree before the change: see the commit message.
"""
import os
import tempfile
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


def sweep():
    """A box nobody is there to answer must not hold the session."""
    for w in QtWidgets.QApplication.topLevelWidgets():
        if isinstance(w, QtWidgets.QMessageBox) and w.isVisible():
            note("NOTE closed a message box: " + w.text().replace("\n", " ")[:160])
            w.reject()


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


def report_font():
    for w in QtWidgets.QApplication.allWidgets():
        if w.metaObject().className() == "Gui::DockWnd::ReportOutput":
            return w.font().family()
    return None


LISTED = (
    ("TechDraw", "preselection colour", "Mod/TechDraw/Colors/PreSelectColor"),
    ("TechDraw", "selection colour", "Mod/TechDraw/Colors/SelectColor"),
    ("Sketcher", "sketch label font size", "View/EditSketcherFontSize"),
    ("Sketcher", "constraint symbol size", "View/ConstraintSymbolSize"),
    ("AssemblyApp", "joint highlight colour", "Mod/Assembly/JointHighlightColor"),
    ("Fem", "gmsh threads", "Mod/Fem/Gmsh/NumOfThreads"),
)


def run():
    doc = None
    editor = FreeCAD.ParamGet(PREFS + "Editor")
    part = FreeCAD.ParamGet(PREFS + "Mod/Part")
    decorations = FreeCAD.ParamGet(PREFS + "Mod/TechDraw/Decorations")
    sketcher = FreeCAD.ParamGet(PREFS + "Mod/Sketcher")
    sweeper = QtCore.QTimer()
    sweeper.timeout.connect(sweep)
    sweeper.start(700)
    try:
        unset = report_font()
        check("with no font stored the report view is drawn in Courier", unset == "Courier", unset)
        editor.SetString("Font", "Arial")
        settle(0.4)
        stored = report_font()
        editor.RemString("Font")
        settle(0.4)
        check("in a stored family once one is stored, and in Courier again when it is removed",
              stored == "Arial" and report_font() == "Courier", (stored, report_font()))

        import Mesh

        path = os.path.join(tempfile.mkdtemp(prefix="entry24-", dir=OUT), "box.asy")
        Mesh.createBox(10.0, 10.0, 10.0).write(path)
        with open(path) as f:
            sizes = [line.strip() for line in f if line.startswith("size(")]
        check("a mesh written as an Asymptote file says size(500)", sizes == ["size(500);"], sizes)

        for module, query, key in LISTED:
            try:
                __import__(module)  # a module registers its settings when it is loaded
            except ImportError as e:
                check("the omni search lists " + key, False, "no module %s: %s" % (module, e))
                continue
            settle(0.5)
            rows = param_rows(query)
            check("the omni search lists " + key, any(r.endswith(key) for r in rows), rows[:5])

        import PartDesignGui  # noqa: F401

        settle(0.5)
        rows = [r for r in param_rows("datum colour") if r.endswith("/DefaultDatumColor")]
        check("the omni search lists one datum colour, Part's",
              len(rows) == 1 and rows[0].endswith("Mod/Part/DefaultDatumColor"), rows)

        doc = FreeCAD.newDocument("Entry24Decisions")
        settle(0.5)
        part.SetUnsigned("DefaultDatumColor", 0x00FF00FF)
        settle(0.2)
        binder = doc.addObject("PartDesign::ShapeBinder", "BinderGreen")
        plane = doc.addObject("PartDesign::Plane", "DatumGreen")
        settle(0.3)
        colours = [tuple(round(c, 3) for c in o.ViewObject.ShapeColor[:3]) for o in (binder, plane)]
        part.RemUnsigned("DefaultDatumColor")
        check("with a datum colour stored, a new shape binder has it, as a new datum plane does",
              colours == [(0.0, 1.0, 0.0), (0.0, 1.0, 0.0)], colours)

        import TechDrawGui  # noqa: F401

        def section_line(name):
            view = doc.addObject("TechDraw::DrawViewPart", name)
            settle(0.3)
            names = view.ViewObject.getEnumerationsOfProperty("SectionLineStyle")
            return names.index(view.ViewObject.SectionLineStyle)

        unset_line = section_line("ViewUnset")
        decorations.SetInt("LineStyleSection", 1)
        settle(0.2)
        stored_line = section_line("ViewStored")
        decorations.RemInt("LineStyleSection")
        check("the section line of a new view is line 4 of the standard, and line 2 with the page's list "
              "stored at its second entry", (unset_line, stored_line) == (4, 2), (unset_line, stored_line))

        import SketcherGui  # noqa: F401

        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        doc.recompute()
        FreeCADGui.ActiveDocument.setEdit(sketch.Name)
        settle(1.0)
        before = sketcher.GetInt("CurRadDiaCons", -1)
        FreeCADGui.runCommand("Sketcher_CompConstrainRadDia", 1)
        settle(0.5)
        after = sketcher.GetInt("CurRadDiaCons", -1)
        FreeCADGui.ActiveDocument.resetEdit()
        settle(0.5)
        check("choosing Diameter in the radius or diameter button stores the choice",
              before == -1 and after == 1, (before, after))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        sweeper.stop()
        editor.RemString("Font")
        part.RemUnsigned("DefaultDatumColor")
        decorations.RemInt("LineStyleSection")
        sketcher.RemInt("CurRadDiaCons")
        if doc is not None:
            FreeCAD.closeDocument(doc.Name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
