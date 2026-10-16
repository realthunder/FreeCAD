# GUI check of docs/TransactionLog.md sec 32.34, the merge dialog's result:
# what the merge removes is drawn over its object as gone -- theirs removing
# it, and the result without it -- and a side picked again makes the result
# again in the document it was made in, which is kept while the sides change
# and closed with the dialog.
# One GUI run in a fresh user home, given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-mr \
#     MERGERESULTCHECK_OUT=/tmp/mr/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-mergeresult-check.py
#
# It writes PASS/FAIL lines to $MERGERESULTCHECK_OUT and exits. The settings
# it changes are put back before it exits.
import math, os, shutil, tempfile, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["MERGERESULTCHECK_OUT"]
lines = []


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def settle():
    for _ in range(5):
        QtWidgets.QApplication.processEvents()


def panel():
    for w in Gui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "Gui::DockWnd::TransactionLogView":
            return w
    return None


def part(of, name):
    for w in of.findChildren(QtWidgets.QWidget):
        if w.objectName() == name:
            return w
    return None


def results():
    return sorted(n for n in App.listDocuments() if "_merged" in n)


def mergeDialog():
    for w in QtWidgets.QApplication.topLevelWidgets():
        if w.objectName() == "TransactionMergeDialog" and w.isVisible():
            return w
    return QtWidgets.QApplication.activeModalWidget()


def drive(seen, doc):
    dialog = mergeDialog()
    if dialog is None or dialog.objectName() != "TransactionMergeDialog":
        seen["found"] = False
        if dialog is not None:
            dialog.reject()
        return
    seen["found"] = True
    try:
        tree = dialog.findChild(QtWidgets.QTreeWidget)
        diff = part(dialog, "TransactionDiffBar")
        pair = part(diff, "diffPair")
        said = part(diff, "diffStatus")
        cone, cut = doc.getObject("Cone"), doc.getObject("Cut")
        sides = [c for c in tree.findChildren(QtWidgets.QComboBox)
                 if [c.itemText(i) for i in range(c.count())] == ["ours", "theirs"]]

        def select(obj, prop):
            tree.clearSelection()
            for i in range(tree.topLevelItemCount()):
                item = tree.topLevelItem(i)
                if item.text(1) == obj and item.text(2) == prop:
                    tree.setCurrentItem(item)
            settle()

        # -- What theirs removes ------------------------------------------
        select("Cone", "")
        seen["line"] = [tree.topLevelItem(i).text(0) for i in range(tree.topLevelItemCount())
                        if tree.topLevelItem(i).text(1) == "Cone"]
        pair.setCurrentIndex(0)
        settle()
        seen["gone theirs"] = (cone.ViewObject.getValueDiff(), said.text())
        pair.setCurrentIndex(3)
        settle()
        seen["gone result"] = (cone.ViewObject.getValueDiff(), said.text(), results())
        # With no line selected, the cone chosen with the cut
        tree.clearSelection()
        Gui.Selection.clearSelection()
        part(diff, "diffAdd").click()
        Gui.Selection.addSelection(cone)
        settle()
        seen["gone whole"] = (cone.ViewObject.getValueDiff(), said.text())

        # -- The side picked again: made in the same document --------------
        select("Cut", "Shape")
        pair.setCurrentIndex(3)
        settle()
        seen["theirs"] = (cut.ViewObject.getValueDiff(), results())
        for c in sides:
            c.setCurrentIndex(0)
        settle()
        seen["ours"] = (cut.ViewObject.getValueDiff(), results())
        # Changed while another pair is looked at: kept, and made when
        # its pair is chosen again
        pair.setCurrentIndex(0)
        settle()
        for c in sides:
            c.setCurrentIndex(1)
        settle()
        seen["kept"] = (results(), cut.ViewObject.getValueDiff().get("volume.added"))
        pair.setCurrentIndex(3)
        settle()
        seen["theirs again"] = (cut.ViewObject.getValueDiff(), results())
        # The ball theirs made, left out: not in the result
        for i in range(tree.topLevelItemCount()):
            item = tree.topLevelItem(i)
            if item.text(1) == "Ball" and item.text(2) == "":
                item.setCheckState(0, QtCore.Qt.Unchecked)
        settle()
        there = App.getDocument(results()[0]) if results() else None
        seen["left out"] = (there is not None and there.getObject("Ball") is None, results())
        pair.setCurrentIndex(0)
        settle()
    except Exception:
        seen["error"] = traceback.format_exc()
    dialog.reject()


def run():
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 2)
    side = p.GetInt("MergeConflictSide", 0)
    where = tempfile.mkdtemp(prefix="fc-mergeresult-check-")
    try:
        p.SetInt("TransactionLog", 2)
        p.SetInt("MergeConflictSide", 1)
        doc = App.newDocument("MergeResult")
        doc.UndoMode = 1
        doc.openTransaction("base")
        box = doc.addObject("Part::Box", "Box")
        cyl = doc.addObject("Part::Cylinder", "Cyl")
        cyl.Radius, cyl.Height = 2, 30
        cyl.Placement.Base = App.Vector(5, 5, -10)
        cut = doc.addObject("Part::Cut", "Cut")
        cut.Base, cut.Tool = box, cyl
        cone = doc.addObject("Part::Cone", "Cone")
        cone.Placement.Base = App.Vector(30, 0, 0)
        doc.recompute()
        doc.commitTransaction()
        doc.saveAs(os.path.join(where, "mergeresult.FCStd"))
        doc.createTransactionBranch("side")
        doc.openTransaction("side: longer, a ball, no cone")
        box.Length = 14
        ball = doc.addObject("Part::Sphere", "Ball")
        ball.Placement.Base = App.Vector(0, 30, 0)
        doc.removeObject("Cone")
        doc.recompute()
        doc.commitTransaction()
        doc.switchTransactionBranch("main")
        doc.openTransaction("main: shorter")
        doc.getObject("Box").Length = 8
        doc.recompute()
        doc.commitTransaction()
        rows = len(doc.getTransactionLog())
        dock = panel()
        check("panel found", dock is not None)
        if dock is None:
            return
        parent = dock.parentWidget()
        if isinstance(parent, QtWidgets.QDockWidget):
            parent.show()
        dock.show()
        settle()

        seen = {}
        QtCore.QTimer.singleShot(300, lambda: drive(seen, doc))
        QtCore.QMetaObject.invokeMethod(
            dock, "mergeBranch", QtCore.Qt.DirectConnection, QtCore.Q_ARG(str, "side"))
        settle()
        check("merge dialog driven (%r)" % (seen.get("error"),),
              seen.get("found") is True and "error" not in seen)
        check("theirs removes the cone: its line (%r)" % (seen.get("line"),),
              [t.split()[-1] for t in seen.get("line") or []] == ["remove"])
        cone_faces = 3
        shown, said = seen.get("gone theirs") or ({}, "")
        check("ours against theirs: the cone, every face of it gone (%r, %r)" % (shown, said),
              shown.get("removed") == cone_faces and shown.get("added", 0) == 0
              and shown.get("same", 0) == 0)
        shown, said, made = seen.get("gone result") or ({}, "", [])
        check("ours against the result: the cone gone there too (%r, %r)" % (shown, said),
              shown.get("removed") == cone_faces and len(made) == 1
              and "1 object(s) are removed" in said)
        shown, said = seen.get("gone whole") or ({}, "")
        check("no line selected, the cone chosen: drawn as gone (%r, %r)" % (shown, said),
              shown.get("removed") == cone_faces)

        # Theirs is 14 long, ours 8, both 10 x 10 with the hole through
        shown, made = seen.get("theirs") or ({}, [])
        check("the result with theirs' length (%r)" % (shown.get("volume.added"),),
              abs(shown.get("volume.added", 0) - (14 - 8) * 10 * 10) < 1e-3 and len(made) == 1)
        name = made[0] if made else None
        shown, made = seen.get("ours") or ({}, [])
        check("ours picked: made again in the same document (%r, %r)" % (made, name),
              made == [name] and shown.get("volume.added", -1) == 0
              and shown.get("volume.removed", -1) == 0)
        kept = seen.get("kept") or ([], None)
        check("a side changed under another pair: the document kept for it (%r)"
              % (kept,), kept[0] == [name])
        shown, made = seen.get("theirs again") or ({}, [])
        check("its pair chosen again: theirs' length, made there (%r, %r)"
              % (shown.get("volume.added"), made),
              abs(shown.get("volume.added", 0) - (14 - 8) * 10 * 10) < 1e-3 and made == [name])
        left = seen.get("left out") or (False, [])
        check("theirs' ball left out: not in the result, made in the same document (%r)"
              % (left,), left[0] is True and left[1] == [name])
        settle()
        check("the dialog closed: its result closed with it (%r)" % (results(),),
              results() == [])
        check("nothing merged, no row written",
              len(doc.getTransactionLog()) - rows in (0, 1)
              and doc.getObject("Cone") is not None and doc.getObject("Ball") is None)
        check("nothing left drawn over the cone",
              doc.getObject("Cone").ViewObject.getValueDiff() == {})
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        p.SetInt("TransactionLog", mode)
        p.SetInt("MergeConflictSide", side)
        App.saveParameter()
        shutil.rmtree(where, ignore_errors=True)
        with open(OUT, "w") as f:
            f.write("\n".join(lines) + "\n")
        os._exit(0)


QtCore.QTimer.singleShot(0, run)
