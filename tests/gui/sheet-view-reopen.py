"""A spreadsheet's view names its object, and so a document is reopened
with it where it was (docs/SplitViews.md sec 5.6, docs/TaskPanelPerView.md
sec 15.13).

The saved layouts name a view an object provides by the object
(O:<object>), which they read off the view's widget: its object name is
the object's. A drawing page's view has carried it from the start; a
spreadsheet's view carried none, was written into no layout, and a
document saved with a spreadsheet beside its 3D view came back with the
3D view alone.

What reopening costs is watched here as much as what it gives. The
spreadsheet's view provider MAKES its view when asked for it, places it
by the placement policy and starts its edit; the layout then moves it
into its cell. So: no cell and no tab more than were saved, the document
not modified by being opened, no edit left on, the same layout written by
a second save. And a view that comes back by itself must stay away when
it was closed, and when its sheet is gone.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set.

Scored against the tree before the change (da6a8cff6c).
"""
import os
import traceback
import zipfile

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SheetKept"
SAVED = os.path.join(OUT, "sheet-kept.FCStd")
AGAIN = os.path.join(OUT, "sheet-kept-again.FCStd")
SHEET = "SpreadsheetGui::SheetView"
VIEW3D = "Gui::View3DInventor"
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
OPEN = VIEW.GetGroup("OpenView")

state = {"done": False, "doc": DOC}
steps = []


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def claim(name, fn):
    try:
        got = fn()
    except Exception as e:
        return check(name, False, "%s: %s" % (type(e).__name__, e))
    if isinstance(got, tuple):
        return check(name, bool(got[0]), got[1])
    return check(name, bool(got))


def settle(ms=200):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


def mw():
    return FreeCADGui.getMainWindow()


def cls(w):
    return w.metaObject().className()


def all_widgets(name, hidden=False):
    """Every live widget of a class; a hidden one is a stray one too."""
    out = []
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == name and (hidden or not w.isHidden()):
                out.append(w)
        except RuntimeError:
            pass
    return out


def gui_doc():
    return FreeCADGui.getDocument(state["doc"])


def sheet_widget():
    w = all_widgets(SHEET)
    return w[0] if w else None


def cell_of(w):
    while w is not None and cls(w) != "Gui::ViewAreaCell":
        w = w.parentWidget()
    return w


def census():
    """What is on the screen for the document: spreadsheet views (hidden
    ones counted), 3D views, view areas, cells, tabs of the MDI area (the
    start page is one of them)."""
    mdi = mw().findChild(QtWidgets.QMdiArea)
    return {
        "sheets": len(all_widgets(SHEET, hidden=True)),
        "sheets known": len(gui_doc().mdiViewsOfType(SHEET)),
        "3D": len(gui_doc().mdiViewsOfType(VIEW3D)),
        "areas": len(all_widgets("Gui::ViewArea", hidden=True)),
        "cells": len(all_widgets("Gui::ViewAreaCell", hidden=True)),
        "tabs": len(mdi.subWindowList()) if mdi else -1,
    }


def beside():
    """Where the spreadsheet's cell is against the 3D view's: the side,
    and its share of the two widths in percent."""
    sheet = cell_of(sheet_widget())
    views = all_widgets(VIEW3D)
    cell3d = cell_of(views[0]) if views else None
    if sheet is None or cell3d is None:
        return None
    sx = sheet.mapToGlobal(QtCore.QPoint(0, 0)).x()
    cx = cell3d.mapToGlobal(QtCore.QPoint(0, 0)).x()
    share = round(100.0 * sheet.width() / max(1, sheet.width() + cell3d.width()))
    return ("right" if sx > cx else "left", share)


def layout_lines(path):
    with zipfile.ZipFile(path) as z:
        xml = z.read("GuiDocument.xml").decode("utf-8", "replace")
    return [l.strip() for l in xml.splitlines() if "<ViewArea " in l]


def step(fn):
    steps.append(fn)
    return fn


@step
def build():
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("AutoSaveTimeout", 0)
    VIEW.SetInt("RenderCache", 3)
    VIEW.SetBool("ShowNaviCube", False)
    VIEW.SetBool("UseNavigationAnimations", False)
    VIEW.SetBool("UseViewArea", True)
    OPEN.SetString("DocViewTarget", "Split")
    OPEN.SetString("SplitDirection", "Right")
    for dock in mw().findChildren(QtWidgets.QDockWidget):
        if dock.objectName() in ("Python console", "Report view"):
            dock.hide()
    doc = FreeCAD.newDocument(DOC)
    doc.addObject("Part::Box", "Box")
    sheet = doc.addObject("Spreadsheet::Sheet", "Spreadsheet")
    sheet.set("A1", "3")
    doc.recompute()
    settle(500)
    gui_doc().getObject("Spreadsheet").doubleClicked()
    settle(1200)


@step
def named_and_saved():
    w = sheet_widget()
    if w is None or cell_of(w) is None:
        note("ABORT no spreadsheet view in a cell")
        del steps[:]
        return
    check("the spreadsheet's view carries its object's name",
          w.objectName() == "Spreadsheet", repr(w.objectName()))
    # The 3D view the active one when the document is saved
    views = all_widgets(VIEW3D)
    mw().setActiveWindow(gui_doc().mdiViewsOfType(VIEW3D)[0])
    settle(400)
    state["census"] = census()
    state["beside"] = beside()
    check("one 3D view and one spreadsheet view, in two cells of one tab",
          dict(state["census"], tabs=0) == {"sheets": 1, "sheets known": 1, "3D": 1,
                                            "areas": 1, "cells": 2, "tabs": 0}
          and len(views) == 1, state["census"])
    FreeCAD.getDocument(DOC).saveAs(SAVED)
    settle(400)
    state["layout"] = layout_lines(SAVED)
    check("the saved layout names the spreadsheet's view by its object",
          len(state["layout"]) == 1 and "O:Spreadsheet" in state["layout"][0],
          state["layout"])
    FreeCAD.closeDocument(DOC)
    settle(600)
    check("nothing of it is left when the document is closed",
          not all_widgets(SHEET, hidden=True), len(all_widgets(SHEET, hidden=True)))


@step
def reopened():
    state["doc"] = FreeCAD.openDocument(SAVED).Name
    settle(2000)


@step
def with_its_spreadsheet_view():
    now = census()
    check("the document is reopened with its spreadsheet view", now["sheets known"] == 1, now)
    check("no view, cell or tab more than were saved", now == state["census"],
          "%s, saved with %s" % (now, state["census"]))
    check("beside the 3D view as it was saved", beside() is not None
          and state["beside"] is not None and beside()[0] == state["beside"][0]
          and abs(beside()[1] - state["beside"][1]) <= 3,
          "%s, saved as %s" % (beside(), state["beside"]))
    w = sheet_widget()
    check("named again", w is not None and w.objectName() == "Spreadsheet",
          repr(w.objectName()) if w else None)
    claim("showing the sheet's cells", lambda: (
        w.findChild(QtWidgets.QTableView).model().index(0, 0).data() == "3",
        w.findChild(QtWidgets.QTableView).model().index(0, 0).data()))
    check("the document is not modified by being opened", not gui_doc().Modified)
    check("no edit is left on", gui_doc().getInEdit() is None, gui_doc().getInEdit())
    active = gui_doc().ActiveView
    check("the 3D view is the active one, as it was when saved",
          active is not None and type(active).__name__ == "View3DInventorPy"
          or "View3DInventor" in str(active), str(active))
    FreeCAD.getDocument(state["doc"]).saveAs(AGAIN)
    settle(400)
    check("saved again, the layout is written as it was",
          layout_lines(AGAIN) == state["layout"],
          "%s, was %s" % (layout_lines(AGAIN), state["layout"]))


@step
def closed_by_the_user():
    w = sheet_widget()
    state["had view"] = w is not None
    if w is not None:
        w.close()


@step
def closed_it_stays_closed():
    # A step later: what closing leaves to the event loop has been done
    if not check("the spreadsheet's view closed by the user", state["had view"],
                 "" if state["had view"] else "no view to close"):
        return
    now = census()
    check("none is made in its place", now["sheets"] == 0 and now["sheets known"] == 0, now)
    state["closed"] = now
    FreeCAD.getDocument(state["doc"]).saveAs(AGAIN)
    settle(400)
    check("the saved layout names it no more",
          not any("O:Spreadsheet" in l for l in layout_lines(AGAIN)), layout_lines(AGAIN))
    FreeCAD.closeDocument(state["doc"])
    settle(600)
    state["doc"] = FreeCAD.openDocument(AGAIN).Name
    settle(2000)
    now = census()
    check("reopened without it", now["sheets"] == 0 and now["3D"] == 1, now)
    check("and with no cell or tab more than it was saved with", now == state["closed"],
          "%s, saved with %s" % (now, state["closed"]))


@step
def opened_again_by_the_user():
    gui_doc().getObject("Spreadsheet").doubleClicked()
    settle(1200)
    now = census()
    check("opened again from the tree: one view, in a cell",
          now["sheets"] == 1 and now["sheets known"] == 1
          and cell_of(sheet_widget()) is not None, now)


@step
def its_sheet_deleted():
    FreeCAD.getDocument(state["doc"]).removeObject("Spreadsheet")


@step
def gone_with_its_sheet():
    now = census()
    check("the sheet deleted: its view is gone", now["sheets"] == 0 and now["sheets known"] == 0,
          now)
    check("and its cell with it", now["cells"] == 1 and now["3D"] == 1, now)
    state["deleted"] = now
    FreeCAD.getDocument(state["doc"]).saveAs(AGAIN)
    settle(400)
    check("the saved layout does not name a sheet that is gone",
          not any("O:Spreadsheet" in l for l in layout_lines(AGAIN)), layout_lines(AGAIN))
    FreeCAD.closeDocument(state["doc"])
    settle(600)
    state["doc"] = FreeCAD.openDocument(AGAIN).Name
    settle(2000)
    now = census()
    check("reopened as it was saved, the 3D view alone", now == state["deleted"],
          "%s, saved with %s" % (now, state["deleted"]))
    check("not modified by being opened", not gui_doc().Modified)


@step
def another_sheet():
    doc = FreeCAD.getDocument(state["doc"])
    doc.addObject("Spreadsheet::Sheet", "Second").set("A1", "5")
    doc.recompute()
    settle(300)
    gui_doc().getObject("Second").doubleClicked()
    settle(1200)


@step
def deleted_and_closed_in_one_go():
    # As a script does it: the sheet removed and its document closed with
    # no turn of the event loop between. The view is still on the screen
    # then, and closing the 3D view hands it the keyboard.
    w = sheet_widget()
    check("a second sheet's view is open in its cell",
          w is not None and w.objectName() == "Second", repr(w.objectName()) if w else None)
    name = state["doc"]
    note("NEXT the sheet deleted and its document closed in one go")
    FreeCAD.getDocument(name).removeObject("Second")
    FreeCAD.closeDocument(name)
    check("survived", name not in FreeCAD.listDocuments())


@step
def nothing_left():
    check("nothing of the document is left on the screen",
          not all_widgets(SHEET, hidden=True) and not all_widgets(VIEW3D, hidden=True),
          (len(all_widgets(SHEET, hidden=True)), len(all_widgets(VIEW3D, hidden=True))))


def advance():
    if state["done"]:
        return
    if not steps:
        finish()
        return
    fn = steps.pop(0)
    try:
        fn()
    except Exception:
        note("ABORT step %s:\n%s" % (fn.__name__, traceback.format_exc()))
        finish()
        return
    QtCore.QTimer.singleShot(600, advance)


def finish():
    if state["done"]:
        return
    state["done"] = True
    OPEN.RemString("DocViewTarget")
    OPEN.RemString("SplitDirection")
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, advance)
