"""TechDraw prints the selected pages and no others.

docs/HandsOnQueue.md entry 127, the reporter 2026-10-14 17:11: "support print
ony the selected Techdraw pages".

Three pages in a document. Claims:
  - the command TechDraw_PrintSelected is there, beside TechDraw_PrintAll in
    the TechDraw workbench's menus, titled "Print Selected Pages";
  - it cannot be used with no page selected, and can with one;
  - the page view's context menu has "Print Selected Pages" while pages are
    selected, and not otherwise.
What it prints goes through the platform's print dialog, which a script
cannot answer: that part is the reporter's to look at.

Runs with QT_QPA_PLATFORM=offscreen; the pointer is not moved.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=300):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def active(name):
    cmd = FreeCADGui.Command.get(name)
    return cmd is not None and cmd.isActive()


def menu_texts_of_page_view(view_widget):
    """The texts of the page view's context menu, the menu closed as it opens"""
    texts = []

    def grab():
        for w in QtWidgets.QApplication.topLevelWidgets():
            if isinstance(w, QtWidgets.QMenu) and w.isVisible():
                texts.extend(a.text().replace("&", "") for a in w.actions())
                w.close()

    QtCore.QTimer.singleShot(500, grab)
    ev = QtGui.QContextMenuEvent(QtGui.QContextMenuEvent.Mouse, QtCore.QPoint(20, 20),
                                 view_widget.mapToGlobal(QtCore.QPoint(20, 20)))
    QtWidgets.QApplication.sendEvent(view_widget, ev)
    settle(300)
    return texts


def run():
    try:
        FreeCADGui.activateWorkbench("TechDrawWorkbench")
        settle(800)
        doc = FreeCAD.newDocument("PrintSelected")
        pages = []
        for i in range(3):
            page = doc.addObject("TechDraw::DrawPage", "Page%d" % i)
            template = doc.addObject("TechDraw::DrawSVGTemplate", "Template%d" % i)
            page.Template = template
            pages.append(page)
        doc.recompute()
        settle(1500)

        cmd = FreeCADGui.Command.get("TechDraw_PrintSelected")
        check("the command is there", cmd is not None)
        if cmd is None:
            return
        title = cmd.getInfo()["menuText"].replace("&", "")
        check("titled 'Print Selected Pages'", title == "Print Selected Pages", title)

        FreeCADGui.Selection.clearSelection()
        settle(300)
        check("no page selected: it cannot be used", not active("TechDraw_PrintSelected"))
        FreeCADGui.Selection.addSelection(doc.Name, pages[1].Name)
        settle(300)
        check("a page selected: it can", active("TechDraw_PrintSelected"))

        # the page view's own menu
        pages[0].ViewObject.doubleClicked()
        settle(1500)
        view = None
        for w in QtWidgets.QApplication.allWidgets():
            if w.metaObject().className() == "TechDrawGui::MDIViewPage" and w.isVisible():
                view = w
        if not check("a page's view is open", view is not None):
            return
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(doc.Name, pages[2].Name)
        settle(300)
        texts = menu_texts_of_page_view(view)
        note("INFO the menu with a page selected: %s" % texts)
        check("its context menu has 'Print Selected Pages' with a page selected",
              "Print Selected Pages" in texts and "Print All Pages" in texts, texts)
        FreeCADGui.Selection.clearSelection()
        settle(300)
        texts = menu_texts_of_page_view(view)
        check("and not with none selected", "Print Selected Pages" not in texts, texts)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(3000, run)
