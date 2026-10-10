"""The view cell menu: what it lists, what opening it does, where a pick lands.

Three reports on one menu (docs/HandsOnQueue.md entry 27):
  - opening the menu opened a spreadsheet nobody asked for -- into the
    nearest non-3D cell, whose view it closed (a TechDraw page turned into a
    spreadsheet), or into a new split;
  - the menu listed every TechDraw object, dimensions and views and all, as
    if each could be shown in a cell;
  - a TechDraw page picked from the menu of a 3D cell landed in the
    spreadsheet's cell, and the spreadsheet was gone.
The menu asked every object's view provider for its view. A spreadsheet's
answers by making the view; a TechDraw view object's answers with its page's.
And a view opened by a pick was placed by the general policy.

Claims, on a document with a box, a spreadsheet, and a TechDraw page with a
view on it:
  - opening the menu of a 3D cell and dismissing it changes nothing: the
    same cells, the same views, no spreadsheet view made;
  - with the page shown in a second cell, opening that cell's menu leaves
    the page in it;
  - the menu lists the page once and none of its views;
  - the page picked from the menu of a 3D cell lands in THAT cell, and a
    spreadsheet shown in a third cell stays where it is.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "CellMenu"


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(n=40):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()


def kind(view):
    if view is None:
        return "empty"
    name = view.metaObject().className()
    return {"Gui::View3DInventor": "3D", "SpreadsheetGui::SheetView": "sheet",
            "TechDrawGui::MDIViewPage": "page"}.get(name, name)


def cells():
    """The cells of the document's view area, left to right: (button, kind of the view in it)"""
    mw = FreeCADGui.getMainWindow()
    res = []
    for button in mw.findChildren(QtWidgets.QWidget, "ViewAreaMenuButton"):
        cell = button.parentWidget()
        if cell is None or not cell.isVisible():
            continue
        child = None
        for w in cell.findChildren(QtWidgets.QMainWindow):
            if w.metaObject().className() in ("Gui::View3DInventor", "SpreadsheetGui::SheetView",
                                              "TechDrawGui::MDIViewPage"):
                child = w
                break
        res.append((cell.mapToGlobal(QtCore.QPoint(0, 0)).x(), button, kind(child)))
    res.sort(key=lambda r: r[0])
    return [(b, k) for _, b, k in res]


def kinds():
    return [k for _, k in cells()]


def sheet_views():
    """The spreadsheet views on screen (a closed one lives on until the event loop deletes it)"""
    return [w for w in QtWidgets.QApplication.allWidgets()
            if w.metaObject().className() == "SpreadsheetGui::SheetView" and w.isVisible()]


class MenuDriver(QtCore.QObject):
    """Opens a cell's menu (a blocking QMenu.exec) and acts on it from a timer."""

    def __init__(self):
        super().__init__()
        self.entries = None
        self.pick = None

    def open(self, button, pick=None):
        self.entries = None
        self.pick = pick
        QtCore.QTimer.singleShot(400, self._act)
        ev = QtGui.QMouseEvent(QtCore.QEvent.MouseButtonPress, QtCore.QPointF(4, 4),
                               QtCore.Qt.LeftButton, QtCore.Qt.LeftButton, QtCore.Qt.NoModifier)
        QtWidgets.QApplication.sendEvent(button, ev)
        ev = QtGui.QMouseEvent(QtCore.QEvent.MouseButtonRelease, QtCore.QPointF(4, 4),
                               QtCore.Qt.LeftButton, QtCore.Qt.NoButton, QtCore.Qt.NoModifier)
        QtWidgets.QApplication.sendEvent(button, ev)
        settle()
        return self.entries

    def _act(self):
        menu = QtWidgets.QApplication.activePopupWidget()
        if not isinstance(menu, QtWidgets.QMenu):
            self.entries = None
            return
        self.entries = [a.text() for a in menu.actions() if not a.isSeparator()]
        chosen = None
        if self.pick is not None:
            for a in menu.actions():
                if a.text() == self.pick:
                    chosen = a
        if chosen is not None:
            # as a user picks it: the menu's exec() has to return the action
            menu.setActiveAction(chosen)
            for what in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
                QtWidgets.QApplication.sendEvent(
                    menu, QtGui.QKeyEvent(what, QtCore.Qt.Key_Return, QtCore.Qt.NoModifier))
        if menu.isVisible():
            menu.close()


def model(name):
    """A box, a spreadsheet, and a TechDraw page with a view and a dimension on it"""
    doc = FreeCAD.newDocument(name)
    box = doc.addObject("Part::Box", "Box")
    doc.addObject("Spreadsheet::Sheet", "Numbers")
    page = doc.addObject("TechDraw::DrawPage", "Drawing")
    template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
    template.Template = os.path.join(
        FreeCAD.getResourceDir(), "Mod", "TechDraw", "Templates", "A4_Landscape_blank.svg")
    page.Template = template
    view = doc.addObject("TechDraw::DrawViewPart", "Front")
    view.Source = [box]
    page.addView(view)
    dim = doc.addObject("TechDraw::DrawViewDimension", "Width")
    page.addView(dim)
    doc.recompute()
    settle()
    return doc


def pick_scenario(driver):
    """Two 3D cells and a spreadsheet; the page, not open, picked from the first 3D cell's menu."""
    name = DOC + "Pick"
    model(name)
    try:
        start = kinds()
        if not check("pick: the document starts with one 3D cell", start == ["3D"], start):
            return
        FreeCADGui.runCommand("Std_ViewSplitRight")
        settle()
        FreeCADGui.getDocument(name).getObject("Numbers").doubleClicked()
        settle()
        layout = kinds()
        if not check("pick: two 3D cells and a spreadsheet cell to start from",
                     sorted(layout) == ["3D", "3D", "sheet"], layout):
            return
        first3d = layout.index("3D")
        driver.open(cells()[first3d][0], pick="Drawing")
        settle()
        after = kinds()
        want = list(layout)
        want[first3d] = "page"
        check("pick: the page lands in the cell whose menu was used", after == want, (layout, after))
        check("pick: the spreadsheet stays where it was",
              "sheet" in after and after.index("sheet") == layout.index("sheet"), (layout, after))
    finally:
        FreeCAD.closeDocument(name)
        settle()


def listing_scenario(driver):
    """Nothing but the 3D view open: what opening a cell's menu does, and what it lists."""
    name = DOC + "List"
    model(name)
    try:
        start = kinds()
        if not check("list: the document starts with one 3D cell and no spreadsheet view",
                     start == ["3D"] and not sheet_views(), (start, len(sheet_views()))):
            return
        entries = driver.open(cells()[0][0])
        check("list: the menu of the 3D cell opens", entries is not None, entries)
        entries = entries or []
        settle()
        check("list: opening the menu and dismissing it leaves the cells as they were",
              kinds() == start, kinds())
        check("list: opening the menu makes no spreadsheet view", not sheet_views(), len(sheet_views()))
        check("list: the menu lists the page, which is not open yet", entries.count("Drawing") == 1, entries)
        check("list: ... and the spreadsheet, which is not open either", entries.count("Numbers") == 1, entries)

        # the page into a second cell: its menu, opened and dismissed
        FreeCADGui.getDocument(name).getObject("Drawing").show()
        settle()
        before = kinds()
        if not check("list: the page shown sits in a cell beside the 3D view",
                     sorted(before) == ["3D", "page"], before):
            return
        entries = None
        for button, what in cells():
            if what == "page":
                entries = driver.open(button)
                break
        entries = entries or []
        settle()
        check("list: opening the menu of the page's cell leaves the page in it",
              kinds() == before, (before, kinds()))
        check("list: ... and makes no spreadsheet view", not sheet_views(), len(sheet_views()))
        check("list: with the page open, the menu lists it once", entries.count("Drawing") == 1, entries)
        check("list: ... and none of the page's views",
              not [e for e in entries if e in ("Front", "Width", "Template")], entries)
    finally:
        FreeCAD.closeDocument(name)
        settle()


def run():
    try:
        import TechDrawGui  # noqa: F401
        import SpreadsheetGui  # noqa: F401
        driver = MenuDriver()
        pick_scenario(driver)
        listing_scenario(driver)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
