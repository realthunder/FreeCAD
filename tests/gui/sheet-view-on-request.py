"""A spreadsheet's view is made when it is asked FOR, not when it is asked about.

docs/HandsOnQueue.md entry 45. ViewProviderSheet::getMDIView() was the one
getMDIView that created: it made the view, placed it and started editing.
The callers treat getMDIView() as a question -- the tree's "sync view" asks
it at every click -- so a question opened a view, and placed it by the
general policy, which may close another view to make room.

Claims, on a document with a box and two spreadsheets, Numbers and Prices:
  - one click on a sheet in the tree selects it and opens nothing;
  - a double click opens its view;
  - with that view open and the 3D view in front, one click on the sheet
    brings its view to the front, as before;
  - Document.setEdit on a sheet that is not open opens its view and leaves
    it the active one;
  - with Numbers open beside the 3D view, a second 3D cell active and
    Prices selected, Std_ViewCellShowObject puts Prices into the active
    cell and leaves Numbers' view where it is;
  - Prices picked from the menu of a 3D cell lands in THAT cell, and
    Numbers' view stays;
  - a saved document with a sheet in a cell comes back with it.
"""
import os
import tempfile
import time
import traceback

import FreeCAD
import FreeCADGui
import shiboken6
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SheetOnRequest"
Qt = QtCore.Qt


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(n=60):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()


def wait(ms):
    end = time.monotonic() + ms / 1000.0
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.005)


def mouse(widget, what, pos, buttons):
    ev = QtGui.QMouseEvent(what, QtCore.QPointF(pos), QtCore.QPointF(widget.mapToGlobal(pos)),
                           Qt.LeftButton, buttons, Qt.NoModifier)
    QtWidgets.QApplication.sendEvent(widget, ev)


def title(view):
    return view.windowTitle().replace("[*]", "")


def views_in(cell):
    """The live 3D and spreadsheet views inside a cell: (class name, widget)"""
    res = []
    for w in cell.findChildren(QtWidgets.QMainWindow):
        if not shiboken6.isValid(w):
            continue
        name = w.metaObject().className()
        if name in ("Gui::View3DInventor", "SpreadsheetGui::SheetView"):
            res.append((name, w))
    return res


def cells():
    """The cells of the view area, left to right: (button, what is in it).

    A 3D view is "3D"; a spreadsheet's view is its sheet's label."""
    mw = FreeCADGui.getMainWindow()
    res = []
    for button in mw.findChildren(QtWidgets.QWidget, "ViewAreaMenuButton"):
        cell = button.parentWidget()
        if cell is None or not cell.isVisible():
            continue
        what = "empty"
        for name, w in views_in(cell):
            what = "3D" if name == "Gui::View3DInventor" else title(w)
            break
        res.append((cell.mapToGlobal(QtCore.QPoint(0, 0)).x(), button, what))
    res.sort(key=lambda r: r[0])
    return [(b, k) for _, b, k in res]


def kinds():
    return [k for _, k in cells()]


def sheet_views():
    """The labels of the spreadsheet views on screen"""
    return sorted(title(w) for w in QtWidgets.QApplication.allWidgets()
                  if shiboken6.isValid(w)
                  and w.metaObject().className() == "SpreadsheetGui::SheetView" and w.isVisible())


def active():
    view = FreeCADGui.activeView()
    if view is None:
        return None
    if hasattr(view, "getSheet"):
        return view.getSheet().Label
    return "3D" if hasattr(view, "getCameraNode") else type(view).__name__


def tree_item(label):
    """(tree, rectangle) of the item with this label in the model tree"""
    for tree in FreeCADGui.getMainWindow().findChildren(QtWidgets.QTreeWidget):
        if tree.metaObject().className() != "Gui::TreeWidget" or not tree.isVisible():
            continue
        for item in tree.findItems(label, Qt.MatchExactly | Qt.MatchRecursive, 0):
            tree.scrollToItem(item)
            return tree, tree.visualItemRect(item)
    return None, None


def click(label, double=False):
    tree, rect = tree_item(label)
    if tree is None:
        return False
    pos = QtCore.QPoint(rect.left() + 30, rect.center().y())
    port = tree.viewport()
    mouse(port, QtCore.QEvent.MouseButtonPress, pos, Qt.LeftButton)
    mouse(port, QtCore.QEvent.MouseButtonRelease, pos, Qt.NoButton)
    if double:
        mouse(port, QtCore.QEvent.MouseButtonDblClick, pos, Qt.LeftButton)
        mouse(port, QtCore.QEvent.MouseButtonRelease, pos, Qt.NoButton)
    settle()
    wait(300)
    settle()
    return True


class MenuDriver(QtCore.QObject):
    """Opens a cell's menu (a blocking QMenu.exec) and picks from it by a timer."""

    def __init__(self):
        super().__init__()
        self.entries = None
        self.pick = None

    def open(self, button, pick=None):
        self.entries = None
        self.pick = pick
        QtCore.QTimer.singleShot(400, self._act)
        for what, buttons in ((QtCore.QEvent.MouseButtonPress, Qt.LeftButton),
                              (QtCore.QEvent.MouseButtonRelease, Qt.NoButton)):
            ev = QtGui.QMouseEvent(what, QtCore.QPointF(4, 4), Qt.LeftButton, buttons,
                                   Qt.NoModifier)
            QtWidgets.QApplication.sendEvent(button, ev)
        settle()
        return self.entries

    def _act(self):
        menu = QtWidgets.QApplication.activePopupWidget()
        if not isinstance(menu, QtWidgets.QMenu):
            return
        self.entries = [a.text() for a in menu.actions() if not a.isSeparator()]
        for a in menu.actions():
            if self.pick is not None and a.text() == self.pick:
                menu.setActiveAction(a)
                for what in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
                    QtWidgets.QApplication.sendEvent(
                        menu, QtGui.QKeyEvent(what, Qt.Key_Return, Qt.NoModifier))
        if menu.isVisible():
            menu.close()


def model(name):
    doc = FreeCAD.newDocument(name)
    doc.addObject("Part::Box", "Box")
    doc.addObject("Spreadsheet::Sheet", "Numbers")
    doc.addObject("Spreadsheet::Sheet", "Prices")
    doc.recompute()
    settle()
    return doc


def close(name):
    FreeCAD.closeDocument(name)
    settle()
    wait(200)
    settle()


def click_scenario():
    name = DOC + "Click"
    model(name)
    try:
        if not check("click: nothing but the 3D view to start with",
                     kinds() == ["3D"] and not sheet_views(), (kinds(), sheet_views())):
            return
        found = click("Numbers")
        selected = [o.Name for o in FreeCADGui.Selection.getSelection()]
        check("click: one click on a sheet selects it", found and selected == ["Numbers"],
              (found, selected))
        check("click: ... and opens no view", not sheet_views() and kinds() == ["3D"],
              (sheet_views(), kinds()))
        click("Numbers", double=True)
        check("click: a double click opens the sheet's view", sheet_views() == ["Numbers"],
              (sheet_views(), kinds()))
        click("Box")
        front = active()
        click("Numbers")
        check("click: with its view open, one click on the sheet brings the view to the front",
              sheet_views() == ["Numbers"] and active() == "Numbers", (front, active()))
    finally:
        close(name)


def edit_scenario():
    name = DOC + "Edit"
    model(name)
    try:
        FreeCADGui.getDocument(name).setEdit("Prices")
        settle()
        wait(300)
        settle()
        check("edit: setEdit on a sheet that is not open opens its view and leaves it active",
              sheet_views() == ["Prices"] and active() == "Prices", (sheet_views(), active()))
        FreeCADGui.getDocument(name).resetEdit()
        settle()
    finally:
        close(name)


def two_3d_and_numbers(name):
    """A 3D cell, a second 3D cell, and Numbers in a third; False if it did not come out so"""
    FreeCADGui.runCommand("Std_ViewSplitRight")
    settle()
    FreeCADGui.getDocument(name).getObject("Numbers").doubleClicked()
    settle()
    wait(200)
    settle()
    return check("two 3D cells and Numbers to start from", sorted(kinds()) == ["3D", "3D", "Numbers"],
                 kinds())


def show_scenario():
    name = DOC + "Show"
    model(name)
    try:
        if not two_3d_and_numbers(name):
            return
        layout = kinds()
        # make a 3D view the active one, as a click into its cell does
        view3d = FreeCADGui.getDocument(name).mdiViewsOfType("Gui::View3DInventor")[0]
        FreeCADGui.getMainWindow().setActiveWindow(view3d)
        settle()
        if not check("show: a 3D cell is the active one", active() == "3D", active()):
            return
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(name, "Prices")
        settle()
        if not check("show: ... and still is with the other sheet selected",
                     active() == "3D" and not [v for v in sheet_views() if v == "Prices"],
                     (active(), sheet_views())):
            return
        FreeCADGui.runCommand("Std_ViewCellShowObject")
        settle()
        wait(300)
        settle()
        check("show: the selected sheet takes the place of a 3D view, the active one",
              sorted(kinds()) == ["3D", "Numbers", "Prices"] and active() == "Prices",
              (layout, kinds(), active()))
        check("show: ... and the other sheet's view is left alone",
              "Numbers" in sheet_views() and "Numbers" in kinds()
              and kinds().index("Numbers") == layout.index("Numbers"), (sheet_views(), kinds()))
    finally:
        close(name)


def pick_scenario(driver):
    name = DOC + "Pick"
    model(name)
    try:
        if not two_3d_and_numbers(name):
            return
        layout = kinds()
        first3d = layout.index("3D")
        entries = driver.open(cells()[first3d][0], pick="Prices")
        settle()
        wait(300)
        settle()
        want = list(layout)
        want[first3d] = "Prices"
        check("pick: the menu lists both sheets", entries is not None
              and entries.count("Numbers") == 1 and entries.count("Prices") == 1, entries)
        check("pick: the sheet picked lands in the cell whose menu was used", kinds() == want,
              (layout, kinds()))
        check("pick: ... and the other sheet's view is left alone",
              "Numbers" in sheet_views() and "Numbers" in kinds()
              and kinds().index("Numbers") == layout.index("Numbers"), (sheet_views(), kinds()))
    finally:
        close(name)


def reopen_scenario():
    name = DOC + "Reopen"
    doc = model(name)
    path = os.path.join(tempfile.mkdtemp(prefix="fc_sheet_request_"), name + ".FCStd")
    try:
        FreeCADGui.getDocument(name).getObject("Numbers").doubleClicked()
        settle()
        wait(200)
        settle()
        before = kinds()
        if not check("reopen: the 3D view and Numbers side by side before saving",
                     sorted(before) == ["3D", "Numbers"], before):
            return
        doc.saveAs(path)
        settle()
    finally:
        close(name)
    try:
        FreeCAD.openDocument(path)
        settle()
        wait(800)
        settle()
        check("reopen: the document comes back with the sheet in its cell", kinds() == before,
              (before, kinds()))
        check("reopen: ... and with no other sheet opened", sheet_views() == ["Numbers"],
              sheet_views())
    finally:
        close(name)


def run():
    try:
        import SpreadsheetGui  # noqa: F401
        driver = MenuDriver()
        click_scenario()
        edit_scenario()
        show_scenario()
        pick_scenario(driver)
        reopen_scenario()
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
