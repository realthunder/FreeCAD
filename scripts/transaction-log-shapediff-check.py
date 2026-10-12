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
import time
import math, os, tempfile, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["SHAPEDIFFCHECK_OUT"]
lines = []


def mergeDialog():
    # The merge's preview is not modal (docs/TransactionLog.md sec 32.15): it
    # is found among the windows. Anything modal instead is in its way.
    for w in QtWidgets.QApplication.topLevelWidgets():
        if w.objectName() == "TransactionMergeDialog" and w.isVisible():
            return w
    return QtWidgets.QApplication.activeModalWidget()


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


def inView(doc, obj):
    """Whether the root of `obj`'s view is in the scene of `doc`'s 3D view."""
    try:
        from pivy import coin
    except ImportError:
        return "no pivy"
    search = coin.SoSearchAction()
    search.setNode(obj.ViewObject.RootNode)
    search.setSearchingAll(True)
    search.apply(Gui.getDocument(doc.Name).ActiveView.getSceneGraph())
    return search.getPath() is not None


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
    # The panel waits for the merge dialog in a loop of its own: this runs
    # inside it.
    dialog = mergeDialog()
    if dialog is None or dialog.objectName() != "TransactionMergeDialog":
        seen["found"] = False
        if dialog is not None:
            dialog.reject()
        return
    seen["found"] = True
    tree = dialog.findChild(QtWidgets.QTreeWidget)
    diff = bar(dialog)
    seen["bar"] = diff is not None
    # By the faces, as what follows counts them; by the material further on
    seen["mode"] = part(diff, "diffMode").currentText() if diff else None
    if diff:
        part(diff, "diffMode").setCurrentIndex(1)
        settle()
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
    if diff:
        part(diff, "diffMode").setCurrentIndex(0)
        settle()
        seen["material"] = cut.ViewObject.getValueDiff()
        seen["material said"] = part(diff, "diffStatus").text()
        part(diff, "diffMode").setCurrentIndex(1)
        settle()

    # What the merge would make (sec 32.17): made when its pair is chosen,
    # in a document of its own, and nothing of this one moves
    doc = cut.Document
    if diff:
        pair = part(diff, "diffPair")
        was = (doc.UndoCount, doc.RedoCount, len(doc.getTransactionLog()),
               [(o.Name, tuple(o.State)) for o in doc.Objects], App.ActiveDocument.Name)
        seen["result docs before"] = [n for n in App.listDocuments() if "_merged" in n]
        part(diff, "diffMode").setCurrentIndex(0)
        pair.setCurrentIndex(3)
        settle()
        seen["result"] = cut.ViewObject.getValueDiff()
        seen["result said"] = part(diff, "diffStatus").text()
        seen["result docs"] = [n for n in App.listDocuments() if "_merged" in n]
        now = (doc.UndoCount, doc.RedoCount, len(doc.getTransactionLog()),
               [(o.Name, tuple(o.State)) for o in doc.Objects], App.ActiveDocument.Name)
        seen["result left"] = (was[:2] == now[:2] and now[2] - was[2] in (0, 1)
                               and was[3:] == now[3:], [r["kind"] for r in
                                                        doc.getTransactionLog()[was[2]:]])
        seen["result open"] = mergeDialog() is dialog
        # With no change selected: of the object chosen
        tree.clearSelection()
        settle()
        part(diff, "diffMode").setCurrentIndex(1)
        settle()
        seen["result whole"] = (cut2.ViewObject.getValueDiff(), cut.ViewObject.getValueDiff(),
                                part(diff, "diffStatus").text())
        # What the merge makes and the document has not: all of it new,
        # drawn in this document's view by the view the result has of it
        made = App.getDocument(seen["result docs"][0]).getObject("Ball")
        seen["made"] = (made is not None and doc.getObject("Ball") is None,
                        made.ViewObject.getValueDiff() if made else None,
                        inView(doc, made) if made else None,
                        made.ViewObject.Visibility if made else None)
        for i in range(tree.topLevelItemCount()):
            item = tree.topLevelItem(i)
            if item.text(1) == "Ball" and item.text(2) == "":
                tree.setCurrentItem(item)
        settle()
        seen["made alone"] = (made.ViewObject.getValueDiff() if made else None,
                              cut2.ViewObject.getValueDiff())
        tree.clearSelection()
        settle()
        # Asked again it is there already
        pair.setCurrentIndex(0)
        settle()
        seen["made other pair"] = (made.ViewObject.getValueDiff() if made else None,
                                   inView(doc, made) if made else None)
        pair.setCurrentIndex(3)
        settle()
        seen["result once"] = [n for n in App.listDocuments() if "_merged" in n]
        pair.setCurrentIndex(0)
        settle()

    # Not modal (sec 32.15): the document is in reach while it is open
    seen["not modal"] = (not dialog.isModal()
                         and QtWidgets.QApplication.activeModalWidget() is None)
    tree.clearSelection()
    part(diff, "diffAdd").click()
    settle()
    seen["asks"] = part(diff, "diffStatus").text()
    Gui.Selection.addSelection(cut)
    settle()
    seen["picked"] = (cut.ViewObject.getValueDiff().get("by.name"),
                      Gui.Selection.getSelection() == [])
    QtCore.QMetaObject.invokeMethod(seen["dock"], "mergeBranch", QtCore.Qt.DirectConnection,
                                    QtCore.Q_ARG(str, "side"))
    settle()
    seen["one at a time"] = len([w for w in QtWidgets.QApplication.topLevelWidgets()
                                 if w.objectName() == "TransactionMergeDialog" and w.isVisible()])

    def wait():
        for _ in range(12):
            time.sleep(0.05)
            settle()

    def stale():
        return (part(dialog, "stale").isVisible(), part(dialog, "merge").isEnabled(),
                part(dialog, "again").isVisible())

    # A change the merge does not see: the preview stands. (Not back to the
    # 12 the branches left from: with nothing of ours changed since, the
    # merge is another one, the other taken whole.)
    doc.openTransaction("main: wider still")
    doc.getObject("Box").Width = 13
    doc.recompute()
    doc.commitTransaction()
    wait()
    seen["stands"] = stale()
    # One it does: theirs set the length, and now ours has too
    doc.openTransaction("main: longer too")
    doc.getObject("Box").Length = 15
    doc.recompute()
    doc.commitTransaction()
    wait()
    seen["stale"] = stale()
    # Stale, the result is of a document that is no more: closed, and none
    # made from here
    if diff:
        pair = part(diff, "diffPair")
        pair.setCurrentIndex(0)
        settle()
        pair.setCurrentIndex(3)
        settle()
        seen["stale result"] = ([n for n in App.listDocuments() if "_merged" in n],
                                part(diff, "diffStatus").text())
    # The side a conflict starts on is a setting (sec 32.17)
    App.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("MergeConflictSide", 1)
    QtCore.QTimer.singleShot(300, lambda: driveAgain(seen, dialog))
    part(dialog, "again").click()


def driveAgain(seen, before):
    # The preview made again, in the same wait of the panel
    dialog = mergeDialog()
    seen["again found"] = (dialog is not None and dialog is not before
                           and dialog.objectName() == "TransactionMergeDialog")
    if dialog is None:
        return
    if seen["again found"]:
        tree = dialog.findChild(QtWidgets.QTreeWidget)
        seen["again says"] = [
            tree.topLevelItem(i).text(0) for i in range(tree.topLevelItemCount())
            if tree.topLevelItem(i).text(1) == "Box" and tree.topLevelItem(i).text(2) == "Length"]
        seen["again fresh"] = (part(dialog, "stale").isVisible(),
                               part(dialog, "merge").isEnabled())
        # The conflict starts on theirs, as the setting says; the result is
        # made with the sides as they are, and again when one changes
        doc = seen["doc"]
        cut = doc.getObject("Cut")
        diff = bar(dialog)
        sides = [c for c in tree.findChildren(QtWidgets.QComboBox)
                 if [c.itemText(i) for i in range(c.count())] == ["ours", "theirs"]]
        seen["starts on"] = [c.currentText() for c in sides]
        for i in range(tree.topLevelItemCount()):
            item = tree.topLevelItem(i)
            if item.text(1) == "Cut" and item.text(2) == "Shape":
                tree.setCurrentItem(item)
        settle()
        pair = part(diff, "diffPair")
        pair.setCurrentIndex(3)
        settle()
        seen["side theirs"] = (cut.ViewObject.getValueDiff(), part(diff, "diffStatus").text(),
                               [n for n in App.listDocuments() if "_merged" in n])
        for c in sides:
            c.setCurrentIndex(0)
        settle()
        seen["side ours"] = (cut.ViewObject.getValueDiff(), part(diff, "diffStatus").text(),
                             [n for n in App.listDocuments() if "_merged" in n])
    App.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("MergeConflictSide", 0)
    seen["rows"] = len(seen["doc"].getTransactionLog())
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

        # By its material (sec 32.16): where it changed, not which faces
        ring = 100 - math.pi * 4

        def near(value, wanted):
            return value is not None and abs(value - wanted) <= 1e-6 * max(1.0, wanted)

        check("by its material: shown",
              view.showValueDiff("Shape", taller["before"], taller["after"], mode="material")
              is True)
        shown = view.getValueDiff()
        check("a box with a hole through it, made taller: one slab added (%r)"
              % ({k: v for k, v in shown.items() if k.startswith(("volume", "material"))},),
              shown.get("material") == 1 and near(shown.get("volume.added"), ring * 2)
              and shown.get("volume.removed") == 0 and near(shown.get("volume.common"), ring * 10)
              and "changed" not in shown and "by.name" not in shown
              and shown.get("material.kept") == 0)
        node, on = drawn(cut)
        check("drawn: what both have, thin, and what was added, full (%r)" % (on,),
              node is not None and on in (2, -1)
              and shown.get("shown.same") == 1 and shown.get("shown.added") == 1
              and shown.get("shown.removed") == 0 and shown.get("shown.changedB") == 0
              and shown.get("transparency.added") == 30
              and shown.get("transparency.same") == 85)
        view.showValueDiff("Shape", taller["before"], taller["after"], differencesOnly=True,
                           mode="material")
        check("differences only: the slab alone",
              view.getValueDiff().get("shown.same") == 0 and drawn(cut)[1] in (1, -1))
        view.showValueDiff("Shape", taller["before"], taller["after"])
        shown = view.getValueDiff()
        check("the same two by their faces again: the same overlay",
              shown.get("material") == 0 and shown.get("changed") == 5
              and "volume.added" not in shown
              and (drawn(cut)[0] == node or node == "no pivy"))
        view.hideValueDiff()
        view.showValueDiff("Shape", taller["before"], taller["after"], mode="material")
        check("shown again: the two are not fused a second time",
              view.getValueDiff().get("material.kept") == 1
              and near(view.getValueDiff().get("volume.added"), ring * 2))
        view.showValueDiff("Shape", taller["after"], taller["before"], position=0.0,
                           mode="material")
        shown = view.getValueDiff()
        check("the other way round: the slab is what was removed",
              near(shown.get("volume.removed"), ring * 2) and shown.get("volume.added") == 0
              and shown.get("material.kept") == 0 and shown.get("transparency.removed") == 30)
        view.hideValueDiff()
        check("against no value there are no two solids: by the faces, and said",
              view.showValueDiff("Shape", "-", taller["after"], mode="material") is True
              and view.getValueDiff().get("material") == 0
              and view.getValueDiff().get("material.none") == 1
              and view.getValueDiff().get("added") == 7)
        view.hideValueDiff()
        check("the value the object has now, by its material",
              view.showValueDiff("Shape", taller["before"], "", mode="material") is True
              and near(view.getValueDiff().get("volume.added"), ring * 2)
              and view.getValueDiff().get("material.kept") == 0)
        view.hideValueDiff()
        view.showValueDiff("Shape", taller["before"], "", mode="material")
        check("and that is known again by the shape it is",
              view.getValueDiff().get("material.kept") == 1)
        view.hideValueDiff()
        check("not by a way there is not",
              view.showValueDiff("Shape", taller["before"], taller["after"], mode="nosuch")
              is False and view.getValueDiff() == {})

        # Two heavy solids are not waited for: their faces meanwhile. Any two
        # are heavy here, by the setting that says from how many faces on.
        heavy = App.ParamGet("User parameter:BaseApp/Preferences/Mod/Part")
        heavy.SetInt("ShapeDiffInlineFaces", 0)

        def arrive(of):
            for _ in range(500):
                if not of.getValueDiff().get("material.pending"):
                    return True
                time.sleep(0.02)
                settle()
            return False

        check("heavy: shown at once, by the faces",
              view.showValueDiff("Shape", "", taller["before"], mode="material") is True
              and view.getValueDiff().get("material.pending") == 1
              and view.getValueDiff().get("material") == 0
              and view.getValueDiff().get("changed") == 5)
        came = arrive(view)
        shown = view.getValueDiff()
        check("heavy: and by the material when it is there (%r)"
              % ({k: v for k, v in shown.items() if k.startswith(("volume", "material"))},),
              came and shown.get("material") == 1 and near(shown.get("volume.removed"), ring * 2)
              and shown.get("material.kept") == 0 and "changed" not in shown
              and drawn(cut)[1] in (2, -1))
        view.hideValueDiff()
        view.showValueDiff("Shape", "", taller["before"], mode="material")
        check("heavy: asked again it is there already",
              "material.pending" not in view.getValueDiff()
              and view.getValueDiff().get("material.kept") == 1)
        view.hideValueDiff()
        second = op("a second hole", cut2)
        view.showValueDiff("Shape", taller["before"], second["after"], mode="material")
        left = view.getValueDiff().get("material.pending")
        view.hideValueDiff()
        for _ in range(20):
            time.sleep(0.01)
            settle()
        check("heavy: left before it is there, nothing is shown later",
              left == 1 and view.getValueDiff() == {} and drawn(cut)[0] is None)
        view.showValueDiff("Shape", taller["before"], second["after"], mode="material")
        again = view.getValueDiff().get("material.pending")
        came = arrive(view)
        shown = view.getValueDiff()
        # The slab without the second hole's part of it, and that hole
        # through what was there
        check("heavy: and asked again it is worked out again (%r, %r)"
              % (shown.get("volume.added"), shown.get("volume.removed")),
              again == 1 and came and shown.get("material") == 1
              and near(shown.get("volume.added"), ring * 2 - math.pi * 2)
              and near(shown.get("volume.removed"), math.pi * 10))
        view.hideValueDiff()
        heavy.RemInt("ShapeDiffInlineFaces")

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
            # By the faces, as what follows counts them; by the material below
            how = part(diff, "diffMode") if diff is not None else None
            check("the bar says how a shape is shown, by its material first",
                  how is not None and [how.itemText(i) for i in range(how.count())]
                  == ["Material", "Faces"] and how.currentIndex() == 0)
            if how is not None:
                how.setCurrentIndex(1)
                settle()
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
                how.setCurrentIndex(0)
                settle()
                shown = view.getValueDiff()
                said = part(diff, "diffStatus").text()
                check("by its material: the slab, and its volume said (%r)" % said,
                      shown.get("material") == 1 and near(shown.get("volume.added"), ring * 2)
                      and " added, " in said and " removed, " in said
                      and said.endswith(" in both") and "as they were" not in said)
                part(diff, "diffOnly").setChecked(True)
                settle()
                check("by its material, differences only",
                      view.getValueDiff().get("shown.same") == 0
                      and view.getValueDiff().get("shown.added") == 1)
                part(diff, "diffOnly").setChecked(False)
                how.setCurrentIndex(1)
                settle()
                check("and by its faces again",
                      view.getValueDiff().get("material") == 0
                      and view.getValueDiff().get("changed") == 5)
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
                how.setCurrentIndex(0)
                settle()
                said = part(diff, "diffStatus").text()
                check("by its material there are no two solids of it: its faces, and said "
                      "(%r)" % said,
                      view2.getValueDiff().get("material.none") == 1
                      and view2.getValueDiff().get("added") == 8
                      and said.startswith("not two solids, by the faces: "))
                # Heavy, as above: the faces and that it is being worked out,
                # then the material without anything asked again
                heavy.SetInt("ShapeDiffInlineFaces", 0)
                for i in range(tree.topLevelItemCount()):
                    if tree.topLevelItem(i).text(name) == "wider":
                        tree.setCurrentItem(tree.topLevelItem(i))
                said = part(diff, "diffStatus").text()
                for _ in range(200):
                    if " added, " in part(diff, "diffStatus").text():
                        break
                    time.sleep(0.02)
                    settle()
                heavy.RemInt("ShapeDiffInlineFaces")
                check("a heavy one is said to be worked out, its faces meanwhile (%r)" % said,
                      said.startswith("working out the material; the faces meanwhile: ")
                      and view2.getValueDiff().get("material") == 1)
                said = part(diff, "diffStatus").text()
                # 10 x 10 made 10 x 12, 12 high; both holes are through what was there
                check("a row of it by its material (%r)" % said,
                      view2.getValueDiff().get("material") == 1
                      and near(view2.getValueDiff().get("volume.added"), 10 * 2 * 12)
                      and view2.getValueDiff().get("volume.removed") == 0
                      and " added, " in said)
                how.setCurrentIndex(1)
                settle()
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
        # And an object main has not, which the result of a merge has
        doc.openTransaction("side: a ball")
        ball = doc.addObject("Part::Sphere", "Ball")
        ball.Placement.Base = App.Vector(40, 0, 0)
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
            seen = {"dock": dock, "doc": doc}
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
                                        "base against theirs", "ours against the result",
                                        "nothing"])
            check("dialog: base against ours, base against theirs",
                  (seen.get("base ours") or {}).get("by.name") == 7
                  and (seen.get("base theirs") or {}).get("by.name") == 7
                  and seen.get("base ours") != seen.get("theirs"))
            check("dialog: nothing, when asked", seen.get("nothing") == {})
            check("dialog: a length selected shows nothing", seen.get("a length") == {})
            check("dialog: the shape again", (seen.get("again") or {}).get("by.name") == 7)
            # Ours is 10 x 11 and theirs 14 x 12, both 12 high with the hole
            # through them: theirs has all of ours and more
            material = seen.get("material") or {}
            check("dialog: by its material, the default: what theirs has more (%r, %r)"
                  % (seen.get("mode"), seen.get("material said")),
                  seen.get("mode") == "Material" and material.get("material") == 1
                  and abs(material.get("volume.added", 0) - (14 * 12 - 10 * 11) * 12) < 1e-3
                  and material.get("volume.removed") == 0
                  and "by.name" not in material
                  and " added, " in (seen.get("material said") or ""))
            # Ours is 10 x 11 and the result 14 x 11: theirs' length, ours'
            # width, which a merge of the two makes and neither side has
            result = seen.get("result") or {}
            check("result: made when asked for, in a document of its own (%r, %r)"
                  % (seen.get("result docs before"), seen.get("result docs")),
                  seen.get("result docs before") == [] and len(seen.get("result docs") or []) == 1)
            check("result: ours against it, by the material (%r)" % seen.get("result said"),
                  result.get("material") == 1
                  and abs(result.get("volume.added", 0) - (14 * 11 - 10 * 11) * 12) < 1e-3
                  and result.get("volume.removed") == 0)
            check("result: nothing of the document moved -- no step, no redo lost, no row "
                  "but the tip's version (%r)" % (seen.get("result left"),),
                  (seen.get("result left") or [False])[0] is True
                  and set(seen["result left"][1]) <= {"snapshot"})
            check("result: the dialog is still the one open", seen.get("result open") is True)
            whole = seen.get("result whole") or ({}, {}, "")
            check("result: with no change selected, of the object chosen and no other (%r)"
                  % (whole[2],), whole[0].get("by.name", 0) > 0 and whole[1] == {})
            made = seen.get("made") or (None, None, None, None)
            check("result: what the merge makes and the document has not is drawn in its "
                  "view, all of it new (%r)" % (made,),
                  made[0] is True and (made[1] or {}).get("added", 0) >= 1
                  and (made[1] or {}).get("same", 0) == 0
                  and made[2] in (True, "no pivy") and made[3] is False)
            alone = seen.get("made alone") or (None, None)
            check("result: the line that makes it selected, that object alone (%r)" % (alone,),
                  (alone[0] or {}).get("added", 0) >= 1 and alone[1] == {})
            other = seen.get("made other pair") or (None, None)
            check("result: another pair chosen, nothing shown of it (%r)" % (other,),
                  other[0] == {})
            check("result: asked again, the one made is shown",
                  seen.get("result once") == seen.get("result docs"))
            check("result: stale with the dialog, its document closed and none made (%r)"
                  % (seen.get("stale result"),),
                  (seen.get("stale result") or [None])[0] == []
                  and "preview again" in (seen.get("stale result") or ["", ""])[1])
            check("a conflict starts on the side the setting says (%r)"
                  % (seen.get("starts on"),), seen.get("starts on") == ["theirs"])
            theirs2 = seen.get("side theirs") or ({}, "", [])
            check("result: with theirs' side, the length theirs gave it (%r)" % (theirs2[1],),
                  theirs2[0].get("material") == 1
                  and abs(theirs2[0].get("volume.removed", 0) - (15 - 14) * 13 * 12) < 1e-3
                  and len(theirs2[2]) == 1)
            ours2 = seen.get("side ours") or ({}, "", [])
            check("result: the side changed, it is made again -- ours kept, and the cut "
                  "is what it was (%r)" % (ours2[1],),
                  ours2[0].get("volume.removed", -1) == 0
                  and ours2[0].get("volume.added", -1) == 0
                  and len(ours2[2]) == 1 and ours2[2] != theirs2[2])
            check("result: gone with the dialog",
                  [n for n in App.listDocuments() if "_merged" in n] == [])
            check("dialog closed: nothing left in the view",
                  view.getValueDiff() == {} and drawn(cut)[0] is None
                  and doc.getObject("Cut2").ViewObject.getValueDiff() == {})
            check("dialog: not modal, and an object is picked from it (%r, %r)"
                  % (seen.get("asks"), seen.get("picked")),
                  seen.get("not modal") is True
                  and (seen.get("asks") or "").startswith("Pick an object")
                  and seen.get("picked") == (7, True))
            check("dialog: a second merge asked for meanwhile opens no second one",
                  seen.get("one at a time") == 1)
            check("dialog: a change the merge does not see leaves it standing (%r)"
                  % (seen.get("stands"),), seen.get("stands") == (False, True, False))
            check("dialog: one it does makes it stale, and nothing is merged from it (%r)"
                  % (seen.get("stale"),), seen.get("stale") == (True, False, True))
            check("dialog: previewed again, the length is a conflict now (%r)"
                  % (seen.get("again says"),),
                  seen.get("again found") is True
                  and [t.split()[0] for t in seen.get("again says") or []] == ["conflict"]
                  and seen.get("again fresh") == (False, True))
            check("dialog cancelled: no row written but the two edits (%r)" % seen.get("rows"),
                  len(doc.getTransactionLog()) == seen.get("rows")
                  and seen.get("rows", 0) >= rows + 2)
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
