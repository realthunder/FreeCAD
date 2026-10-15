"""TechDraw_Print: the pages to print or export, chosen in a dialog.

docs/HandsOnQueue.md entry 137 (the reporter, 2026-10-15 10:20, 11:27,
11:33): one command, a printer icon, "Print pages"; a dialog with a tree of
every open document and its pages, check boxes on both, a document's box
toggling its pages; at first the current document expanded with all its
pages checked; a list beside it of the pages checked as document#page
(labels), reordered by buttons and by dragging, the order printed; a button
to export one PDF; the last choice remembered for the session.

Three documents: "Drawings A" with three pages, "Drawings B" with two, a
third with none; A the current one. Claims:
  - TechDraw_Print is there, "Print pages", the printer's icon, its tool tip
    naming the PDF; TechDraw_PrintAll and TechDraw_PrintSelected are gone;
  - the dialog lists A and B, not the document with no page; A expanded, B
    not; A's pages checked, B's not; the order list "Drawings A#Sheet A1" ...;
  - B's box checks both its pages, which come at the end of the list;
  - "Move up" on the last moves it one up;
  - one of A's pages cleared: A's box is part-checked, the page out of the
    list;
  - "Export PDF...": a file is asked for, written, with a page for each page
    chosen and the banner page;
  - opened again: the same pages checked, in the same order.

Runs with QT_QPA_PLATFORM=offscreen; the file dialog is FreeCAD's own.
"""
import os
import re
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PDF = os.path.join(OUT, "chosen.pdf")

state = {"done": False}


def note(msg):
    with open(RESULT, "a", encoding="utf-8") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=300):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def dialog():
    for w in QtWidgets.QApplication.topLevelWidgets():
        if isinstance(w, QtWidgets.QDialog) and w.isVisible() \
                and w.metaObject().className() == "TechDrawGui::DlgPrintPages":
            return w
    return None


def parts(dlg):
    tree = dlg.findChild(QtWidgets.QTreeWidget)
    order = dlg.findChild(QtWidgets.QListWidget)
    buttons = {b.text().replace("&", ""): b for b in dlg.findChildren(QtWidgets.QPushButton)}
    return tree, order, buttons


def tree_state(tree):
    docs = []
    for i in range(tree.topLevelItemCount()):
        item = tree.topLevelItem(i)
        pages = [(item.child(j).text(0), item.child(j).checkState(0) == QtCore.Qt.Checked)
                 for j in range(item.childCount())]
        docs.append((item.text(0), item.isExpanded(), item.checkState(0), pages))
    return docs


def order_texts(order):
    return [order.item(i).text() for i in range(order.count())]


def when_dialog(act):
    """Run `act(dialog)` once the dialog is up"""
    def poll(tries=[0]):
        dlg = dialog()
        if dlg is not None:
            try:
                act(dlg)
            except Exception:
                note("ABORT in the dialog " + traceback.format_exc().replace("\n", " | "))
                dlg.reject()
            return
        tries[0] += 1
        if tries[0] < 50:
            QtCore.QTimer.singleShot(100, poll)

    QtCore.QTimer.singleShot(200, poll)


def answer_file_dialog(path):
    def poll(tries=[0]):
        for w in QtWidgets.QApplication.topLevelWidgets():
            if isinstance(w, QtWidgets.QFileDialog) and w.isVisible():
                note("INFO the file dialog: %s" % w.windowTitle())
                # the name typed where a hand types it; FreeCAD's dialog reads
                # its line edit (selectFile and accept left it open)
                edit = w.findChild(QtWidgets.QLineEdit, "fileNameEdit")
                if edit is not None:
                    edit.setText(path)
                w.done(QtWidgets.QDialog.Accepted)
                return
        tries[0] += 1
        if tries[0] < 50:
            QtCore.QTimer.singleShot(100, poll)

    QtCore.QTimer.singleShot(200, poll)


def make(name, label, pages):
    doc = FreeCAD.newDocument(name)
    doc.Label = label
    for i, page_label in enumerate(pages):
        page = doc.addObject("TechDraw::DrawPage", "Page%d" % i)
        page.Label = page_label
        template = doc.addObject("TechDraw::DrawSVGTemplate", "Template%d" % i)
        template.Template = os.path.join(FreeCAD.getResourceDir(), "Mod", "TechDraw",
                                         "Templates", "A4_Landscape_blank.svg")
        page.Template = template
    doc.recompute()
    return doc


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Dialog").SetBool(
            "DontUseNativeDialog", True)
        FreeCADGui.activateWorkbench("TechDrawWorkbench")
        settle(800)
        make("PrintB", "Drawings B", ["Sheet B1", "Sheet B2"])
        make("PrintNone", "No drawings", [])
        a = make("PrintA", "Drawings A", ["Sheet A1", "Sheet A2", "Sheet A3"])
        FreeCAD.setActiveDocument(a.Name)
        FreeCADGui.setActiveDocument(a.Name)
        settle(1000)

        cmd = FreeCADGui.Command.get("TechDraw_Print")
        info = cmd.getInfo() if cmd else {}
        note("INFO TechDraw_Print: %s" % info)
        check("TechDraw_Print is there, 'Print pages', the printer's icon",
              cmd is not None and info.get("menuText") == "Print pages"
              and info.get("pixmap") == "document-print", info)
        check("its tool tip names the PDF", "PDF" in info.get("toolTip", ""), info.get("toolTip"))
        check("TechDraw_PrintAll and TechDraw_PrintSelected are gone",
              FreeCADGui.Command.get("TechDraw_PrintAll") is None
              and FreeCADGui.Command.get("TechDraw_PrintSelected") is None)

        seen = {}

        def first(dlg):
            tree, order, buttons = parts(dlg)
            docs = tree_state(tree)
            seen["first"] = docs
            note("INFO the tree: %s" % docs)
            note("INFO the order: %s" % order_texts(order))
            names = [d[0] for d in docs]
            check("the tree lists A and B, not the document with no page",
                  sorted(names) == ["Drawings A", "Drawings B"], names)
            da = [d for d in docs if d[0] == "Drawings A"]
            db = [d for d in docs if d[0] == "Drawings B"]
            check("A expanded with all its pages checked, B closed with none",
                  da and db and da[0][1] and not db[0][1]
                  and all(c for _, c in da[0][3]) and not any(c for _, c in db[0][3]), docs)
            check("the order list: A's pages as document#page",
                  order_texts(order) == ["Drawings A#Sheet A1", "Drawings A#Sheet A2",
                                         "Drawings A#Sheet A3"], order_texts(order))
            check("the order list can be dragged",
                  order.dragDropMode() == QtWidgets.QAbstractItemView.InternalMove)

            b = [tree.topLevelItem(i) for i in range(tree.topLevelItemCount())
                 if tree.topLevelItem(i).text(0) == "Drawings B"][0]
            b.setCheckState(0, QtCore.Qt.Checked)
            QtWidgets.QApplication.processEvents()
            check("B's box checks both its pages, at the end of the list",
                  order_texts(order)[-2:] == ["Drawings B#Sheet B1", "Drawings B#Sheet B2"]
                  and len(order_texts(order)) == 5, order_texts(order))

            order.setCurrentRow(order.count() - 1)
            buttons["Move up"].click()
            QtWidgets.QApplication.processEvents()
            check("'Move up' moves the last one up",
                  order_texts(order)[-2:] == ["Drawings B#Sheet B2", "Drawings B#Sheet B1"],
                  order_texts(order))

            a_item = [tree.topLevelItem(i) for i in range(tree.topLevelItemCount())
                      if tree.topLevelItem(i).text(0) == "Drawings A"][0]
            a_item.child(1).setCheckState(0, QtCore.Qt.Unchecked)
            QtWidgets.QApplication.processEvents()
            check("A's second page cleared: A's box part-checked, the page out of the list",
                  a_item.checkState(0) == QtCore.Qt.PartiallyChecked
                  and "Drawings A#Sheet A2" not in order_texts(order), order_texts(order))
            seen["order"] = order_texts(order)

            answer_file_dialog(PDF)
            buttons["Export PDF..."].click()

        when_dialog(first)
        FreeCADGui.runCommand("TechDraw_Print")
        settle(1500)

        size = os.path.getsize(PDF) if os.path.exists(PDF) else -1
        pages = -1
        if size > 0:
            with open(PDF, "rb") as f:
                pages = len(re.findall(rb"/Type\s*/Page[^s]", f.read()))
        check("'Export PDF...' wrote the file: a page for each of the four chosen and the banner",
              pages == 5, "%d bytes, %d pages" % (size, pages))

        def second(dlg):
            tree, order, buttons = parts(dlg)
            note("INFO opened again, the order: %s" % order_texts(order))
            check("opened again: the same pages, in the same order",
                  order_texts(order) == seen.get("order"), (order_texts(order), seen.get("order")))
            dlg.reject()

        when_dialog(second)
        FreeCADGui.runCommand("TechDraw_Print")
        settle(500)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        finish()


def start_sweeper(seconds=60):
    """Every second: what top-level windows are up, in the log; a message box
    or a menu left open is closed, so that a step waiting on it goes on"""
    ticks = {"n": 0}

    def sweep():
        ticks["n"] += 1
        shown = []
        for w in QtWidgets.QApplication.topLevelWidgets():
            if w.isVisible() and w is not FreeCADGui.getMainWindow():
                shown.append("%s '%s'" % (w.metaObject().className(), w.windowTitle()))
                if isinstance(w, (QtWidgets.QMessageBox, QtWidgets.QMenu)):
                    note("INFO sweeper closes %s '%s': %s" % (
                        w.metaObject().className(), w.windowTitle(),
                        w.text() if isinstance(w, QtWidgets.QMessageBox) else ""))
                    w.close()
        if shown:
            note("INFO sweeper sees: %s" % shown)
        if ticks["n"] < seconds:
            QtCore.QTimer.singleShot(1000, sweep)

    QtCore.QTimer.singleShot(1000, sweep)


def finish():
    if state["done"]:
        return
    state["done"] = True
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


start_sweeper()
QtCore.QTimer.singleShot(3000, run)
