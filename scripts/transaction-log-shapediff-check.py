# GUI check of docs/TransactionLog.md sec 32.9: a shape at two states of the
# log, shown over its object in the 3D view -- asked of the view provider, from
# an op of a row in the log panel, and from a change in the merge dialog.
# One GUI run in a fresh user home, given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-sd \
#     SHAPEDIFFCHECK_OUT=/tmp/sd/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-shapediff-check.py
#
# It writes PASS/FAIL lines to $SHAPEDIFFCHECK_OUT and exits. The log setting
# is put back before it exits.
import os, tempfile, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["SHAPEDIFFCHECK_OUT"]
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


def drawn(obj):
    """The overlay's node under the object's root, and how many of its kinds
    are switched on; (None, 0) where there is none."""
    try:
        from pivy import coin
    except ImportError:
        return "no pivy", -1
    root = obj.ViewObject.RootNode
    for i in range(root.getNumChildren()):
        node = root.getChild(i)
        if str(node.getName()) == "ShapeDiff":
            on = 0
            for j in range(node.getNumChildren()):
                child = node.getChild(j)
                if (
                    child.isOfType(coin.SoSwitch.getClassTypeId())
                    and child.getNumChildren()
                    and child.whichChild.getValue() == coin.SO_SWITCH_ALL
                ):
                    on += 1
            return node, on
    return None, 0


def bar(parent):
    for w in parent.findChildren(QtWidgets.QWidget):
        if w.objectName() == "TransactionDiffBar":
            return w
    return None


def part(of, name):
    for w in of.findChildren(QtWidgets.QWidget):
        if w.objectName() == name:
            return w
    return None


def drive(seen, cut):
    # The merge dialog is modal: this runs inside its event loop.
    dialog = QtWidgets.QApplication.activeModalWidget()
    if dialog is None or dialog.objectName() != "TransactionMergeDialog":
        seen["found"] = False
        if dialog is not None:
            dialog.reject()
        return
    seen["found"] = True
    tree = dialog.findChild(QtWidgets.QTreeWidget)
    diff = bar(dialog)
    seen["bar"] = diff is not None
    seen["none"] = (cut.ViewObject.getValueDiff(), drawn(cut)[0] is None)
    for i in range(tree.topLevelItemCount()):
        item = tree.topLevelItem(i)
        if item.text(0).startswith("derived") and item.text(1) == "Cut" and item.text(2) == "Shape":
            tree.setCurrentItem(item)
    settle()
    seen["theirs"] = cut.ViewObject.getValueDiff()
    seen["theirs said"] = part(diff, "diffStatus").text() if diff else None
    seen["theirs drawn"] = drawn(cut)[1]
    if diff:
        pair = part(diff, "diffPair")
        seen["pairs"] = [pair.itemText(i) for i in range(pair.count())]
        pair.setCurrentIndex(1)
        settle()
        seen["base ours"] = cut.ViewObject.getValueDiff()
        pair.setCurrentIndex(2)
        settle()
        seen["base theirs"] = cut.ViewObject.getValueDiff()
        pair.setCurrentIndex(pair.count() - 1)
        settle()
        seen["nothing"] = cut.ViewObject.getValueDiff()
        pair.setCurrentIndex(0)
        settle()
    for i in range(tree.topLevelItemCount()):
        item = tree.topLevelItem(i)
        if item.text(1) == "Box" and item.text(2) == "Length":
            tree.setCurrentItem(item)
    settle()
    seen["a length"] = cut.ViewObject.getValueDiff()
    for i in range(tree.topLevelItemCount()):
        item = tree.topLevelItem(i)
        if item.text(0).startswith("derived") and item.text(1) == "Cut" and item.text(2) == "Shape":
            tree.setCurrentItem(item)
    settle()
    seen["again"] = cut.ViewObject.getValueDiff()
    dialog.reject()


def run():
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 2)
    try:
        p.SetInt("TransactionLog", 2)
        doc = App.newDocument("ShapeDiff")
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
        doc.saveAs(os.path.join(tempfile.mkdtemp(prefix="fc-sd-check-"), "sd.FCStd"))
        doc.openTransaction("taller")
        box.Height = 12
        doc.recompute()
        doc.commitTransaction()
        doc.openTransaction("a second hole")
        cyl2 = doc.addObject("Part::Cylinder", "Cyl2")
        cyl2.Radius, cyl2.Height = 1, 30
        cyl2.Placement.Base = App.Vector(2, 8, -10)
        cut2 = doc.addObject("Part::Cut", "Cut2")
        cut2.Base, cut2.Tool = cut, cyl2
        doc.recompute()
        doc.commitTransaction()
        settle()
        view = cut.ViewObject

        def op(name, obj, prop="Shape"):
            row = [r for r in doc.getTransactionLog() if r["name"] == name][0]
            return [
                o
                for o in doc.getTransactionOps(row["seq"])
                if o["prop"] == prop and o["cid"] == obj.ID
            ][0]

        taller = op("taller", cut)
        rows = len(doc.getTransactionLog())
        states = [(o.Name, tuple(o.State)) for o in doc.Objects]
        undos = doc.UndoCount

        # Asked of the view provider
        check("nothing shown to begin with", view.getValueDiff() == {} and drawn(cut)[0] is None)
        check("a row's before against its after is shown",
              view.showValueDiff("Shape", taller["before"], taller["after"]) is True)
        shown = view.getValueDiff()
        # A box with a hole through it, made taller: the bottom is as it was,
        # the top moved, the four sides and the hole's wall changed.
        check("what became of each face (%r)" % ({k: v for k, v in shown.items() if "." not in k},),
              shown.get("same") == 1 and shown.get("moved") == 1 and shown.get("changed") == 5
              and "added" not in shown and "removed" not in shown)
        check("every face paired by its name", shown.get("by.name") == 7)
        node, on = drawn(cut)
        check("drawn under the object's root, three kinds on (%r)" % (on,),
              node is not None and on in (3, -1))
        check("at the later state: it is full and the earlier ghosted (%r / %r)"
              % (shown.get("transparency.changedB"), shown.get("transparency.changedA")),
              shown.get("position") == 100
              and shown.get("transparency.changedB") == 30
              and shown.get("transparency.changedA") == 80
              and shown.get("transparency.same") == 85)
        view.showValueDiff("Shape", taller["before"], taller["after"], position=0.0)
        shown = view.getValueDiff()
        check("at the earlier state the other way round",
              shown.get("position") == 0
              and shown.get("transparency.changedB") == 80
              and shown.get("transparency.changedA") == 30)
        check("the same overlay, not another", drawn(cut)[0] == node or node == "no pivy")
        view.showValueDiff("Shape", taller["before"], taller["after"], differencesOnly=True)
        shown = view.getValueDiff()
        check("differences only: what did not change is left out",
              shown.get("shown.same") == 0 and shown.get("shown.changedB") == 1
              and drawn(cut)[1] in (2, -1))
        view.hideValueDiff()
        check("hidden: nothing shown, nothing drawn",
              view.getValueDiff() == {} and drawn(cut)[0] is None)

        # A face only the later state has
        added = op("a second hole", cut2)
        check("a shape with no value before has nothing to be against",
              added["before"] == ""
              or cut2.ViewObject.showValueDiff("Shape", added["before"], added["after"]) is True)
        cut2.ViewObject.hideValueDiff()
        check("the first cut against the second, of one object's view: a wall added",
              view.showValueDiff("Shape", taller["after"], added["after"]) is True
              and view.getValueDiff().get("added") == 1
              and view.getValueDiff().get("shown.added") == 1)
        view.hideValueDiff()

        # What it is not shown of
        check("not of a length", box.ViewObject.showValueDiff(
            "Length", op("taller", box, "Height")["before"], op("taller", box, "Height")["after"])
            is False)
        check("not of a ref the log has not",
              view.showValueDiff("Shape", "0" * 40, taller["after"]) is False
              and view.getValueDiff() == {})
        check("not of a property there is not",
              view.showValueDiff("NoSuch", taller["before"], taller["after"]) is False)

        check("showing wrote no row and no undo step",
              len(doc.getTransactionLog()) == rows and doc.UndoCount == undos)
        check("and left every object as it was",
              states == [(o.Name, tuple(o.State)) for o in doc.Objects])

        # The object's own shape as one of the two
        check("the value the object has now, against an earlier one",
              view.showValueDiff("Shape", taller["before"], "") is True
              and view.getValueDiff().get("changed") == 5)
        cut.Placement.Base = App.Vector(0, 0, 3)
        settle()
        check("the object moved: gone, its own shape being one of the two",
              view.getValueDiff() == {} and drawn(cut)[0] is None)
        cut.Placement.Base = App.Vector(0, 0, 0)
        doc.recompute()
        settle()
        view.showValueDiff("Shape", taller["before"], taller["after"])
        cyl2.Radius = 1.5
        doc.recompute()
        settle()
        check("two values of the log stay shown when another object changes",
              view.getValueDiff().get("changed") == 5)
        cyl2.Radius = 1
        doc.recompute()
        view.hideValueDiff()
        settle()

        # From the log panel: an op of a row
        dock, tree, ops = panel()
        check("panel found", dock is not None and tree is not None and ops is not None)
        if dock is not None and ops is not None:
            diff = bar(dock)
            check("the panel has the bar", diff is not None)
            panelRows = len(doc.getTransactionLog())
            name = column(tree, "Name")
            for i in range(tree.topLevelItemCount()):
                if tree.topLevelItem(i).text(name) == "taller":
                    tree.setCurrentItem(tree.topLevelItem(i))
            settle()
            check("a row selected shows nothing yet", view.getValueDiff() == {})
            prop = column(ops, "Property")
            before = column(ops, "Before")
            found = None
            for i in range(ops.topLevelItemCount()):
                item = ops.topLevelItem(i)
                if item.text(prop) == "Shape" and taller["before"].startswith(item.text(before)):
                    found = item
            check("the cut's shape among the row's ops", found is not None)
            if found is not None and diff is not None:
                ops.setCurrentItem(found)
                settle()
                check("its op selected: shown over the cut (%r)" % part(diff, "diffStatus").text(),
                      view.getValueDiff().get("changed") == 5
                      and part(diff, "diffStatus").text().startswith("1 as they were, 6 changed"))
                part(diff, "diffPosition").setValue(25)
                settle()
                check("the slider is where between the two",
                      view.getValueDiff().get("position") == 25)
                part(diff, "diffOnly").setChecked(True)
                settle()
                check("differences only", view.getValueDiff().get("shown.same") == 0)
                part(diff, "diffOnly").setChecked(False)
                part(diff, "diffPosition").setValue(100)
                for i in range(ops.topLevelItemCount()):
                    if ops.topLevelItem(i).text(prop) == "Height":
                        ops.setCurrentItem(ops.topLevelItem(i))
                settle()
                check("another op selected: gone",
                      view.getValueDiff() == {} and part(diff, "diffStatus").text() == "")
                ops.setCurrentItem(found)
                settle()
                for i in range(tree.topLevelItemCount()):
                    if tree.topLevelItem(i).text(name) == "base":
                        tree.setCurrentItem(tree.topLevelItem(i))
                settle()
                check("another row selected: gone", view.getValueDiff() == {})
                check("shown from the panel: no row written (%d)" % len(doc.getTransactionLog()),
                      len(doc.getTransactionLog()) == panelRows)

        # From the merge dialog: a change of the other branch
        doc.createTransactionBranch("side")
        doc.openTransaction("side: longer")
        box.Length = 14
        doc.recompute()
        doc.commitTransaction()
        doc.switchTransactionBranch("main")
        settle()
        box, cut = doc.getObject("Box"), doc.getObject("Cut")
        view = cut.ViewObject
        doc.openTransaction("main: wider")
        box.Width = 11
        doc.recompute()
        doc.commitTransaction()
        settle()
        rows = len(doc.getTransactionLog())
        if dock is not None:
            seen = {}
            QtCore.QTimer.singleShot(300, lambda: drive(seen, cut))
            QtCore.QMetaObject.invokeMethod(
                dock, "mergeBranch", QtCore.Qt.DirectConnection, QtCore.Q_ARG(str, "side")
            )
            settle()
            check("dialog shown, with the bar", seen.get("found") is True and seen.get("bar"))
            check("dialog: nothing shown before a change is selected (%r)" % (seen.get("none"),),
                  seen.get("none") == ({}, True))
            theirs = seen.get("theirs") or {}
            check("dialog: the cut's shape selected, ours against theirs (%r)"
                  % seen.get("theirs said"),
                  theirs.get("changed", 0) + theirs.get("moved", 0) >= 4
                  and theirs.get("by.name") == 7
                  and seen.get("theirs drawn") in (2, 3, -1)
                  and bool(seen.get("theirs said")))
            check("dialog: the pairs (%r)" % (seen.get("pairs"),),
                  seen.get("pairs") == ["ours against theirs", "base against ours",
                                        "base against theirs", "nothing"])
            check("dialog: base against ours, base against theirs",
                  (seen.get("base ours") or {}).get("by.name") == 7
                  and (seen.get("base theirs") or {}).get("by.name") == 7
                  and seen.get("base ours") != seen.get("theirs"))
            check("dialog: nothing, when asked", seen.get("nothing") == {})
            check("dialog: a length selected shows nothing", seen.get("a length") == {})
            check("dialog: the shape again", (seen.get("again") or {}).get("by.name") == 7)
            check("dialog closed: nothing left in the view",
                  view.getValueDiff() == {} and drawn(cut)[0] is None)
            check("dialog cancelled: no row written", len(doc.getTransactionLog()) == rows)
        App.closeDocument(doc.Name)
        settle()
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        p.SetInt("TransactionLog", mode)
        App.saveParameter()
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
