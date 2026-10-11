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
    cut2 = cut.Document.getObject("Cut2")
    seen["unchosen"] = cut2.ViewObject.getValueDiff()
    seen["unchosen said"] = part(diff, "diffStatus").text() if diff else None
    Gui.Selection.addSelection(cut2)
    part(diff, "diffAdd").click()
    settle()
    seen["taken"] = Gui.Selection.getSelection() == []
    seen["whole"] = cut2.ViewObject.getValueDiff()
    seen["whole said"] = part(diff, "diffStatus").text() if diff else None
    seen["none"] = (cut.ViewObject.getValueDiff(), drawn(cut)[0] is None)
    for i in range(tree.topLevelItemCount()):
        item = tree.topLevelItem(i)
        if item.text(0).startswith("derived") and item.text(1) == "Cut" and item.text(2) == "Shape":
            tree.setCurrentItem(item)
    settle()
    seen["theirs"] = cut.ViewObject.getValueDiff()
    seen["whole then"] = cut2.ViewObject.getValueDiff()
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

        # No value at one of the two: all of the other is new, or gone
        check("against no value, every face is new",
              view.showValueDiff("Shape", "-", taller["after"]) is True
              and view.getValueDiff().get("added") == 7
              and "changed" not in view.getValueDiff())
        check("and against none after, every face is gone",
              view.showValueDiff("Shape", taller["after"], "-") is True
              and view.getValueDiff().get("removed") == 7)
        check("not of no value twice", view.showValueDiff("Shape", "-", "-") is False)
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

        # A version here, and a row after it that changes what the view shows
        doc.save()
        doc.openTransaction("wider")
        box.Width = 12
        doc.recompute()
        doc.commitTransaction()
        settle()
        view2 = cut2.ViewObject

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

                # A row, the whole of it: of it, the objects chosen (sec 32.13)
                def select(row):
                    for i in range(tree.topLevelItemCount()):
                        if tree.topLevelItem(i).text(name) == row:
                            tree.setCurrentItem(tree.topLevelItem(i))
                    settle()

                Gui.Selection.clearSelection()
                select("wider")
                said = part(diff, "diffStatus").text()
                check("a row selected, no object chosen: nothing shown, and how many "
                      "changed (%r)" % said,
                      view2.getValueDiff() == {} and view.getValueDiff() == {}
                      and said.startswith("3 object(s) with changes: add one")
                      and part(diff, "diffPair").itemText(0) == "before against after")
                # Chosen as an attachment's references are: what is selected
                # is taken, and is then no longer selected
                Gui.Selection.addSelection(cut2)
                settle()
                check("selecting chooses nothing by itself", view2.getValueDiff() == {})
                part(diff, "diffAdd").click()
                settle()
                shown2 = view2.getValueDiff()
                check("added: what the row changed of it, and it is no longer selected (%r)"
                      % part(diff, "diffStatus").text(),
                      shown2.get("changed", 0) + shown2.get("moved", 0) >= 5
                      and shown2.get("by.name") == 8
                      and Gui.Selection.getSelection() == []
                      and part(diff, "diffObject0").text() == "Cut2")
                check("and of no other object",
                      view.getValueDiff() == {} and box.ViewObject.getValueDiff() == {})
                part(diff, "diffAdd").click()
                settle()
                check("add, with nothing selected: asks for a pick (%r)"
                      % part(diff, "diffStatus").text(),
                      part(diff, "diffStatus").text().startswith("Pick an object")
                      and part(diff, "diffAdd").isChecked())
                Gui.Selection.addSelection(cut)
                settle()
                check("the next one picked is taken: both shown (%r)"
                      % part(diff, "diffStatus").text(),
                      view.getValueDiff().get("by.name") == 7
                      and view2.getValueDiff().get("by.name") == 8
                      and part(diff, "diffStatus").text().startswith("2 objects: ")
                      and Gui.Selection.getSelection() == []
                      and not part(diff, "diffAdd").isChecked()
                      and part(diff, "diffObject1").text() == "Cut")
                part(diff, "diffObject1").click()
                settle()
                Gui.Selection.addSelection(box)
                settle()
                check("an object's own button picks another in its place",
                      part(diff, "diffObject1").text() == "Box"
                      and view.getValueDiff() == {} and bool(box.ViewObject.getValueDiff()))
                part(diff, "diffRemove1").click()
                settle()
                check("and let go: the first alone",
                      view.getValueDiff() == {} and box.ViewObject.getValueDiff() == {}
                      and view2.getValueDiff().get("by.name") == 8
                      and part(diff, "diffObject1") is None)
                for i in range(ops.topLevelItemCount()):
                    item = ops.topLevelItem(i)
                    if item.text(prop) == "Shape" and str(cut.ID) in item.text(column(ops, "Container")):
                        ops.setCurrentItem(item)
                settle()
                check("one of its ops selected: that object alone, chosen or not",
                      view.getValueDiff().get("by.name") == 7 and view2.getValueDiff() == {})
                select("a second hole")
                shown2 = view2.getValueDiff()
                check("the row that made the object chosen: all of it is new (%r)"
                      % part(diff, "diffStatus").text(),
                      shown2.get("added") == 8 and "changed" not in shown2
                      and view.getValueDiff() == {})
                select("base")
                said = part(diff, "diffStatus").text()
                check("a row that changed nothing of the object chosen: nothing, and said "
                      "(%r)" % said,
                      view2.getValueDiff() == {} and view.getValueDiff() == {}
                      and said.startswith("No shape of what is chosen changed"))
                part(diff, "diffRemove0").click()
                settle()

                # A version against the document as it is
                tabs = dock.findChild(QtWidgets.QTabWidget)
                versions = None
                for w in dock.findChildren(QtWidgets.QTreeWidget):
                    if w.headerItem().text(1) == "Kind" and w.headerItem().text(0) == "Num":
                        versions = w
                check("the versions list found", tabs is not None and versions is not None)
                if tabs is not None and versions is not None:
                    tabs.setCurrentIndex(1)
                    settle()
                    seq = column(versions, "Seq")
                    items = sorted((versions.topLevelItem(i) for i in range(versions.topLevelItemCount())),
                                   key=lambda item: int(item.text(seq)))
                    check("two versions and more (%d)" % len(items), len(items) >= 2)
                    versions.setCurrentItem(items[-1])
                    settle()
                    changes = dock.findChild(QtWidgets.QTreeWidget, "TransactionVersionChanges")
                    lines2 = [] if changes is None else [
                        tuple(changes.topLevelItem(i).text(c) for c in (0, 1, 3))
                        for i in range(changes.topLevelItemCount())]
                    said = part(diff, "diffStatus").text()
                    check("the newest version, no object chosen: nothing shown, and the list "
                          "of what is not as it was (%d, %r)" % (len(lines2), said),
                          view2.getValueDiff() == {} and view.getValueDiff() == {}
                          and "with changes: add one" in said
                          and ("obj:%d Cut2" % cut2.ID, "Shape", "changed, by a recompute") in lines2
                          and ("obj:%d Box" % box.ID, "Width", "changed") in lines2
                          and part(diff, "diffPair").itemText(0) == "the version against now")
                    Gui.Selection.addSelection(cut2)
                    part(diff, "diffAdd").click()
                    settle()
                    shown2 = view2.getValueDiff()
                    check("an object chosen: what changed of it since (%r)"
                          % part(diff, "diffStatus").text(),
                          shown2.get("changed", 0) + shown2.get("moved", 0) >= 5
                          and shown2.get("by.name") == 8 and view.getValueDiff() == {})
                    if changes is not None:
                        for i in range(changes.topLevelItemCount()):
                            item = changes.topLevelItem(i)
                            if item.text(0) == "obj:%d Cut" % cut.ID and item.text(1) == "Shape":
                                changes.setCurrentItem(item)
                        settle()
                        pane = [w for w in dock.findChildren(QtWidgets.QPlainTextEdit)][-1]
                        check("a line of the list selected: that object alone, and its two "
                              "values",
                              view.getValueDiff().get("by.name") == 7
                              and view2.getValueDiff() == {}
                              and pane.toPlainText().startswith("== now ")
                              and "== at the version " in pane.toPlainText())
                        changes.clearSelection()
                        settle()
                        check("none selected: the object chosen again",
                              view.getValueDiff() == {}
                              and view2.getValueDiff().get("by.name") == 8)
                    versions.setCurrentItem(items[0])
                    settle()
                    shown2 = view2.getValueDiff()
                    lines2 = [] if changes is None else [
                        tuple(changes.topLevelItem(i).text(c) for c in (0, 1, 3))
                        for i in range(changes.topLevelItemCount())]
                    check("the first version, which had no such object: all of it is new, "
                          "and the list says it was made",
                          shown2.get("added") == 8 and "changed" not in shown2
                          and ("obj:%d Cut2" % cut2.ID, "", "made") in lines2)
                    tabs.setCurrentIndex(0)
                    settle()
                    select("wider")
                    check("still chosen on another tab and row",
                          view2.getValueDiff().get("by.name") == 8)
                    tabs.setCurrentIndex(1)
                    settle()
                    part(diff, "diffRemove0").click()
                    settle()
                    tabs.setCurrentIndex(0)
                    settle()
                    select("base")
                    check("back on the rows: gone", view2.getValueDiff() == {})

                # Kept in the file (sec 32.11): one value, an object's, and
                # the objects selected as they are at a row
                derived = column(ops, "Derived")
                container = column(ops, "Container")

                def said(cid):
                    for i in range(ops.topLevelItemCount()):
                        item = ops.topLevelItem(i)
                        if item.text(prop) == "Shape" and str(cid) in item.text(container):
                            return item.text(derived)
                    return None

                def call(slot, *args):
                    QtCore.QMetaObject.invokeMethod(dock, slot, QtCore.Qt.DirectConnection, *args)
                    settle()

                wider = [r for r in doc.getTransactionLog() if r["name"] == "wider"][-1]["seq"]
                select("wider")
                check("a shape a recompute made says so (%r)" % said(cut2.ID), said(cut2.ID) == "yes")
                call("keepValue", QtCore.Q_ARG("qlonglong", wider),
                     QtCore.Q_ARG("qlonglong", cut2.ID), QtCore.Q_ARG(str, "Shape"),
                     QtCore.Q_ARG(bool, True))
                kept = doc.getKeptTransactionValues()
                check("one value kept: listed, and its op says kept (%r)" % said(cut2.ID),
                      [(k["object"], k["prop"], k["seq"]) for k in kept] == [("Cut2", "Shape", wider)]
                      and said(cut2.ID) == "yes, kept" and said(cut.ID) == "yes")
                call("keepValue", QtCore.Q_ARG("qlonglong", wider),
                     QtCore.Q_ARG("qlonglong", cut2.ID), QtCore.Q_ARG(str, "Shape"),
                     QtCore.Q_ARG(bool, False))
                check("let go again", doc.getKeptTransactionValues() == []
                      and said(cut2.ID) == "yes")
                call("keepObject", QtCore.Q_ARG("qlonglong", cut.ID), QtCore.Q_ARG(bool, True))
                check("an object's values kept: every op of it says so",
                      doc.getKeptTransactionObjects() == [cut.ID]
                      and said(cut.ID) == "yes, kept" and said(cut2.ID) == "yes")
                call("keepObject", QtCore.Q_ARG("qlonglong", cut.ID), QtCore.Q_ARG(bool, False))
                check("and no longer", doc.getKeptTransactionObjects() == [])
                Gui.Selection.clearSelection()
                call("keepSelectedAt", QtCore.Q_ARG("qlonglong", wider), QtCore.Q_ARG(bool, True))
                check("nothing selected: nothing kept", doc.getKeptTransactionValues() == [])
                Gui.Selection.addSelection(cut2)
                call("keepSelectedAt", QtCore.Q_ARG("qlonglong", wider), QtCore.Q_ARG(bool, True))
                kept = doc.getKeptTransactionValues()
                check("the object selected, kept as it is at a row (%r)"
                      % [(k["object"], k["prop"]) for k in kept],
                      bool(kept) and {k["object"] for k in kept} == {"Cut2"}
                      and "Shape" in {k["prop"] for k in kept})
                call("keepSelectedAt", QtCore.Q_ARG("qlonglong", wider), QtCore.Q_ARG(bool, False))
                check("and let go", doc.getKeptTransactionValues() == [])
                Gui.Selection.clearSelection()

                # The list of the kept (sec 32.12)
                keptList = dock.findChild(QtWidgets.QTreeWidget, "TransactionKept")
                check("the list of the kept: there, and empty",
                      keptList is not None and keptList.topLevelItemCount() == 0
                      and tabs.tabText(2) == "Kept")
                if keptList is not None:
                    what, whose = column(keptList, "Kept"), column(keptList, "Object")
                    which, at = column(keptList, "Property"), column(keptList, "Row")

                    def tops():
                        return [keptList.topLevelItem(i)
                                for i in range(keptList.topLevelItemCount())]

                    call("keepValue", QtCore.Q_ARG("qlonglong", wider),
                         QtCore.Q_ARG("qlonglong", cut2.ID), QtCore.Q_ARG(str, "Shape"),
                         QtCore.Q_ARG(bool, True))
                    call("keepObject", QtCore.Q_ARG("qlonglong", cut.ID), QtCore.Q_ARG(bool, True))
                    tabs.setCurrentIndex(2)
                    settle()
                    said2 = [(t.text(what), t.text(whose), t.text(which), t.text(at)) for t in tops()]
                    check("one value and one object listed (%r)" % said2,
                          said2 == [("one value", "obj:%d Cut2" % cut2.ID, "Shape", str(wider)),
                                    ("every value", "obj:%d Cut" % cut.ID, "", "")]
                          and tabs.tabText(2) == "Kept (2)")
                    # The wrappers held: a child's goes with its parent's
                    listed = tops()
                    under = [listed[1].child(i) for i in range(listed[1].childCount())]
                    check("under the object, each shape the log has of it (%d)" % len(under),
                          len(under) >= 2 and all(c.text(which) == "Shape" for c in under)
                          and len({c.text(at) for c in under}) == len(under)
                          and all(len(c.toolTip(column(keptList, "Value"))) == 40 for c in under))
                    keptList.setCurrentItem(listed[0])
                    settle()
                    pane = [w for w in dock.findChildren(QtWidgets.QPlainTextEdit)][-1]
                    # Cut2 is still what the row left it: all eight as they were
                    check("a kept value selected: against now in the view, and its text (%r)"
                          % part(diff, "diffStatus").text(),
                          part(diff, "diffPair").itemText(0) == "the kept value against now"
                          and view2.getValueDiff().get("same") == 8
                          and view.getValueDiff() == {}
                          and pane.toPlainText().startswith("== kept "))
                    keptList.setCurrentItem(under[0])
                    settle()
                    shown = view.getValueDiff()
                    check("a value of the kept object selected: its first shape, over that "
                          "object (%r)" % part(diff, "diffStatus").text(),
                          shown.get("changed", 0) + shown.get("moved", 0) + shown.get("added", 0) > 0
                          and view2.getValueDiff() == {})
                    del listed, under   # the list is made again below
                    ref = doc.getKeptTransactionValues()[0]["ref"]
                    call("releaseValue", QtCore.Q_ARG(str, ref))
                    check("the one value let go by its ref: what was selected stays so",
                          doc.getKeptTransactionValues() == [] and tabs.tabText(2) == "Kept (1)"
                          and [t.text(what) for t in tops()] == ["every value"]
                          and len(keptList.selectedItems()) == 1
                          and bool(view.getValueDiff()))
                    call("releaseValue", QtCore.Q_ARG(str, ref))
                    check("let go twice: said, and nothing moved",
                          any("Nothing let go" in w.text()
                              for w in dock.findChildren(QtWidgets.QLabel))
                          and tabs.tabText(2) == "Kept (1)")
                    call("keepObject", QtCore.Q_ARG("qlonglong", cut.ID), QtCore.Q_ARG(bool, False))
                    check("the object no longer kept: the list is empty and nothing is shown",
                          tops() == [] and tabs.tabText(2) == "Kept" and view.getValueDiff() == {})
                    # Kept from Python: no signal says so, the tab asks again
                    tabs.setCurrentIndex(0)
                    settle()
                    refs = doc.keepTransactionValue(cut2, "Shape", wider)
                    tabs.setCurrentIndex(2)
                    settle()
                    check("kept from Python: listed when the tab is shown",
                          len(refs) == 1 and [t.text(what) for t in tops()] == ["one value"])
                    check("let go from Python by its ref, once",
                          doc.releaseTransactionValue(refs[0]) is True
                          and doc.releaseTransactionValue(refs[0]) is False)
                    tabs.setCurrentIndex(0)
                    settle()
                    tabs.setCurrentIndex(2)
                    settle()
                    check("and gone from the list", tops() == [])
                    tabs.setCurrentIndex(0)
                    settle()
                select("base")
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
            check("dialog: no change selected and no object chosen: nothing shown (%r)"
                  % seen.get("unchosen said"),
                  seen.get("unchosen") == {}
                  and "with changes: add one" in (seen.get("unchosen said") or ""))
            check("dialog: an object added, what the merge would change of it (%r)"
                  % seen.get("whole said"),
                  (seen.get("whole") or {}).get("by.name") == 8 and bool(seen.get("whole said"))
                  and seen.get("taken") is True)
            check("dialog: and of no other object (%r)" % (seen.get("none"),),
                  seen.get("none") == ({}, True))
            check("dialog: a change selected, its object alone",
                  seen.get("whole then") == {})
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
                  view.getValueDiff() == {} and drawn(cut)[0] is None
                  and doc.getObject("Cut2").ViewObject.getValueDiff() == {})
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
