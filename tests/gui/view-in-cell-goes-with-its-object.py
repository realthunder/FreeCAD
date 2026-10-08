"""A view kept in a split view cell goes when what it shows goes.

docs/HandsOnQueue.md entry 40. A TechDraw page's view is placed in a cell of
the document's view area by default, and a spreadsheet's can be. The page's
view provider took its view away only when it was one of the main window's
own windows, which a view in a cell is not: hiding the page or deleting it
left the view in its cell, drawing a page whose view provider was gone or
going. The spreadsheet's took the CELL away instead of the view, leaving the
area's splitters as they were.

Each step below returns to the main event loop before the next one looks:
a view is deleted by a deferred delete, which a nested loop never runs.

Claims, on a document with a box, a page with one view and a spreadsheet:
  - the page opened lands in a cell beside the 3D view;
  - the page hidden (Visibility off): its view is gone, the 3D cell is left;
  - shown again: its view is back in a cell;
  - the page deleted: its view is gone, the 3D cell is left;
  - the spreadsheet opened in a cell and deleted: the same;
  - the area is whole after that: a split gives two 3D cells;
  - a page that is the only cell: hidden, its view is gone and the cell
    stays, empty -- closing the area would close the document's last view
    and with it the document; shown again the page is back;
  - a document closed with its page in a cell: no page view is left, and
    the session goes on (a second document opens its page);
  - the session ends with a page in a cell (the process's exit code, read
    by the launcher, says how).
"""
import os
import tempfile
import traceback

import FreeCAD
import FreeCADGui
import shiboken6
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ViewInCell"
PAGE_VIEW = "TechDrawGui::MDIViewPage"
SHEET_VIEW = "SpreadsheetGui::SheetView"
VIEW_3D = "Gui::View3DInventor"
STEPS = []
STATE = {}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def step(delay_ms):
    def deco(fn):
        STEPS.append((delay_ms, fn))
        return fn
    return deco


def title(view):
    return view.windowTitle().replace("[*]", "")


def cells():
    """What each cell on screen holds, left to right: "3D", a page's or a
    sheet's label, or "empty"."""
    res = []
    for button in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget,
                                                          "ViewAreaMenuButton"):
        cell = button.parentWidget()
        if cell is None or not cell.isVisible():
            continue
        what = "empty"
        for w in cell.findChildren(QtWidgets.QMainWindow):
            if not shiboken6.isValid(w) or not w.isVisible():
                continue
            name = w.metaObject().className()
            if name == VIEW_3D:
                what = "3D"
            elif name in (PAGE_VIEW, SHEET_VIEW):
                what = title(w)
            else:
                continue
            break
        res.append((cell.mapToGlobal(QtCore.QPoint(0, 0)).x(), what))
    res.sort(key=lambda r: r[0])
    return [k for _, k in res]


def views(class_name):
    """The views of this class on screen, by title"""
    res = []
    for w in QtWidgets.QApplication.allWidgets():
        if shiboken6.isValid(w) and w.metaObject().className() == class_name and w.isVisible():
            res.append(title(w))
    return sorted(res)


def areas():
    """The split view containers on screen"""
    n = 0
    for w in QtWidgets.QApplication.allWidgets():
        if shiboken6.isValid(w) and w.metaObject().className() == "Gui::ViewArea" \
                and w.isVisible():
            n += 1
    return n


def model(name, sheet=False):
    doc = FreeCAD.newDocument(name)
    box = doc.addObject("Part::Box", "Box")
    page = doc.addObject("TechDraw::DrawPage", "Page")
    template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
    template.Template = os.path.join(
        FreeCAD.getResourceDir(), "Mod", "TechDraw", "Templates", "A4_Landscape_blank.svg")
    page.Template = template
    view = doc.addObject("TechDraw::DrawViewPart", "View")
    page.addView(view)
    view.Source = [box]
    if sheet:
        doc.addObject("Spreadsheet::Sheet", "Numbers")
    doc.recompute()
    return doc


def gui_object(name, obj):
    return FreeCADGui.getDocument(name).getObject(obj)


def close(name):
    try:
        FreeCAD.closeDocument(name)
    except Exception:
        pass


@step(1500)
def open_page():
    import SpreadsheetGui  # noqa: F401  the view providers
    import TechDrawGui  # noqa: F401
    model(DOC, sheet=True)
    gui_object(DOC, "Page").doubleClicked()


@step(1500)
def hide_page():
    if not check("the page opened lands in a cell beside the 3D view",
                 sorted(cells()) == ["3D", "Page"], cells()):
        raise RuntimeError("the page is not in a cell")
    gui_object(DOC, "Page").Visibility = False


@step(800)
def show_page():
    check("the page hidden: its view is gone", views(PAGE_VIEW) == [], views(PAGE_VIEW))
    check("... and the 3D cell is left alone", cells() == ["3D"], cells())
    gui_object(DOC, "Page").Visibility = True


@step(1500)
def delete_page():
    check("the page shown again: its view is back in a cell",
          sorted(cells()) == ["3D", "Page"] and views(PAGE_VIEW) == ["Page"],
          (cells(), views(PAGE_VIEW)))
    FreeCAD.getDocument(DOC).removeObject("Page")


@step(800)
def open_sheet():
    check("the page deleted: its view is gone", views(PAGE_VIEW) == [], views(PAGE_VIEW))
    check("... and the 3D cell is left alone", cells() == ["3D"], cells())
    gui_object(DOC, "Numbers").doubleClicked()


@step(1500)
def delete_sheet():
    if not check("the spreadsheet opened lands in a cell beside the 3D view",
                 sorted(cells()) == ["3D", "Numbers"], cells()):
        return
    FreeCAD.getDocument(DOC).removeObject("Numbers")


@step(800)
def split_after():
    check("the spreadsheet deleted: its view is gone", views(SHEET_VIEW) == [],
          views(SHEET_VIEW))
    check("... and the 3D cell is left alone", cells() == ["3D"], cells())
    view3d = FreeCADGui.getDocument(DOC).mdiViewsOfType(VIEW_3D)[0]
    FreeCADGui.getMainWindow().setActiveWindow(view3d)
    FreeCADGui.runCommand("Std_ViewSplitRight")


@step(1200)
def lone_page():
    check("the area is whole after that: a split gives two 3D cells", cells() == ["3D", "3D"],
          cells())
    close(DOC)
    name = DOC + "Lone"
    model(name)
    gui_object(name, "Page").doubleClicked()


@step(1500)
def lone_close_3d():
    name = DOC + "Lone"
    if not check("lone: the page in a cell beside the 3D view",
                 sorted(cells()) == ["3D", "Page"], cells()):
        raise RuntimeError("the page is not in a cell")
    for w in QtWidgets.QApplication.allWidgets():
        if shiboken6.isValid(w) and w.metaObject().className() == VIEW_3D and w.isVisible():
            w.close()
    STATE["lone"] = name


@step(1200)
def lone_hide():
    if not check("lone: the page is the only cell", cells() == ["Page"], cells()):
        raise RuntimeError("the page is not the only cell")
    gui_object(STATE["lone"], "Page").Visibility = False


@step(1000)
def lone_show():
    check("lone: the page hidden: its view is gone and its cell stays, empty",
          views(PAGE_VIEW) == [] and cells() == ["empty"] and areas() == 1,
          (views(PAGE_VIEW), cells(), areas()))
    check("lone: ... and the document is still open",
          STATE["lone"] in FreeCAD.listDocuments(), list(FreeCAD.listDocuments()))
    gui_object(STATE["lone"], "Page").Visibility = True


@step(1500)
def close_with_page():
    check("lone: shown again, the page is back, in the one cell",
          views(PAGE_VIEW) == ["Page"] and cells() == ["Page"], (views(PAGE_VIEW), cells()))
    close(STATE["lone"])
    name = DOC + "Close"
    model(name)
    gui_object(name, "Page").doubleClicked()
    STATE["close"] = name


@step(1500)
def close_document():
    name = STATE["close"]
    if not check("close: the page in a cell beside the 3D view",
                 sorted(cells()) == ["3D", "Page"], cells()):
        raise RuntimeError("the page is not in a cell")
    # a measurement, not a claim: was the document still there when the page's
    # view was destroyed? The view provider is freed with its document.
    STATE["document at destroy"] = None
    for w in QtWidgets.QApplication.allWidgets():
        if shiboken6.isValid(w) and w.metaObject().className() == PAGE_VIEW:
            w.destroyed.connect(
                lambda *a: STATE.__setitem__("document at destroy",
                                             name in FreeCAD.listDocuments()))
    FreeCAD.closeDocument(name)
    check("close: no page view on screen once the document is closed",
          views(PAGE_VIEW) == [], views(PAGE_VIEW))


@step(1500)
def second_document():
    note("NOTE close: the document was still open when its page's view was destroyed: %s"
         % STATE.get("document at destroy"))
    check("close: nothing is left of the document's views",
          views(PAGE_VIEW) == [] and cells() == [], (views(PAGE_VIEW), cells()))
    name = DOC + "Exit"
    doc = model(name)
    gui_object(name, "Page").doubleClicked()
    STATE["exit"] = (name, doc)


@step(1500)
def before_exit():
    name, doc = STATE["exit"]
    check("close: the session goes on, a second document opens its page in a cell",
          sorted(cells()) == ["3D", "Page"], cells())
    # saved, so that leaving with it open has nothing to lose; the page being
    # shown marks the document again, and sweep() below answers the question
    path = os.path.join(tempfile.mkdtemp(prefix="fc_view_in_cell_"), name + ".FCStd")
    doc.saveAs(path)
    check("exit: the document is saved and its page still in a cell",
          sorted(cells()) == ["3D", "Page"], cells())


def sweep():
    """A box nobody is there to answer: "Discard", and say so"""
    box = QtWidgets.QApplication.activeModalWidget()
    if isinstance(box, QtWidgets.QMessageBox):
        note("NOTE a box at exit: " + box.text().replace("\n", " "))
        button = box.button(QtWidgets.QMessageBox.Discard) or box.button(QtWidgets.QMessageBox.No)
        if button is not None:
            button.click()
        else:
            box.reject()


def finish():
    note("DONE")
    timer = QtCore.QTimer(FreeCADGui.getMainWindow())
    timer.timeout.connect(sweep)
    timer.start(300)
    # the last document is left open on purpose: the session ends with a page in a cell
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


def advance():
    if not STEPS:
        finish()
        return
    delay, fn = STEPS.pop(0)

    def run():
        try:
            fn()
        except Exception:
            note("ABORT in %s: %s" % (
                fn.__name__, traceback.format_exc().replace("\n", " | ")))
            STEPS.clear()
            for name in list(FreeCAD.listDocuments()):
                close(name)
        advance()

    QtCore.QTimer.singleShot(delay, run)


advance()
