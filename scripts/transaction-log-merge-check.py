# GUI check of docs/TransactionLog.md sec 28: a branch merged in the running
# application through the log panel's dialog -- the picker's rows, a side
# picked for a conflict, the view providers following the merge, and the
# merge row in the list. One GUI run in a fresh user home, given this script
# at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-mc \
#     MERGECHECK_OUT=/tmp/mc/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-merge-check.py
#
# It writes PASS/FAIL lines to $MERGECHECK_OUT and exits. The log setting is
# put back before it exits.
import os, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["MERGECHECK_OUT"]
lines = []


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def colour(obj):
    return tuple(round(c, 3) for c in obj.ViewObject.ShapeColor[:3])


def settle():
    for _ in range(5):
        QtWidgets.QApplication.processEvents()


def panel():
    dock = None
    for w in Gui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "Gui::DockWnd::TransactionLogView":
            dock = w
    if not dock:
        return None, None
    parent = dock.parentWidget()
    if isinstance(parent, QtWidgets.QDockWidget):
        parent.show()
    dock.show()
    settle()
    tree = None
    for t in dock.findChildren(QtWidgets.QTreeWidget):
        if t.headerItem().text(1) == "Seq":
            tree = t
    return dock, tree


def column(tree, title):
    header = tree.headerItem()
    for c in range(header.columnCount()):
        if header.text(c) == title:
            return c
    return -1


def drive(seen):
    # The merge dialog is modal: this runs inside its event loop.
    dialog = QtWidgets.QApplication.activeModalWidget()
    if dialog is None or dialog.objectName() != "TransactionMergeDialog":
        seen["found"] = False
        if dialog is not None:
            dialog.reject()
        return
    seen["found"] = True
    tree = dialog.findChild(QtWidgets.QTreeWidget)
    rows = []
    for i in range(tree.topLevelItemCount()):
        item = tree.topLevelItem(i)
        side = tree.itemWidget(item, column(tree, "Takes"))
        rows.append(
            {
                "kind": item.text(0),
                "object": item.text(1),
                "prop": item.text(2),
                "ours": item.text(column(tree, "Ours")),
                "theirs": item.text(column(tree, "Theirs")),
                "side": side.currentText() if side is not None else None,
                "bold": item.font(0).bold(),
            }
        )
        if side is not None and item.text(0).startswith("conflict") and item.text(2) == "Width":
            side.setCurrentIndex(side.findText("theirs"))
    seen["rows"] = rows
    seen["title"] = dialog.windowTitle()
    for b in dialog.findChildren(QtWidgets.QPushButton):
        if b.objectName() == "merge":
            b.click()
            return
    dialog.reject()


def run():
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 2)
    try:
        p.SetInt("TransactionLog", 1)
        doc = App.newDocument("Merge")
        doc.openTransaction("box")
        box = doc.addObject("Part::Box", "Box")
        doc.commitTransaction()
        doc.recompute()
        doc.openTransaction("red")
        box.ViewObject.ShapeColor = (1.0, 0.0, 0.0)
        doc.commitTransaction()

        doc.createTransactionBranch("side")
        doc.openTransaction("longer, narrower")
        box.Length = 30
        box.Width = 5
        doc.recompute()
        doc.commitTransaction()
        doc.openTransaction("blue")
        box.ViewObject.ShapeColor = (0.0, 0.0, 1.0)
        doc.commitTransaction()
        doc.openTransaction("cylinder")
        cyl = doc.addObject("Part::Cylinder", "Cylinder")
        doc.recompute()
        doc.commitTransaction()
        doc.openTransaction("green")
        cyl.ViewObject.ShapeColor = (0.0, 1.0, 0.0)
        doc.commitTransaction()
        settle()

        doc.switchTransactionBranch("main")
        settle()
        box = doc.getObject("Box")
        doc.openTransaction("lower, narrow")
        box.Height = 5
        box.Width = 7
        doc.recompute()
        doc.commitTransaction()
        doc.openTransaction("yellow")
        box.ViewObject.ShapeColor = (1.0, 1.0, 0.0)
        doc.commitTransaction()
        settle()

        preview = doc.previewTransactionMerge("side")
        kinds = {c["key"]: c["kind"] for c in preview["changes"]}
        check("preview: width conflicts", kinds.get("Box.Width") == "conflict")
        check("preview: length taken", kinds.get("Box.Length") == "take")
        check("preview: the colour is a view conflict (%r)" % kinds.get("view:Box.ShapeColor"),
              kinds.get("view:Box.ShapeColor") == "view")
        check("preview: one conflict", preview["conflicts"] == 1)

        dock, tree = panel()
        check("panel found", dock is not None and tree is not None)
        if dock is not None:
            undos = doc.UndoCount
            seen = {}
            QtCore.QTimer.singleShot(300, lambda: drive(seen))
            QtCore.QMetaObject.invokeMethod(
                dock, "mergeBranch", QtCore.Qt.DirectConnection, QtCore.Q_ARG(str, "side")
            )
            settle()
            check("dialog shown (%r)" % seen.get("title"), seen.get("found") is True)
            rows = seen.get("rows", [])
            check("dialog: the conflict first, bold, on ours",
                  bool(rows) and rows[0]["kind"].startswith("conflict")
                  and rows[0]["prop"] == "Width" and rows[0]["bold"] and rows[0]["side"] == "ours")
            check("dialog: its values as text (%r / %r)"
                  % (rows[0]["ours"] if rows else None, rows[0]["theirs"] if rows else None),
                  bool(rows) and "7" in rows[0]["ours"] and "5" in rows[0]["theirs"])
            views = [r for r in rows if r["kind"].startswith("view")]
            check("dialog: view conflicts keep ours (%d)" % len(views),
                  bool(views) and all(r["side"] == "ours" for r in views))
            check("dialog: a created object is one row",
                  len([r for r in rows if r["object"] == "Cylinder"]) == 1)

            box = doc.getObject("Box")
            cyl = doc.getObject("Cylinder")
            check("merged: length 30, theirs", abs(box.Length.Value - 30) < 1e-9)
            check("merged: height 5, ours", abs(box.Height.Value - 5) < 1e-9)
            check("merged: width 5, the side picked", abs(box.Width.Value - 5) < 1e-9)
            check("merged: recomputed, volume %g" % box.Shape.Volume,
                  abs(box.Shape.Volume - 30 * 5 * 5) < 1e-6)
            check("merged: box yellow, ours %r" % (colour(box),), colour(box) == (1.0, 1.0, 0.0))
            check("merged: cylinder in, green",
                  cyl is not None and cyl.ViewObject is not None and colour(cyl) == (0.0, 1.0, 0.0))
            check("merged: one step", doc.UndoCount == undos + 1)

            kcol = column(tree, "Kind")
            mcol = column(tree, "Merged")
            merges = [tree.topLevelItem(i) for i in range(tree.topLevelItemCount())
                      if tree.topLevelItem(i).text(kcol) == "merge"]
            side = [b for b in doc.getTransactionBranches() if b["name"] == "side"][0]
            check("panel: the merge row names what it merged (%s)"
                  % (merges[0].text(mcol) if merges else None),
                  len(merges) == 1 and merges[0].text(mcol) == str(side["head"]))
            # Every branch shown: the graph draws the second parent.
            for c in dock.findChildren(QtWidgets.QCheckBox):
                if c.text().replace("&", "") == "All branches":
                    c.setChecked(True)
            settle()
            shown = [tree.topLevelItem(i) for i in range(tree.topLevelItemCount())
                     if not tree.topLevelItem(i).isHidden()]
            bcol = column(tree, "Branch")
            check("panel: both branches listed with the merge",
                  {i.text(bcol) for i in shown} == {"main", "side"})

            doc.undo()
            settle()
            box = doc.getObject("Box")
            check("undone: width 7, length 10, no cylinder",
                  abs(box.Width.Value - 7) < 1e-9 and abs(box.Length.Value - 10) < 1e-9
                  and doc.getObject("Cylinder") is None)
            check("undone: volume %g" % box.Shape.Volume, abs(box.Shape.Volume - 10 * 7 * 5) < 1e-6)
            doc.redo()
            settle()
            box = doc.getObject("Box")
            cyl = doc.getObject("Cylinder")
            check("redone: volume %g" % box.Shape.Volume, abs(box.Shape.Volume - 750) < 1e-6)
            check("redone: cylinder green",
                  cyl is not None and colour(cyl) == (0.0, 1.0, 0.0))

            # Nothing more of side: no dialog, no row.
            before = len(doc.getTransactionLog())
            QtCore.QMetaObject.invokeMethod(
                dock, "mergeBranch", QtCore.Qt.DirectConnection, QtCore.Q_ARG(str, "side")
            )
            settle()
            check("again: nothing to merge", len(doc.getTransactionLog()) == before)

            # Ours unchanged since the base: theirs whole, colour included.
            doc.createTransactionBranch("next")
            doc.openTransaction("taller")
            box.Height = 20
            doc.recompute()
            doc.commitTransaction()
            doc.openTransaction("white")
            box.ViewObject.ShapeColor = (1.0, 1.0, 1.0)
            doc.commitTransaction()
            doc.switchTransactionBranch("main")
            settle()
            box = doc.getObject("Box")
            check("next: main as it was", abs(box.Height.Value - 5) < 1e-9
                  and colour(box) == (1.0, 1.0, 0.0))
            preview = doc.previewTransactionMerge("next")
            check("next: taken whole", preview["fast_forward"] and preview["conflicts"] == 0)
            result = doc.mergeTransactionBranch("next")
            settle()
            check("next: merged", result["seq"] > 0 and not result["unresolved"])
            check("next: volume %g, white %r" % (box.Shape.Volume, colour(box)),
                  abs(box.Shape.Volume - 30 * 5 * 20) < 1e-6 and colour(box) == (1.0, 1.0, 1.0))
        # Sec 31.5: a sketch is one thing to a merge. Ours removes a line,
        # theirs constrains it: one conflict in the dialog, the properties
        # under it going the way it is picked.
        import tempfile

        import Part
        import Sketcher

        sdoc = App.newDocument("MergeSketch")
        sdoc.UndoMode = 1
        sdoc.openTransaction("base")
        sk = sdoc.addObject("Sketcher::SketchObject", "Sketch")
        for x in (0, 20, 40, 60):
            sk.addGeometry(Part.LineSegment(App.Vector(x, 0, 0), App.Vector(x + 10, 0, 0)))
        sdoc.recompute()
        sdoc.commitTransaction()
        settle()
        sdoc.saveAs(os.path.join(tempfile.mkdtemp(prefix="mergesketch-"), "sketch.FCStd"))
        sdoc.createTransactionBranch("side")
        sdoc.switchTransactionBranch("side")
        sdoc.openTransaction("theirs")
        sdoc.Sketch.addConstraint(Sketcher.Constraint("DistanceX", 2, 1, 2, 2, 10))
        sdoc.recompute()
        sdoc.commitTransaction()
        settle()
        sdoc.switchTransactionBranch("main")
        sdoc.openTransaction("ours")
        sdoc.Sketch.delGeometry(2)
        sdoc.recompute()
        sdoc.commitTransaction()
        settle()
        App.setActiveDocument(sdoc.Name)
        settle()
        sdock, _ = panel()

        def driveUnit(seen):
            dialog = QtWidgets.QApplication.activeModalWidget()
            if dialog is None or dialog.objectName() != "TransactionMergeDialog":
                seen["found"] = False
                if dialog is not None:
                    dialog.reject()
                return
            seen["found"] = True
            tree = dialog.findChild(QtWidgets.QTreeWidget)
            rows = []
            for i in range(tree.topLevelItemCount()):
                item = tree.topLevelItem(i)
                side = tree.itemWidget(item, column(tree, "Takes"))
                rows.append((item.text(0), item.text(2),
                             side.currentText() if side is not None
                             else item.text(column(tree, "Takes"))))
                if side is not None and item.text(0).startswith("conflict"):
                    side.setCurrentIndex(side.findText("theirs"))
            seen["rows"] = rows
            for b in dialog.findChildren(QtWidgets.QPushButton):
                if b.objectName() == "merge":
                    b.click()
                    return
            dialog.reject()

        seen = {}
        QtCore.QTimer.singleShot(300, lambda: driveUnit(seen))
        QtCore.QMetaObject.invokeMethod(sdock, "mergeBranch", QtCore.Qt.DirectConnection,
                                        QtCore.Q_ARG(str, "side"))
        settle()
        shown = seen.get("rows") or []
        check("sketch: one conflict in the dialog, with a side (%r)" % shown,
              seen.get("found") is True
              and [r[:2] for r in shown if r[0].startswith("conflict")]
              == [("conflict unit", "Geometry")])
        check("sketch: its properties under it, going as it goes",
              sorted(r for r in shown if r[0].startswith("unit"))
              == [("unit set", "Constraints", "as Sketch.Geometry"),
                  ("unit set", "Geometry", "as Sketch.Geometry")])
        sk = sdoc.Sketch
        ids = [sk.getGeometryId(i) for i in range(len(sk.Geometry))]
        check("sketch: merged as theirs, the distance on the line it was put on (%r, %r)"
              % (ids, [(c.Type, c.First) for c in sk.Constraints]),
              ids == [1, 2, 3, 4] and [(c.Type, ids[c.First]) for c in sk.Constraints]
              == [("DistanceX", 3)]
              and not [o.Name for o in sdoc.Objects if "Invalid" in o.State])
        App.closeDocument(sdoc.Name)
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
