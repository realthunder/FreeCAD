"""The settings of TechDraw's General group are listed, and a new page still takes them.

Thirty-five keys of Preferences/Mod/TechDraw/General are behind
TechDraw::TechDrawParams (docs/HandsOnQueue.md entry 24). TechDraw reads
its settings through hand-written accessors and at many places straight
from the group, each with a default of its own; they take the class's now.
Its pages are held to the definitions by
preferences-ok-keeps-defaults.py, which named three of them when the group
was defined: the new face finder is off to the program and was shown on by
the Advanced page; the vertex scale is 3 to the program and 5 on the Scale
page; the template mark size is 5 to the program and 3 on the page. OK
stored the page's each time.

Forty-six more followed, of the sub-groups Decorations, Dimensions, HLR,
PAT, Colors, Labels, LeaderLine, Rez, Tracker and debug -- those whose
default is a plain value. Two pages were named again: centre marks are off
to the program and were shown on by the Annotation page; the tolerance text
size is 0.5 to the program and 0.8 on the Dimensions page.

The colours, the line keys and the file names followed, and with them the
settings a page stored where nothing read them:

  - the face colour was read with 0xFFFFFF, which as a packed colour is
    cyan, so a new view on a profile that never stored it had cyan faces;
  - the iso line count is stored as an Int by its spin box and was read as a
    Bool, the ISO line spacing is stored as a Float and was read as an Int:
    a new view and a new dimension took the default whatever the page said;
  - "Use Polygon Approximation" stored HLR/UsePolygon while new views read
    General/CoarseView, and the "Leaderline" colour was stored as
    Markups/Color while leaders read LeaderLine/Color. The pages store the
    keys that are read; what they stored before is still honoured;
  - the section dialog kept its two keys in a group of its own, reached
    through a path with two colons.

Claims:

  - "/param vertex scale" and "/param new face finder" list the settings
    of the sub-group General, "/param tol size adjust" and "/param show
    center marks" those of Dimensions and Decorations;
  - a view made with "smooth edges" stored off does not show them, one
    made without the key does;
  - a page made with the page scale stored as 2 has a scale of 2, one made
    without the key 1;
  - a page made with "keep pages up to date" stored off does not keep
    itself updated, one made without the key does;
  - "/param face colour" and "/param leader line colour" list the colours;
  - a view made with no face colour stored has white faces;
  - a view made with the iso line count stored as the page stores it, 5,
    has 5; a dimension made with the ISO line spacing stored as the page
    stores it, 3.5, has 3.5;
  - a view made with General/CoarseView stored on is coarse, and so is one
    made with only what the HLR page stored before;
  - a leader made with only what the Colors page stored before, red, is
    red; with LeaderLine/Color stored blue as well it is blue;
  - the section dialog opened with "live update" stored off in TechDraw's
    General group has it off.

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


def sweep():
    """Close a modal box nobody is there to answer, and say so."""
    box = QtWidgets.QApplication.activeModalWidget()
    if box is not None:
        note("NOTE a modal box was closed: %s %r" % (box.metaObject().className(), box.windowTitle()))
        box.reject() if isinstance(box, QtWidgets.QDialog) else box.close()


def section_live_update(general):
    """The state of "live update" in the section dialog, opened with the switch stored off.

    In a document of its own: with more than one page the command asks which.
    """
    import TechDrawGui  # noqa: F401  the command

    doc = FreeCAD.newDocument("Entry24Section")
    settle(0.5)
    try:
        return section_dialog_state(doc, general)
    finally:
        if FreeCADGui.Control.activeDialog():
            FreeCADGui.Control.closeDialog()
        settle(0.5)
        general.RemBool("SectionLiveUpdate")
        FreeCAD.closeDocument(doc.Name)
        settle(0.5)


def section_dialog_state(doc, general):
    box = doc.addObject("Part::Box", "Box")
    page = doc.addObject("TechDraw::DrawPage", "PageSection")
    template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
    template.Template = os.path.join(
        FreeCAD.getResourceDir(), "Mod", "TechDraw", "Templates", "A4_Landscape_blank.svg")
    page.Template = template
    view = doc.addObject("TechDraw::DrawViewPart", "ViewToCut")
    view.Source = [box]
    page.addView(view)
    doc.recompute()
    settle(1.0)
    general.SetBool("SectionLiveUpdate", False)
    settle(0.2)
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(view)
    settle(0.3)
    FreeCADGui.runCommand("TechDraw_SectionView")
    settle(1.5)
    state = None
    for w in QtWidgets.QApplication.allWidgets():
        if w.objectName() == "cbLiveUpdate" and isinstance(w, QtWidgets.QCheckBox):
            state = w.isChecked()
    return state


def run():
    general =FreeCAD.ParamGet(PREFS + "Mod/TechDraw/General")
    hlr = FreeCAD.ParamGet(PREFS + "Mod/TechDraw/HLR")
    dims = FreeCAD.ParamGet(PREFS + "Mod/TechDraw/Dimensions")
    markups = FreeCAD.ParamGet(PREFS + "Mod/TechDraw/Markups")
    leader = FreeCAD.ParamGet(PREFS + "Mod/TechDraw/LeaderLine")
    doc = None
    sweeper = QtCore.QTimer()
    sweeper.timeout.connect(sweep)
    sweeper.start(2000)
    try:
        import TechDraw  # noqa: F401  the module registers its settings when it is loaded
        settle(0.5)

        rows = param_rows("vertex scale")
        check("the omni search lists TechDraw's vertex scale",
              any(r.endswith("Mod/TechDraw/General/VertexScale") for r in rows), rows[:6])
        rows = param_rows("new face finder")
        check("and its face finder switch", any(r.endswith("Mod/TechDraw/General/NewFaceFinder") for r in rows),
              rows[:6])

        rows = param_rows("tol size adjust")
        check("and the tolerance text size of the Dimensions group",
              any(r.endswith("Mod/TechDraw/Dimensions/TolSizeAdjust") for r in rows), rows[:6])
        rows = param_rows("show center marks")
        check("and the centre marks switch of the Decorations group",
              any(r.endswith("Mod/TechDraw/Decorations/ShowCenterMarks") for r in rows), rows[:6])

        doc = FreeCAD.newDocument("Entry24TechDraw")
        settle(0.5)
        hlr.SetBool("SmoothViz", False)
        settle(0.2)
        hidden = doc.addObject("TechDraw::DrawViewPart", "ViewNoSmooth").SmoothVisible
        hlr.RemBool("SmoothViz")
        settle(0.2)
        shown = doc.addObject("TechDraw::DrawViewPart", "ViewSmooth").SmoothVisible
        check("a view made with 'smooth edges' stored off does not show them, one made without the key does",
              hidden is False and shown is True, (hidden, shown))

        general.SetFloat("DefaultScale", 2.0)
        settle(0.2)
        two = doc.addObject("TechDraw::DrawPage", "PageScale2").Scale
        general.RemFloat("DefaultScale")
        settle(0.2)
        one = doc.addObject("TechDraw::DrawPage", "PageScale1").Scale
        check("a page made with the page scale stored as 2 has 2, one made without the key 1",
              abs(two - 2.0) < 1e-9 and abs(one - 1.0) < 1e-9, (two, one))

        general.SetBool("KeepPagesUpToDate", False)
        settle(0.2)
        off = doc.addObject("TechDraw::DrawPage", "PageStill").KeepUpdated
        general.RemBool("KeepPagesUpToDate")
        settle(0.2)
        on = doc.addObject("TechDraw::DrawPage", "PageLive").KeepUpdated
        check("a page made with 'keep pages up to date' stored off does not, one made without the key does",
              off is False and on is True, (off, on))

        rows = param_rows("face colour")
        check("the omni search lists TechDraw's face colour",
              any(r.endswith("Mod/TechDraw/Colors/FaceColor") for r in rows), rows[:6])
        rows = param_rows("leader line colour")
        check("and its leader line colour", any(r.endswith("Mod/TechDraw/LeaderLine/Color") for r in rows), rows[:6])

        face = doc.addObject("TechDraw::DrawViewPart", "ViewFace").ViewObject.FaceColor
        check("a view made with no face colour stored has white faces", tuple(face[:3]) == (1.0, 1.0, 1.0), face)

        hlr.SetInt("IsoCount", 5)
        settle(0.2)
        count = doc.addObject("TechDraw::DrawViewPart", "ViewIso").IsoCount
        hlr.RemInt("IsoCount")
        check("a view made with the iso line count stored as the page stores it has it", count == 5, count)

        dims.SetFloat("LineSpacingFactorISO", 3.5)
        settle(0.2)
        spacing = doc.addObject("TechDraw::DrawViewDimension", "DimSpacing").ViewObject.LineSpacingFactorISO
        dims.RemFloat("LineSpacingFactorISO")
        check("a dimension made with the ISO line spacing stored as the page stores it has it",
              abs(spacing - 3.5) < 1e-9, spacing)

        general.SetBool("CoarseView", True)
        settle(0.2)
        now = doc.addObject("TechDraw::DrawViewPart", "ViewCoarse").CoarseView
        general.RemBool("CoarseView")
        hlr.SetBool("UsePolygon", True)
        settle(0.2)
        before = doc.addObject("TechDraw::DrawViewPart", "ViewPolygon").CoarseView
        hlr.RemBool("UsePolygon")
        settle(0.2)
        neither = doc.addObject("TechDraw::DrawViewPart", "ViewFine").CoarseView
        check("a view made with the polygon approximation stored on is coarse, by the key that is read and by "
              "what the page stored before", now is True and before is True and neither is False,
              (now, before, neither))

        markups.SetUnsigned("Color", 0xFF0000FF)
        settle(0.2)
        red = doc.addObject("TechDraw::DrawLeaderLine", "LeaderRed").ViewObject.Color
        leader.SetUnsigned("Color", 0x0000FFFF)
        settle(0.2)
        blue = doc.addObject("TechDraw::DrawLeaderLine", "LeaderBlue").ViewObject.Color
        markups.RemUnsigned("Color")
        leader.RemUnsigned("Color")
        check("a leader made with only what the Colors page stored before is that colour; the key that is read "
              "wins", tuple(red[:3]) == (1.0, 0.0, 0.0) and tuple(blue[:3]) == (0.0, 0.0, 1.0), (red, blue))

        live = section_live_update(general)
        check("the section dialog opened with 'live update' stored off in TechDraw's General group has it off",
              live is False, live)
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        general.RemFloat("DefaultScale")
        general.RemBool("KeepPagesUpToDate")
        hlr.RemBool("SmoothViz")
        hlr.RemInt("IsoCount")
        hlr.RemBool("UsePolygon")
        general.RemBool("CoarseView")
        general.RemBool("SectionLiveUpdate")
        dims.RemFloat("LineSpacingFactorISO")
        markups.RemUnsigned("Color")
        leader.RemUnsigned("Color")
        sweeper.stop()
        if doc is not None:
            FreeCAD.closeDocument(doc.Name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
