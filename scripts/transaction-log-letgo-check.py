# GUI check of docs/TransactionLog.md sec 32.33: a shape the log let go -- a
# value a recompute wrote is a cache, which a file's copy of the log does not
# carry, so a file opened again has none of its rows' shapes -- shown all the
# same, read from that state's own document: a row's before against after,
# an op of it, and the two sides of a merge. The states are closed when
# nothing shows them, and nothing of the document moves.
# One GUI run in a fresh user home, given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-lg \
#     LETGOCHECK_OUT=/tmp/lg/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-letgo-check.py
#
# It writes PASS/FAIL lines to $LETGOCHECK_OUT and exits. The log setting is
# put back before it exits.
import math, os, shutil, tempfile, traceback
import FreeCAD as App
import FreeCADGui as Gui
import Part
from PySide import QtCore, QtWidgets

OUT = os.environ["LETGOCHECK_OUT"]
lines = []


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def settle():
    for _ in range(5):
        QtWidgets.QApplication.processEvents()


def panel():
    dock = None
    for w in Gui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "Gui::DockWnd::TransactionLogView":
            dock = w
    if not dock:
        return None, None, None
    parent = dock.parentWidget()
    if isinstance(parent, QtWidgets.QDockWidget):
        parent.show()
    dock.show()
    settle()
    rows = ops = None
    for t in dock.findChildren(QtWidgets.QTreeWidget):
        if t.headerItem().text(1) == "Seq":
            rows = t
        elif t.headerItem().text(1) == "Op":
            ops = t
    return dock, rows, ops


def column(tree, title):
    header = tree.headerItem()
    for c in range(header.columnCount()):
        if header.text(c) == title:
            return c
    return -1


def part(of, name):
    for w in of.findChildren(QtWidgets.QWidget):
        if w.objectName() == name:
            return w
    return None


def bar(parent):
    return part(parent, "TransactionDiffBar")


def states():
    """The documents of a row's state open now."""
    return sorted(n for n in App.listDocuments() if "_row" in n)


def mergeDialog():
    for w in QtWidgets.QApplication.topLevelWidgets():
        if w.objectName() == "TransactionMergeDialog" and w.isVisible():
            return w
    return QtWidgets.QApplication.activeModalWidget()


def drive(seen, cut):
    # The panel waits for the merge dialog in a loop of its own: this runs
    # inside it.
    dialog = mergeDialog()
    if dialog is None or dialog.objectName() != "TransactionMergeDialog":
        seen["found"] = False
        if dialog is not None:
            dialog.reject()
        return
    seen["found"] = True
    try:
        tree = dialog.findChild(QtWidgets.QTreeWidget)
        diff = bar(dialog)
        for i in range(tree.topLevelItemCount()):
            item = tree.topLevelItem(i)
            if (item.text(0).startswith("derived") and item.text(1) == "Cut"
                    and item.text(2) == "Shape"):
                tree.setCurrentItem(item)
        settle()
        pair = part(diff, "diffPair")
        for index, key in ((0, "ours theirs"), (1, "base ours"), (2, "base theirs")):
            pair.setCurrentIndex(index)
            settle()
            seen[key] = (cut.ViewObject.getValueDiff(), part(diff, "diffStatus").text(),
                         states())
        pair.setCurrentIndex(0)
        settle()
    except Exception:
        seen["error"] = traceback.format_exc()
    dialog.reject()


def run():
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 2)
    where = tempfile.mkdtemp(prefix="fc-letgo-check-")
    try:
        p.SetInt("TransactionLog", 2)
        doc = App.newDocument("LetGo")
        doc.UndoMode = 1
        doc.openTransaction("base")
        box = doc.addObject("Part::Box", "Box")
        cyl = doc.addObject("Part::Cylinder", "Cyl")
        cyl.Radius, cyl.Height = 2, 30
        cyl.Placement.Base = App.Vector(5, 5, -10)
        cut = doc.addObject("Part::Cut", "Cut")
        cut.Base, cut.Tool = box, cyl
        doc.recompute()
        doc.commitTransaction()
        doc.saveAs(os.path.join(where, "letgo.FCStd"))
        doc.openTransaction("taller")
        box.Height = 12
        doc.recompute()
        doc.commitTransaction()
        doc.createTransactionBranch("side")
        doc.openTransaction("side: longer")
        box.Length = 14
        doc.recompute()
        doc.commitTransaction()
        doc.switchTransactionBranch("main")
        doc.openTransaction("main: wider")
        doc.getObject("Box").Width = 11
        doc.recompute()
        doc.commitTransaction()
        path = doc.FileName
        Gui.getDocument(doc.Name).save()
        App.closeDocument(doc.Name)
        settle()

        doc = App.openDocument(path)
        settle()
        cut = doc.getObject("Cut")
        view = cut.ViewObject

        def row(name):
            return [r for r in doc.getTransactionLog() if r["name"] == name][-1]

        taller = [o for o in doc.getTransactionOps(row("taller")["seq"])
                  if o["prop"] == "Shape" and o["cid"] == cut.ID][0]
        check("the file opened again: its rows' shapes are not in the log",
              Part.getTransactionShape(doc, taller["after"], cut.ID) is None
              and Part.getTransactionShape(doc, taller["before"], cut.ID) is None)
        logged = len(doc.getTransactionLog())

        dock, tree, ops = panel()
        diff = bar(dock) if dock is not None else None
        check("panel found, with the bar", tree is not None and ops is not None
              and diff is not None)
        if diff is None:
            return
        name = column(tree, "Name")

        def select(which):
            tree.clearSelection()
            for i in range(tree.topLevelItemCount()):
                if tree.topLevelItem(i).text(name) == which:
                    tree.setCurrentItem(tree.topLevelItem(i))
            settle()

        # -- A row, its before against its after ------------------------
        select("taller")
        Gui.Selection.addSelection(cut)
        part(diff, "diffAdd").click()
        settle()
        shown = view.getValueDiff()
        said = part(diff, "diffStatus").text()
        # Two millimetres more of a ten by ten box with a hole of radius 2
        slab = 2 * (100 - math.pi * 4)
        check("a row of the file opened again, by the material: the slab the row added "
              "(%r, %r)" % (shown.get("volume.added"), said),
              abs(shown.get("volume.added", 0) - slab) < 1e-6
              and shown.get("volume.removed", 1) < 1e-6
              and "2 shape(s) the log let go, computed again at their row" in said)
        opened = states()
        check("read from the two states, the row's and its parent's (%r)" % (opened,),
              len(opened) == 2)
        part(diff, "diffMode").setCurrentIndex(1)
        settle()
        shown = view.getValueDiff()
        check("and by the faces, every one paired by its name (%r)" % (shown,),
              shown.get("by.name") == 7 and shown.get("changed", 0) + shown.get("moved", 0) > 0)
        check("the states stay while the same two are shown (%r)" % (states(),),
              states() == opened)
        part(diff, "diffMode").setCurrentIndex(0)
        settle()

        # -- An op of the row ---------------------------------------------
        prop = column(ops, "Property")
        before = column(ops, "Before")
        found = None
        for i in range(ops.topLevelItemCount()):
            item = ops.topLevelItem(i)
            if item.text(prop) == "Shape" and taller["before"].startswith(item.text(before)):
                found = item
        if found is not None:
            ops.setCurrentItem(found)
            settle()
        shown = view.getValueDiff()
        check("its op selected: the same slab (%r)" % (shown.get("volume.added"),),
              found is not None and abs(shown.get("volume.added", 0) - slab) < 1e-6)

        # -- The row that made it: a version holds that one ----------------
        select("base")
        shown = view.getValueDiff()
        said = part(diff, "diffStatus").text()
        check("the row that made the cut, which the file was saved at: all of it new, "
              "read from the log (%r)" % (said,),
              shown.get("added", 0) == 7 and "let go" not in said)
        check("and the states of the row before are closed (%r -> %r)" % (opened, states()),
              states() == [])

        select("none such")
        check("nothing selected: no state left open (%r)" % (states(),), states() == [])
        check("showing wrote no row", len(doc.getTransactionLog()) == logged)
        Gui.Selection.clearSelection()

        # -- The merge dialog's sides -------------------------------------
        seen = {}
        QtCore.QTimer.singleShot(300, lambda: drive(seen, cut))
        QtCore.QMetaObject.invokeMethod(
            dock, "mergeBranch", QtCore.Qt.DirectConnection, QtCore.Q_ARG(str, "side"))
        settle()
        check("merge dialog driven (%r)" % (seen.get("error"),),
              seen.get("found") is True and "error" not in seen)
        for key, count in (("ours theirs", 1), ("base ours", 1), ("base theirs", 2)):
            shown, said, opened = seen.get(key) or ({}, "", [])
            check("merge, %s: shown, the log's let go computed again (%r, %r)"
                  % (key, said, opened),
                  shown != {} and "%d shape(s) the log let go, computed again" % count in said
                  and len(opened) == count)
        settle()
        check("the dialog closed: no state left open (%r)" % (states(),), states() == [])
        check("nothing merged, no row written", len(doc.getTransactionLog()) == logged)
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        p.SetInt("TransactionLog", mode)
        App.saveParameter()
        shutil.rmtree(where, ignore_errors=True)
        with open(OUT, "w") as f:
            f.write("\n".join(lines) + "\n")
        os._exit(0)


QtCore.QTimer.singleShot(0, run)
