"""Where a task panel goes is kept for every kind of view, not for 3D views
alone (docs/TaskPanelPerView.md sec 15.6, milestone 5).

A 3D view saves its own properties with the document. A view an object
provides -- here a drawing page's -- saves nothing and is made again from
its object when the document is reopened, so the document keeps the place
of its task panel for it: by the object, in an entry of its own in
GuiDocument.xml that older readers pass over.

A panel is sent into a page's view with the combo view's title bar
button, put beside it on the right; the document is saved and reopened;
the view is a new one and holds no property; its next panel opens beside
it on the right all the same, with the preference saying "combo view".
Changed again and saved again, it comes back as changed.

The view must name its object as the saved layouts already need it to
(its widget's object name is the object's, TechDrawGui::MDIViewPage). A
spreadsheet's view did not, and was not covered (sec 15.13); it does now,
and with GT_KIND=sheet in the environment this script runs on one
(GuiTaskPanelKeptSheetView_tests_run), where it does not on a page.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set.

Scored against the tree before the change (7a45fa84b4), where the view's
properties die with the view; on a spreadsheet's view, against the tree
before that view had its name (da6a8cff6c).
"""
import os
import traceback
import zipfile

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from PySide6 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "TaskKept"
SAVED = os.path.join(OUT, "task-kept.FCStd")
KIND = os.environ.get("GT_KIND", "page")
SHEET = "SpreadsheetGui::SheetView" if KIND == "sheet" else "TechDrawGui::MDIViewPage"
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
TASKS = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/TaskView")
LAST = TASKS.GetGroup("Host")
Control = FreeCADGui.Control
QTest = QtTest.QTest
LEFT = QtCore.Qt.LeftButton

state = {"done": False, "doc": DOC, "panels": {}}
steps = []
KEEP = []


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


def all_widgets(name):
    out = []
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == name and not w.isHidden():
                out.append(w)
        except RuntimeError:
            pass
    return out


def sheet_view():
    """The page's view, as the document knows it."""
    v = FreeCADGui.getDocument(state["doc"]).mdiViewsOfType(SHEET)
    return v[0] if v else None


def sheet_widget():
    w = all_widgets(SHEET)
    return w[0] if w else None


def sheet_cell():
    w = sheet_widget()
    while w is not None and cls(w) != "Gui::ViewAreaCell":
        w = w.parentWidget()
    return w


def host():
    """The host of the page view's panel: over its cell, or in the
    panel cell beside it."""
    cell = sheet_cell()
    for h in all_widgets("Gui::TaskView::TaskPanelHost"):
        place = h.parentWidget()
        if place is None or cell is None:
            continue
        if place == cell or (cls(place) == "Gui::ViewAreaPanelCell"
                             and place.parentWidget() == cell.parentWidget()):
            KEEP.append(h)
            return h
    return None


def beside_on_the_right(key):
    h, cell, form = host(), sheet_cell(), state["panels"][key].form
    if h is None or cls(h.parentWidget()) != "Gui::ViewAreaPanelCell":
        return False, "no host in a panel cell"
    panel = h.parentWidget()
    px = panel.mapToGlobal(QtCore.QPoint(0, 0)).x()
    cx = cell.mapToGlobal(QtCore.QPoint(0, 0)).x()
    return px >= cx + cell.width() - 2 and h.isAncestorOf(form) and form.isVisible(), (px, cx)


def task_view():
    for w in all_widgets("Gui::TaskView::TaskView"):
        return w
    return None


def in_tab(key):
    tv, form = task_view(), state["panels"][key].form
    try:
        return tv is not None and tv.isAncestorOf(form) and form.isVisibleTo(tv)
    except RuntimeError:
        return False


def title_button():
    for b in mw().findChildren(QtWidgets.QAbstractButton, "OBTN TaskHost"):
        if b.isVisible():
            return b
    return None


def choose(what):
    host().findChild(QtGui.QAction, "taskPanelHost" + what).trigger()
    settle(600)


def own(prop):
    view = sheet_view()
    if prop not in view.PropertiesList:
        return None
    return getattr(view, prop)


def saved_entry():
    """The line of GuiDocument.xml that keeps the view's state, and the
    count the Camera element announces."""
    with zipfile.ZipFile(SAVED) as z:
        xml = z.read("GuiDocument.xml").decode("utf-8", "replace")
    lines = [l.strip() for l in xml.splitlines() if "<ViewTaskState" in l]
    camera = [l.strip() for l in xml.splitlines() if "<Camera" in l]
    return lines, camera[0] if camera else ""


class Panel:
    def __init__(self, title):
        self.form = QtWidgets.QWidget()
        self.form.setWindowTitle(title)
        self.edit = QtWidgets.QLineEdit(self.form)
        layout = QtWidgets.QVBoxLayout(self.form)
        layout.addWidget(QtWidgets.QLabel("a line of " + title, self.form))
        layout.addWidget(self.edit)

    def accept(self):
        return True

    def reject(self):
        return True


def show(key):
    mw().setActiveWindow(sheet_view())
    settle(400)
    panel = Panel(key)
    state["panels"][key] = panel
    try:
        Control.showDialog(panel)
    except RuntimeError as e:
        note("FAIL showing %s: %s" % (key, e))
    settle(600)


def open_sheet_view():
    if sheet_view() is None:
        FreeCADGui.getDocument(state["doc"]).getObject("Sheet").doubleClicked()
        settle(1200)


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
    VIEW.SetBool("TaskPanelInViewAll", False)
    VIEW.SetBool("TaskPanelInView", False)
    LAST.RemString("Side")
    LAST.RemString("Mode")
    for dock in mw().findChildren(QtWidgets.QDockWidget):
        if dock.objectName() in ("Python console", "Report view"):
            dock.hide()
    doc = FreeCAD.newDocument(DOC)
    doc.addObject("Part::Box", "Box")
    if KIND == "sheet":
        doc.addObject("Spreadsheet::Sheet", "Sheet").set("A1", "3")
    else:
        # A drawing page, named as the rest of this script names it
        page = doc.addObject("TechDraw::DrawPage", "Sheet")
        template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
        template.Template = os.path.join(FreeCAD.getResourceDir(), "Mod", "TechDraw",
                                         "Templates", "A4_LandscapeTD.svg")
        page.Template = template
    doc.recompute()
    settle(500)
    open_sheet_view()


@step
def into_the_spreadsheets_view():
    if sheet_view() is None or sheet_cell() is None:
        note("ABORT no page view in a cell")
        del steps[:]
        return
    show("first")
    check("the preference off: the page view's panel is in the Tasks tab",
          in_tab("first") and host() is None)
    claim("sent into its view by the combo view's title bar button",
          lambda: QTest.mouseClick(title_button(), LEFT) is None)
    settle(600)
    claim("it stands over the page",
          lambda: host() is not None and host().parentWidget() == sheet_cell())
    claim("put beside it, on the right",
          lambda: choose("Beside") is None and choose("Right") is None
          and beside_on_the_right("first")[0])
    check("the view holds all of it",
          (own("Task_Place"), own("Task_Mode"), own("Task_Side")) == ("InView", "Side", "Right"),
          (own("Task_Place"), own("Task_Mode"), own("Task_Side")))
    Control.closeDialog()


@step
def saved_and_closed():
    FreeCAD.getDocument(DOC).saveAs(SAVED)
    settle(400)

    def written():
        lines, camera = saved_entry()
        ok = (len(lines) == 1 and 'view="O:Sheet"' in lines[0] and 'place="InView"' in lines[0]
              and 'mode="Side"' in lines[0] and 'side="Right"' in lines[0]
              and 'viewstates="1"' in camera)
        return ok, (lines, camera[:160])

    claim("the document wrote it down for the view, by its object", written)
    FreeCAD.closeDocument(DOC)


@step
def reopened():
    state["doc"] = FreeCAD.openDocument(SAVED).Name
    settle(1500)
    open_sheet_view()


@step
def kept_for_a_view_made_again():
    if not check("the page's view is there again", sheet_view() is not None
                 and sheet_cell() is not None):
        return
    check("a new view, holding no property of its own",
          own("Task_Place") is None and own("Task_Mode") is None,
          (own("Task_Place"), own("Task_Mode")))
    check("the preference still says the combo view",
          VIEW.GetBool("TaskPanelInView", True) is False)
    show("second")
    claim("its next panel is beside it on the right all the same",
          lambda: beside_on_the_right("second"))
    check("and not in the Tasks tab", not in_tab("second"))
    claim("put over the view instead", lambda: choose("Overlay") is None
          and host().parentWidget() == sheet_cell())
    Control.closeDialog()


@step
def saved_again():
    FreeCAD.getDocument(state["doc"]).save()
    settle(400)
    claim("the document wrote the change down",
          lambda: ('mode="Overlay"' in saved_entry()[0][0], saved_entry()[0]))
    FreeCAD.closeDocument(state["doc"])


@step
def reopened_again():
    state["doc"] = FreeCAD.openDocument(SAVED).Name
    settle(1500)
    open_sheet_view()


@step
def as_it_was_changed():
    if not check("the page's view is there again", sheet_view() is not None
                 and sheet_cell() is not None):
        return
    show("third")
    claim("its panel is over the view now",
          lambda: host() is not None and host().parentWidget() == sheet_cell()
          and host().isAncestorOf(state["panels"]["third"].form))
    Control.closeDialog()


def advance():
    if state["done"]:
        return
    if not steps:
        finish()
        return
    fn = steps.pop(0)
    del KEEP[:]
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
    VIEW.SetBool("TaskPanelInViewAll", False)
    VIEW.SetBool("TaskPanelInView", False)
    LAST.RemString("Side")
    LAST.RemString("Mode")
    try:
        if Control.activeDialog():
            Control.closeDialog()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, advance)
