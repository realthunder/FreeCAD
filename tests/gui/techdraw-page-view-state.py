"""A TechDraw page keeps its view state as a spreadsheet does, with no eye.

docs/HandsOnQueue.md entry 134 (the reporter's word of 2026-10-15): a page
shows no eye in the tree, like a spreadsheet; whether its view is open is
kept by the document's record of its views (the split view layouts, an
O:<page> leaf), not by its Visibility. The build session's rule for older
files: a file that carries no such record (another program's, or written
before the records) opens a page that was visible, once, at the load. Shown
and hidden from a script, a page still opens and closes its view.

Claims:
  - live: the page has no eye (NoToggleVisibility) and counts as shown with
    its view closed; "Hide all objects", "Show all objects" and the Space
    toggle of a selected page leave its view as it is; vp.hide() closes it
    and vp.show() opens it;
  - saved open and reopened: the page is open, the document unmodified, and
    the file names the page in a layout record;
  - saved closed and reopened: the page is closed;
  - the file saved open, its records emptied (Visibility still on): closed;
  - the same with no records at all, as an older file: open;
  - split views off: the page open in a tab of its own is recorded as a
    layout of its one leaf, and comes back open.

Runs with QT_QPA_PLATFORM=offscreen.
"""
import os
import re
import traceback
import zipfile

import ArchiveMembers
import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
STEPS = []
STATE = {"name": "PageState"}
VIEW_PARAMS = "User parameter:BaseApp/Preferences/View"


def path(tag):
    return os.path.join(OUT, tag + ".FCStd")


def note(msg):
    with open(RESULT, "a", encoding="utf-8") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def step(delay_ms):
    def deco(fn):
        STEPS.append((delay_ms, fn))
        return fn

    return deco


def pump(turns=20):
    for _ in range(turns):
        QtWidgets.QApplication.processEvents()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)


def vp(name="Page"):
    return FreeCADGui.getDocument(STATE["name"]).getObject(name)


def page_open():
    """Whether the page's view is up: its QGVPage, alive and shown"""
    pump()
    for v in FreeCADGui.getMainWindow().findChildren(QtWidgets.QGraphicsView):
        if v.metaObject().className() == "TechDrawGui::QGVPage" and v.isVisible():
            return True
    return False


def gui_xml(file):
    with zipfile.ZipFile(file) as z:
        return ArchiveMembers.read(z, "GuiDocument.xml").decode("utf-8")


def rewrite(src, dst, keep_attribute):
    """The file with its layout records gone: the attribute that says the
    file has them set to 0, or (an older file) taken out as well"""
    with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as zout:
        for item, data in ArchiveMembers.members(src):
            if item.filename == "GuiDocument.xml":
                text = data.decode("utf-8")
                text = re.sub(r"[ \t]*<ViewArea [^>]*/>\r?\n?", "", text)
                if keep_attribute:
                    text = re.sub(r' viewareas="\d+"', ' viewareas="0"', text)
                else:
                    text = re.sub(r' viewareas="\d+"', "", text)
                data = text.encode("utf-8")
            zout.writestr(item, data)


def reopen(file):
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    pump()
    STATE["name"] = FreeCAD.openDocument(file).Name


def save_and_close(file):
    FreeCAD.getDocument(STATE["name"]).saveAs(file)
    STATE["name"] = FreeCAD.getDocument(STATE["name"]).Name
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    pump()


@step(1000)
def make():
    import TechDrawGui  # noqa: F401 -- the view providers

    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("AutoSaveTimeout", 0)
    doc = FreeCAD.newDocument(STATE["name"])
    page = doc.addObject("TechDraw::DrawPage", "Page")
    template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
    template.Template = os.path.join(
        FreeCAD.getResourceDir(), "Mod", "TechDraw", "Templates", "A4_Landscape_blank.svg")
    page.Template = template
    doc.recompute()
    vp().show()


@step(2000)
def live():
    check("the page is open after show()", page_open())
    check("the page has no eye", vp().ToggleVisibility == "NoToggleVisibility",
          vp().ToggleVisibility)
    FreeCADGui.runCommand("Std_HideObjects")
    check("'Hide all objects' leaves the page open", page_open())
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(STATE["name"], "Page")
    FreeCADGui.runCommand("Std_ToggleVisibility")
    check("Space on the selected page leaves it open", page_open())
    FreeCADGui.Selection.clearSelection()
    vp().hide()
    check("vp.hide() closes the page", not page_open())
    check("closed, the page still counts as shown (not greyed)", vp().isVisible())
    FreeCADGui.runCommand("Std_ShowObjects")
    check("'Show all objects' leaves the page closed", not page_open())
    vp().show()
    check("vp.show() opens the page", page_open())
    save_and_close(path("open"))
    gui = gui_xml(path("open"))
    layouts = re.findall(r'<ViewArea layout="([^"]*)"', gui)
    check("the file saved open names the page in a layout record",
          any("O:Page" in l for l in layouts), layouts)
    STATE["name"] = FreeCAD.openDocument(path("open")).Name


@step(3000)
def reopened_open():
    check("saved open and reopened: the page is open", page_open())
    check("and the document is not modified", not FreeCADGui.getDocument(STATE["name"]).Modified)
    vp().hide()
    save_and_close(path("closed"))
    STATE["name"] = FreeCAD.openDocument(path("closed")).Name


@step(3000)
def reopened_closed():
    check("saved closed and reopened: the page is closed", not page_open())
    rewrite(path("open"), path("records-emptied"), True)
    rewrite(path("open"), path("no-records"), False)
    reopen(path("records-emptied"))


@step(3000)
def records_emptied():
    check("records emptied, Visibility on: the page is closed", not page_open())
    reopen(path("no-records"))


@step(3000)
def no_records():
    check("no records at all (an older file), Visibility on: the page opens once", page_open())
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    pump()
    params = FreeCAD.ParamGet(VIEW_PARAMS)
    STATE["useViewArea"] = params.GetBool("UseViewArea", True)
    params.SetBool("UseViewArea", False)
    STATE["name"] = FreeCAD.openDocument(path("closed")).Name


@step(3000)
def tabs():
    vp().show()
    check("split views off: the page opens", page_open())
    save_and_close(path("tab"))
    gui = gui_xml(path("tab"))
    layouts = re.findall(r'<ViewArea layout="([^"]*)"', gui)
    check("split views off: the page is recorded as a layout of its one leaf",
          "O:Page" in layouts, layouts)
    STATE["name"] = FreeCAD.openDocument(path("tab")).Name


@step(3000)
def tab_reopened():
    check("split views off, reopened: the page is open", page_open())
    FreeCAD.ParamGet(VIEW_PARAMS).SetBool("UseViewArea", STATE.get("useViewArea", True))
    reopen(path("tab"))


@step(3000)
def tab_reopened_split():
    check("the same file with split views on: the page is open", page_open())


def finish():
    if "useViewArea" in STATE:
        FreeCAD.ParamGet(VIEW_PARAMS).SetBool("UseViewArea", STATE["useViewArea"])
    for name in list(FreeCAD.listDocuments()):
        try:
            FreeCAD.closeDocument(name)
        except Exception:
            pass
    note("DONE")
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
            note("ABORT in %s: %s" % (fn.__name__, traceback.format_exc().replace("\n", " | ")))
            STEPS.clear()
        advance()

    QtCore.QTimer.singleShot(delay, run)


advance()
