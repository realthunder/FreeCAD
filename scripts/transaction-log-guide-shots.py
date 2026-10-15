# The pictures of docs/TransactionLogGuide.md, taken from the running
# application: a small document is made, edited, branched and merged, and the
# transaction log panel is driven through what the guide describes.
#
# The offscreen platform draws nothing; run it under a display:
#
#   cd build/conda-relwithdebinfo-801
#   GUIDE_OUT=$PWD/../../docs/images/transaction-log \
#     FREECAD_USER_HOME=/some/fresh/dir XDG_CACHE_HOME=/some/fresh/dir \
#     xvfb-run -a -s "-screen 0 1500x1000x24" \
#     ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD ../../scripts/transaction-log-guide-shots.py
#
# A fresh home and cache each time: a run ends in os._exit(), and the next
# one in the same cache is offered the document to recover, over the pictures.
# What was taken is listed in $GUIDE_OUT/shots.txt.
import os
import tempfile
import time
import traceback

import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ.get(
    "GUIDE_OUT",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "images",
                 "transaction-log"),
)
said = []


def settle(n=30):
    for _ in range(n):
        QtWidgets.QApplication.processEvents()


def wait(seconds):
    until = time.time() + seconds
    while time.time() < until:
        settle(5)
        time.sleep(0.02)


def rectOf(*widgets, pad=3):
    """The part of the screen the widgets cover together."""
    rect = QtCore.QRect()
    for w in widgets:
        rect = rect.united(QtCore.QRect(w.mapToGlobal(QtCore.QPoint(0, 0)), w.size()))
    return rect.adjusted(-pad, -pad, pad, pad)


def shot(name, *widgets):
    """The screen, or the part of it the widgets cover."""
    settle()
    Gui.updateGui()
    settle()
    pix = QtGui.QGuiApplication.primaryScreen().grabWindow(0)
    if widgets:
        pix = pix.copy(rectOf(*widgets).intersected(pix.rect()))
    path = os.path.join(OUT, name + ".png")
    pix.save(path)
    try:
        # A third of the size, for pictures kept in the repository
        from PIL import Image

        Image.open(path).convert("RGB").quantize(colors=256).save(path, optimize=True)
    except Exception:
        pass
    said.append("%s %dx%d" % (name, pix.width(), pix.height()))


def shotWidget(name, widget):
    """A window as it draws itself: no other window's pixels at its edges."""
    settle()
    path = os.path.join(OUT, name + ".png")
    pix = widget.grab()
    pix.save(path)
    try:
        from PIL import Image

        Image.open(path).convert("RGB").quantize(colors=256).save(path, optimize=True)
    except Exception:
        pass
    said.append("%s %dx%d" % (name, pix.width(), pix.height()))


def part(of, name):
    for w in of.findChildren(QtWidgets.QWidget):
        if w.objectName() == name:
            return w
    return None


def column(tree, title):
    header = tree.headerItem()
    return [c for c in range(header.columnCount()) if header.text(c) == title][0]


def rowNamed(tree, name):
    col = column(tree, "Name")
    for i in range(tree.topLevelItemCount()):
        if tree.topLevelItem(i).text(col) == name:
            return tree.topLevelItem(i)
    return None


def panel():
    mw = Gui.getMainWindow()
    dock = None
    for w in mw.findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "Gui::DockWnd::TransactionLogView":
            dock = w
    # The other panels out of the way: the tree stays, to pick objects in
    for d in mw.findChildren(QtWidgets.QDockWidget):
        title = d.windowTitle()
        if d is dock.parentWidget() or "Tree" in title or "Model" in title:
            continue
        d.hide()
    parent = dock.parentWidget()
    if isinstance(parent, QtWidgets.QDockWidget):
        parent.show()
        parent.raise_()
        mw.resizeDocks([parent], [450], QtCore.Qt.Vertical)
    dock.show()
    settle()
    trees = {}
    for t in dock.findChildren(QtWidgets.QTreeWidget):
        head = t.headerItem()
        if head.text(1) == "Seq":
            trees["rows"] = t
        elif head.text(1) == "Op":
            trees["ops"] = t
        elif head.text(0) == "Num":
            trees["versions"] = t
    trees["changes"] = part(dock, "TransactionVersionChanges")
    trees["kept"] = part(dock, "TransactionKept")
    return dock, trees


def withMenu(name, tree, item, *widgets):
    """The context menu of an item, pictured while it is open."""

    def grab():
        menu = QtWidgets.QApplication.activePopupWidget()
        if menu is not None:
            shot(name, *(list(widgets) + [menu]))
            menu.close()
        else:
            said.append("%s: no menu" % name)

    QtCore.QTimer.singleShot(500, grab)
    tree.customContextMenuRequested.emit(tree.visualItemRect(item).center())
    settle()


def mergeDialog():
    for w in QtWidgets.QApplication.topLevelWidgets():
        if w.objectName() == "TransactionMergeDialog" and w.isVisible():
            return w
    return None


def driveMerge(doc, mw):
    try:
        dialog = mergeDialog()
        if dialog is None:
            said.append("merge: no dialog")
            return
        dialog.move(500, 585)
        dialog.resize(990, 400)
        settle()
        tree = dialog.findChild(QtWidgets.QTreeWidget)
        # The three values narrow, for the side picked to be in the picture
        for col in (3, 4, 5):
            tree.setColumnWidth(col, 200)
        bar = part(dialog, "TransactionDiffBar")
        Gui.Selection.addSelection(doc.getObject("Cut2"))
        part(bar, "diffAdd").click()
        settle()
        shot("11-merge-dialog")
        for i in range(tree.topLevelItemCount()):
            item = tree.topLevelItem(i)
            if item.text(1) == "Clamp" and item.text(2) == "Shape":
                tree.setCurrentItem(item)
        settle()
        shot("12-merge-one-change")
        tree.clearSelection()
        # The document changed under the preview
        doc.openTransaction("main: clamp wider")
        doc.getObject("Clamp").Height = 16
        doc.recompute()
        doc.commitTransaction()
        wait(0.8)
        shot("13-merge-stale", dialog)
        dialog.reject()
    except Exception:
        said.append(traceback.format_exc())
        dialog = mergeDialog()
        if dialog is not None:
            dialog.reject()


def run():
    try:
        os.makedirs(OUT, exist_ok=True)
        p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
        p.SetInt("TransactionLog", 2)
        mw = Gui.getMainWindow()
        mw.setGeometry(0, 0, 1500, 1000)
        settle()

        doc = App.newDocument("Bracket")
        doc.UndoMode = 1
        doc.openTransaction("make the bracket")
        box = doc.addObject("Part::Box", "Box")
        box.Length, box.Width, box.Height = 40, 20, 10
        cyl = doc.addObject("Part::Cylinder", "Cyl")
        cyl.Radius, cyl.Height = 3, 60
        cyl.Placement.Base = App.Vector(10, 10, -20)
        cut = doc.addObject("Part::Cut", "Cut")
        cut.Base, cut.Tool = box, cyl
        cyl2 = doc.addObject("Part::Cylinder", "Cyl2")
        cyl2.Radius, cyl2.Height = 3, 60
        cyl2.Placement.Base = App.Vector(30, 10, -20)
        cut2 = doc.addObject("Part::Cut", "Cut2")
        cut2.Base, cut2.Tool = cut, cyl2
        clamp = doc.addObject("Part::Box", "Clamp")
        clamp.Length, clamp.Width, clamp.Height = 12, 12, 12
        clamp.Placement.Base = App.Vector(55, 4, 0)
        doc.recompute()
        doc.commitTransaction()
        doc.saveAs(os.path.join(tempfile.mkdtemp(prefix="fc-guide-"), "bracket.FCStd"))
        doc.openTransaction("taller")
        box.Height = 14
        doc.recompute()
        doc.commitTransaction()
        doc.openTransaction("second hole moved")
        cyl2.Placement.Base = App.Vector(26, 12, -20)
        doc.recompute()
        doc.commitTransaction()
        released = doc.snapshotTransactionLog()
        doc.nameTransactionVersion(released, "released")
        doc.openTransaction("taller still")
        box.Height = 18
        doc.recompute()
        doc.commitTransaction()
        settle()

        view = Gui.ActiveDocument.ActiveView
        dock, trees = panel()
        rows, ops = trees["rows"], trees["ops"]
        wait(0.5)
        view.viewIsometric()
        view.fitAll()
        wait(0.5)
        tabs = dock.findChild(QtWidgets.QTabWidget)
        bar = part(dock, "TransactionDiffBar")
        mdi = mw.centralWidget()
        frame = dock.parentWidget() if isinstance(dock.parentWidget(), QtWidgets.QDockWidget) \
            else dock

        # 01: the panel, a row selected and one of its ops
        rows.setCurrentItem(rowNamed(rows, "taller"))
        settle()
        prop = column(ops, "Property")
        for i in range(ops.topLevelItemCount()):
            if ops.topLevelItem(i).text(prop) == "Height":
                ops.setCurrentItem(ops.topLevelItem(i))
        settle()
        shot("01-panel")
        shot("02-bars", part(dock, "qt_spinbox_lineedit") or dock.findChild(QtWidgets.QLineEdit),
             dock.findChild(QtWidgets.QComboBox), part(dock, "TransactionStatus") or
             dock.findChildren(QtWidgets.QCheckBox)[0])
        shot("03-rows-and-ops", tabs, ops, dock.findChild(QtWidgets.QPlainTextEdit))

        # 04: the row's menu
        withMenu("04-row-menu", rows, rowNamed(rows, "taller"), tabs)

        # 05: an op that is a shape, in the view
        for i in range(ops.topLevelItemCount()):
            item = ops.topLevelItem(i)
            if item.text(prop) == "Shape" and str(cut2.ID) in item.text(column(ops, "Container")):
                ops.setCurrentItem(item)
        settle()
        shot("05-op-in-the-view", mdi, frame)
        part(bar, "diffPosition").setValue(0)
        shot("06-slider-at-before", mdi, bar)
        part(bar, "diffPosition").setValue(100)
        # The same by its faces
        part(bar, "diffMode").setCurrentIndex(1)
        settle()
        shot("06-by-the-faces", mdi, bar)
        part(bar, "diffMode").setCurrentIndex(0)
        settle()

        # 07: the whole of a row, and the objects chosen
        ops.clearSelection()
        settle()
        shot("07-nothing-chosen", bar)
        Gui.Selection.addSelection(cut2)
        settle()
        part(bar, "diffAdd").click()
        settle()
        Gui.Selection.addSelection(clamp)
        part(bar, "diffAdd").click()
        settle()
        rows.setCurrentItem(rowNamed(rows, "second hole moved"))
        settle()
        shot("08-objects-chosen", mdi, frame)

        # 09: the op's menu -- kept in the file
        rows.setCurrentItem(rowNamed(rows, "taller"))
        settle()
        shape = None
        for i in range(ops.topLevelItemCount()):
            item = ops.topLevelItem(i)
            if item.text(prop) == "Shape" and str(cut2.ID) in item.text(column(ops, "Container")):
                shape = item
        ops.setCurrentItem(shape)
        settle()
        withMenu("09-op-menu", ops, shape, ops)
        taller = [r for r in doc.getTransactionLog() if r["name"] == "taller"][-1]["seq"]
        doc.keepTransactionValue(cut2, "Shape", taller)
        doc.keepTransactionValuesOf(cut)

        # 10: the versions
        tabs.setCurrentIndex(1)
        settle()
        versions = trees["versions"]
        named = None
        for i in range(versions.topLevelItemCount()):
            if versions.topLevelItem(i).text(column(versions, "Name")) == "released":
                named = versions.topLevelItem(i)
        versions.setCurrentItem(named)
        settle()
        shot("10-versions", mdi, frame)
        withMenu("10-version-menu", versions, named, tabs)
        changes = trees["changes"]
        for i in range(changes.topLevelItemCount()):
            item = changes.topLevelItem(i)
            if item.text(0).endswith(" Box") and item.text(1) == "Height":
                changes.setCurrentItem(item)
        settle()
        shot("10-version-change", tabs, changes.parentWidget().parentWidget(),
             dock.findChild(QtWidgets.QPlainTextEdit))
        changes.clearSelection()

        # The kept
        tabs.setCurrentIndex(2)
        settle()
        kept = trees["kept"]
        kept.setCurrentItem(kept.topLevelItem(0))
        settle()
        shot("14-kept", mdi, frame)
        tabs.setCurrentIndex(0)
        settle()
        for name in ("diffRemove1", "diffRemove0"):
            if part(bar, name):
                part(bar, name).click()
                settle()

        # Branches, and a merge with a conflict
        doc.createTransactionBranch("side")
        doc.openTransaction("side: longer")
        doc.getObject("Box").Length = 46
        doc.recompute()
        doc.commitTransaction()
        doc.openTransaction("side: clamp taller")
        doc.getObject("Clamp").Height = 20
        doc.recompute()
        doc.commitTransaction()
        doc.switchTransactionBranch("main")
        settle()
        doc.openTransaction("main: longer")
        doc.getObject("Box").Length = 44
        doc.recompute()
        doc.commitTransaction()
        settle()
        for box2 in dock.findChildren(QtWidgets.QCheckBox):
            if box2.text() == "All branches":
                box2.setChecked(True)
        settle()
        rows.setCurrentItem(rowNamed(rows, "side: clamp taller"))
        settle()
        Gui.ActiveDocument.ActiveView.fitAll()
        shot("15-branches", tabs)
        withMenu("16-other-branch-row-menu", rows, rowNamed(rows, "side: clamp taller"), tabs)

        QtCore.QTimer.singleShot(900, lambda: driveMerge(doc, mw))
        QtCore.QMetaObject.invokeMethod(dock, "mergeBranch", QtCore.Qt.DirectConnection,
                                        QtCore.Q_ARG(str, "side"))
        settle()

        # 17: who may what of a branch, and a merge someone asked for
        # (docs/TransactionLog.md sec 32.25). Two who came to the document
        # and wrote a row each are users the log knows.
        for kind, name, prop, value in (("invited", "bob", "Width", 22),
                                        ("verified", "ann@example.com", "Height", 19)):
            doc.pushTransactionActor(kind, name)
            try:
                doc.openTransaction("%s: %s" % (name, prop.lower()))
                setattr(box, prop, value)
                doc.recompute()
                doc.commitTransaction()
            finally:
                doc.popTransactionActor()
        doc.setTransactionAccess("invited", "bob", "side")
        doc.setTransactionAccess("verified", "ann@example.com", "main", True, released)
        doc.pushTransactionActor("invited", "bob")
        try:
            doc.requestTransactionMerge("side", "main", "the longer box, please")
        finally:
            doc.popTransactionActor()
        settle()
        wait(0.5)

        def grabAccess():
            dialog = QtWidgets.QApplication.activeModalWidget()
            if dialog is None or dialog.objectName() != "TransactionAccessDialog":
                said.append("access: no dialog")
                if dialog is not None:
                    dialog.reject()
                return
            dialog.setGeometry(420, 300, 660, 300)
            level = part(dialog, "TransactionAccessLevel")
            level.setCurrentIndex(1)
            where = part(dialog, "TransactionAccessBranch")
            where.setCurrentIndex(where.findText("main"))
            start = part(dialog, "TransactionAccessFrom")
            start.setCurrentIndex(start.findData(released))
            wait(0.5)
            shotWidget("17-access", dialog)
            dialog.accept()

        QtCore.QTimer.singleShot(700, grabAccess)
        part(dock, "TransactionAccess").click()
        settle()
        requests = part(dock, "TransactionRequests")
        asked = None
        for i in range(requests.topLevelItemCount()):
            if requests.topLevelItem(i).text(0) == "merge side into main":
                asked = requests.topLevelItem(i)
        if asked is not None:
            # The panel put back at the bottom of the window: the merge's
            # preview, beside it, leaves it out of the window's area
            if isinstance(dock.parentWidget(), QtWidgets.QDockWidget):
                mw.addDockWidget(QtCore.Qt.BottomDockWidgetArea, dock.parentWidget())
            panel()
            wait(0.5)
            requests.setCurrentItem(asked)
            withMenu("17-asked-merge-menu", requests, asked, tabs, requests)
        else:
            said.append("asked merge: not listed")

        # 18: a branch served in a document of its own, which has no window:
        # closing the last window that has one says so (sec 32.30)
        for name in App.listDocuments():
            if name != doc.Name:
                App.closeDocument(name)
        settle()
        if not Gui.serveDocument(doc, 0):
            said.append("hidden: not served")
        Gui.serveBranch(doc, "for-ann")
        Gui.getDocument(doc.Name).save()
        settle()
        wait(0.5)
        tries = [0]

        def grabHidden():
            box = QtWidgets.QApplication.activeModalWidget()
            if box is None and tries[0] < 20:
                tries[0] += 1
                QtCore.QTimer.singleShot(200, grabHidden)
                return
            if box is None or box.objectName() != "HiddenDocuments":
                said.append("hidden: no question (%r)" % (box and box.objectName(),))
                if box is not None:
                    box.reject()
                return
            shotWidget("18-hidden-close", box)
            box.reject()

        QtCore.QTimer.singleShot(300, grabHidden)
        area = mw.findChild(QtWidgets.QMdiArea)
        for sub in area.subWindowList():
            if sub.windowTitle().startswith(doc.Label):
                sub.close()
                break
        settle()
    except Exception:
        said.append(traceback.format_exc())
    with open(os.path.join(OUT, "shots.txt"), "w") as f:
        f.write("\n".join(said) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(1500, run)
