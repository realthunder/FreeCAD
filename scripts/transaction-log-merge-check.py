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
        box.ViewObject.LineWidth = 5
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
        box.ViewObject.LineWidth = 3
        doc.commitTransaction()
        settle()

        preview = doc.previewTransactionMerge("side")
        kinds = {c["key"]: c["kind"] for c in preview["changes"]}
        check("preview: width conflicts", kinds.get("Box.Width") == "conflict")
        check("preview: length taken", kinds.get("Box.Length") == "take")
        # The colour is the object's (docs/ShapeAppearanceDesign.md sec
        # 14.6.5): merged by what it holds, and no row of the view's. What
        # is the view's own -- a line width -- is a view conflict still.
        check("preview: the colour is the object's, merged (%r)"
              % kinds.get("Box.ElementAppearance"),
              kinds.get("Box.ElementAppearance") == "merge"
              and "view:Box.ShapeColor" not in kinds)
        check("preview: the line width is a view conflict (%r)" % kinds.get("view:Box.LineWidth"),
              kinds.get("view:Box.LineWidth") == "view")
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
            check("merged: line width 3, ours", abs(box.ViewObject.LineWidth - 3) < 1e-9)
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
        # Sec 31.5, 31.10: a sketch is one thing to a merge, and is merged
        # by what it holds where that solves. Ours says a line is ten along
        # x, theirs that it is twenty-five long: together they do not
        # solve, and it is one conflict in the dialog, the properties under
        # it going the way it is picked.
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
        sdoc.Sketch.addConstraint(Sketcher.Constraint("Distance", 2, 1, 2, 2, 25))
        sdoc.recompute()
        sdoc.commitTransaction()
        settle()
        sdoc.switchTransactionBranch("main")
        sdoc.openTransaction("ours")
        sdoc.Sketch.addConstraint(Sketcher.Constraint("Horizontal", 2))
        sdoc.Sketch.addConstraint(Sketcher.Constraint("DistanceX", 2, 1, 2, 2, 10))
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
              == [("Distance", 3)]
              and not [o.Name for o in sdoc.Objects if "Invalid" in o.State])
        App.closeDocument(sdoc.Name)
        settle()

        # Sec 31.10: ours removes a line and adds one, theirs constrains
        # the removed one and another: nothing to pick. The line stays
        # removed, the constraint on it is left out and said so, and the
        # other is on its line, which is in another place now.
        edoc = App.newDocument("MergeSketchParts")
        edoc.UndoMode = 1
        edoc.openTransaction("base")
        sk = edoc.addObject("Sketcher::SketchObject", "Sketch")
        for x in (0, 20, 40, 60):
            sk.addGeometry(Part.LineSegment(App.Vector(x, 0, 0), App.Vector(x + 10, 0, 0)))
        edoc.recompute()
        edoc.commitTransaction()
        settle()
        edoc.saveAs(os.path.join(tempfile.mkdtemp(prefix="mergeparts-"), "parts.FCStd"))
        edoc.createTransactionBranch("side")
        edoc.switchTransactionBranch("side")
        edoc.openTransaction("theirs")
        edoc.Sketch.addConstraint(Sketcher.Constraint("DistanceX", 2, 1, 2, 2, 10))
        edoc.Sketch.addConstraint(Sketcher.Constraint("DistanceX", 3, 1, 3, 2, 12))
        edoc.recompute()
        edoc.commitTransaction()
        settle()
        edoc.switchTransactionBranch("main")
        edoc.openTransaction("ours")
        edoc.Sketch.delGeometry(2)
        edoc.Sketch.addGeometry(Part.LineSegment(App.Vector(0, 20, 0), App.Vector(5, 20, 0)))
        edoc.recompute()
        edoc.commitTransaction()
        settle()
        App.setActiveDocument(edoc.Name)
        settle()
        edock, _ = panel()

        def driveParts(seen):
            dialog = QtWidgets.QApplication.activeModalWidget()
            if dialog is None or dialog.objectName() != "TransactionMergeDialog":
                seen["found"] = False
                if dialog is not None:
                    dialog.reject()
                return
            seen["found"] = True
            tree = dialog.findChild(QtWidgets.QTreeWidget)
            takes = column(tree, "Takes")
            rows = []
            for i in range(tree.topLevelItemCount()):
                item = tree.topLevelItem(i)
                rows.append((item.text(0), item.text(2), item.text(takes),
                             tree.itemWidget(item, takes) is not None, item.toolTip(takes)))
            seen["rows"] = rows
            for b in dialog.findChildren(QtWidgets.QPushButton):
                if b.objectName() == "merge":
                    seen["enabled"] = b.isEnabled()
                    b.click()
                    return
            dialog.reject()

        seen = {}
        QtCore.QTimer.singleShot(300, lambda: driveParts(seen))
        QtCore.QMetaObject.invokeMethod(edock, "mergeBranch", QtCore.Qt.DirectConnection,
                                        QtCore.Q_ARG(str, "side"))
        settle()
        shown = sorted(r for r in (seen.get("rows") or []) if r[1] in ("Geometry", "Constraints"))
        check("sketch parts: merged by what it holds, nothing to pick (%r)" % shown,
              seen.get("found") is True and seen.get("enabled") is True
              and [r[:4] for r in shown] == [("merge set", "Constraints", "both", False),
                                             ("merge set", "Geometry", "both", False)]
              and "left out" in shown[0][4] and "g3" in shown[0][4])
        sk = edoc.Sketch
        ids = [sk.getGeometryId(i) for i in range(len(sk.Geometry))]
        check("sketch parts: the line stays removed, the other distance on its line (%r, %r)"
              % (ids, [(c.Type, c.First) for c in sk.Constraints]),
              ids == [1, 2, 4, 5] and [(c.Type, ids[c.First], c.Value) for c in sk.Constraints]
              == [("DistanceX", 4, 12.0)]
              and not [o.Name for o in edoc.Objects if "Invalid" in o.State])
        App.closeDocument(edoc.Name)
        settle()

        # Sec 31.11: what an expression names by its place is named before
        # the merge. Theirs takes the first constraint out; ours binds a
        # cylinder to the second, as `Constraints[1]`. The dialog shows the
        # naming, and the cylinder says the name after.
        ndoc = App.newDocument("MergeNamed")
        ndoc.UndoMode = 1
        ndoc.openTransaction("base")
        sk = ndoc.addObject("Sketcher::SketchObject", "Sketch")
        for x in (0, 20, 40, 60):
            sk.addGeometry(Part.LineSegment(App.Vector(x, 0, 0), App.Vector(x + 10, 0, 0)))
        sk.addConstraint(Sketcher.Constraint("Horizontal", 0))
        sk.addConstraint(Sketcher.Constraint("DistanceX", 0, 1, 0, 2, 10))
        ndoc.addObject("Part::Cylinder", "Cyl")
        ndoc.recompute()
        ndoc.commitTransaction()
        settle()
        ndoc.saveAs(os.path.join(tempfile.mkdtemp(prefix="mergenamed-"), "named.FCStd"))
        ndoc.createTransactionBranch("side")
        ndoc.openTransaction("theirs")
        ndoc.Sketch.delConstraint(0)
        ndoc.recompute()
        ndoc.commitTransaction()
        settle()
        ndoc.switchTransactionBranch("main")
        ndoc.openTransaction("ours")
        ndoc.Cyl.setExpression("Height", "Sketch.Constraints[1]")
        ndoc.recompute()
        ndoc.commitTransaction()
        settle()
        App.setActiveDocument(ndoc.Name)
        settle()
        ndock, _ = panel()
        seen = {}
        QtCore.QTimer.singleShot(300, lambda: driveParts(seen))
        QtCore.QMetaObject.invokeMethod(ndock, "mergeBranch", QtCore.Qt.DirectConnection,
                                        QtCore.Q_ARG(str, "side"))
        settle()
        shown = sorted(r for r in (seen.get("rows") or []) if r[0].startswith("name"))
        check("named: the dialog says what is named (%r)" % shown,
              seen.get("found") is True and seen.get("enabled") is True
              and [r[:4] for r in shown] == [("name set", "Constraints", "named", False),
                                             ("name set", "ExpressionEngine", "named", False)]
              and "Constraints[1] -> DistanceX_g1p1_g1p2" in shown[0][4])
        check("named: the constraint has its name and the cylinder says it (%r, %r)"
              % ([c.Name for c in ndoc.Sketch.Constraints], ndoc.Cyl.ExpressionEngine),
              [c.Name for c in ndoc.Sketch.Constraints] == ["DistanceX_g1p1_g1p2"]
              and ndoc.Cyl.ExpressionEngine
              == [("Height", "Sketch.Constraints.DistanceX_g1p1_g1p2")]
              and abs(ndoc.Cyl.Height.Value - 10) < 1e-9
              and not [o.Name for o in ndoc.Objects if "Invalid" in o.State])
        App.closeDocument(ndoc.Name)
        settle()

        # Sec 31.8: a sheet whose cells both branches set, each another, is
        # merged by its cells -- shown as that, with nothing to pick.
        cdoc = App.newDocument("MergeCells")
        cdoc.UndoMode = 1
        cdoc.openTransaction("base")
        sheet = cdoc.addObject("Spreadsheet::Sheet", "Sheet")
        sheet.set("A1", "1")
        cdoc.recompute()
        cdoc.commitTransaction()
        settle()
        cdoc.saveAs(os.path.join(tempfile.mkdtemp(prefix="mergecells-"), "cells.FCStd"))
        cdoc.createTransactionBranch("side")
        cdoc.switchTransactionBranch("side")
        cdoc.openTransaction("theirs")
        cdoc.Sheet.set("B1", "2")
        cdoc.recompute()
        cdoc.commitTransaction()
        settle()
        cdoc.switchTransactionBranch("main")
        cdoc.openTransaction("ours")
        cdoc.Sheet.set("C1", "3")
        cdoc.recompute()
        cdoc.commitTransaction()
        settle()
        App.setActiveDocument(cdoc.Name)
        settle()
        cdock, _ = panel()

        def driveCells(seen):
            dialog = QtWidgets.QApplication.activeModalWidget()
            if dialog is None or dialog.objectName() != "TransactionMergeDialog":
                seen["found"] = False
                if dialog is not None:
                    dialog.reject()
                return
            seen["found"] = True
            tree = dialog.findChild(QtWidgets.QTreeWidget)
            takes = column(tree, "Takes")
            rows = []
            for i in range(tree.topLevelItemCount()):
                item = tree.topLevelItem(i)
                rows.append((item.text(0), item.text(2), item.text(takes),
                             tree.itemWidget(item, takes) is not None, item.toolTip(takes)))
            seen["rows"] = rows
            for b in dialog.findChildren(QtWidgets.QPushButton):
                if b.objectName() == "merge":
                    seen["enabled"] = b.isEnabled()
                    b.click()
                    return
            dialog.reject()

        seen = {}
        QtCore.QTimer.singleShot(300, lambda: driveCells(seen))
        QtCore.QMetaObject.invokeMethod(cdock, "mergeBranch", QtCore.Qt.DirectConnection,
                                        QtCore.Q_ARG(str, "side"))
        settle()
        shown = [r for r in (seen.get("rows") or []) if r[1] == "cells"]
        check("cells: merged by what they hold, nothing to pick (%r)" % shown,
              seen.get("found") is True and seen.get("enabled") is True
              and [r[:4] for r in shown] == [("merge set", "cells", "both", False)]
              and "B1" in shown[0][4])
        got = {c: cdoc.Sheet.getContents(c) for c in cdoc.Sheet.getUsedCells()}
        check("cells: each side's cell in the sheet (%r)" % got,
              got == {"A1": "1", "B1": "2", "C1": "3"}
              and not [o.Name for o in cdoc.Objects if "Invalid" in o.State])
        App.closeDocument(cdoc.Name)
        settle()

        # Sec 31.12: picking by hand, as an operation of its own. A change
        # that asks nothing is unticked in the dialog and left out; and a
        # row of another branch is applied here from the panel.
        pdoc = App.newDocument("MergePicked")
        pdoc.UndoMode = 1
        pdoc.openTransaction("base")
        pdoc.addObject("Part::Box", "Box")
        pdoc.recompute()
        pdoc.commitTransaction()
        settle()
        pdoc.saveAs(os.path.join(tempfile.mkdtemp(prefix="mergepicked-"), "picked.FCStd"))
        pdoc.createTransactionBranch("side")
        seqs = {}
        for name, prop, value in (("long", "Length", 20), ("narrow", "Width", 5),
                                  ("high", "Height", 30)):
            pdoc.openTransaction(name)
            setattr(pdoc.Box, prop, value)
            pdoc.recompute()
            pdoc.commitTransaction()
            settle()
            seqs[name] = [t["seq"] for t in pdoc.getTransactionLog() if t["name"] == name][-1]
        pdoc.createTransactionBranch("other", 0, seqs["narrow"])
        pdoc.switchTransactionBranch("main")
        pdoc.openTransaction("ours")
        pdoc.Box.Label = "Ours"
        pdoc.commitTransaction()
        settle()
        App.setActiveDocument(pdoc.Name)
        settle()
        pdock, _ = panel()

        def drivePick(seen, untick=None):
            dialog = QtWidgets.QApplication.activeModalWidget()
            if dialog is None or dialog.objectName() != "TransactionMergeDialog":
                seen["found"] = False
                if dialog is not None:
                    dialog.reject()
                return
            seen["found"] = True
            seen["title"] = dialog.windowTitle()
            tree = dialog.findChild(QtWidgets.QTreeWidget)
            rows = []
            for i in range(tree.topLevelItemCount()):
                item = tree.topLevelItem(i)
                ticked = item.data(0, QtCore.Qt.CheckStateRole) is not None
                rows.append((item.text(0), item.text(2), ticked))
                if ticked and item.text(2) == untick:
                    item.setCheckState(0, QtCore.Qt.Unchecked)
            seen["rows"] = rows
            for b in dialog.findChildren(QtWidgets.QPushButton):
                if b.objectName() == "merge":
                    seen["button"] = b.text().replace("&", "")
                    b.click()
                    return
            dialog.reject()

        box = lambda: (pdoc.Box.Length.Value, pdoc.Box.Width.Value, pdoc.Box.Height.Value)
        # The row that made the box high, of `side`, applied here alone.
        seen = {}
        QtCore.QTimer.singleShot(300, lambda: drivePick(seen))
        QtCore.QMetaObject.invokeMethod(pdock, "applyRows", QtCore.Qt.DirectConnection,
                                        QtCore.Q_ARG(str, str(seqs["high"])))
        settle()
        check("picked: a row applied from the panel (%r, %r, %r)"
              % (seen.get("title"), seen.get("button"), seen.get("rows")),
              seen.get("found") is True and seen.get("button") == "Apply"
              and sorted(seen.get("rows") or [])
              == [("derived set", "Shape", False), ("take set", "Height", True)]
              and box() == (10.0, 10.0, 30.0))
        kinds = [t["kind"] for t in pdoc.getTransactionLog()]
        check("picked: a row of its own kind, and no branch merged (%r)" % kinds[-3:],
              "pick" in kinds and "merge" not in kinds
              and ("take", "Box.Length") in [(c["kind"], c["key"])
                                              for c in pdoc.previewTransactionMerge("other")["changes"]])
        # `other` merged with its width unticked: the length goes in.
        seen = {}
        QtCore.QTimer.singleShot(300, lambda: drivePick(seen, untick="Width"))
        QtCore.QMetaObject.invokeMethod(pdock, "mergeBranch", QtCore.Qt.DirectConnection,
                                        QtCore.Q_ARG(str, "other"))
        settle()
        check("left out: a change unticked in the dialog stays ours (%r, %r)"
              % (seen.get("rows"), box()),
              seen.get("found") is True and seen.get("button") == "Merge"
              and ("take set", "Width", True) in (seen.get("rows") or [])
              and box() == (20.0, 10.0, 30.0)
              and not [o.Name for o in pdoc.Objects if "Invalid" in o.State])
        App.closeDocument(pdoc.Name)
        settle()

        # docs/ShapeAppearanceDesign.md sec 14.6.10, "The merge of a link's
        # store": what a link lays over what it shows is the link's, and is
        # merged by what it is given to as a shape's looks are. Theirs
        # colours a face through the link's view provider; ours gives the
        # link a look for all it shows. Both are there after, in the object
        # and in the view provider that draws them.
        ldoc = App.newDocument("MergeLink")
        ldoc.UndoMode = 1
        ldoc.openTransaction("base")
        lbox = ldoc.addObject("Part::Box", "Box")
        link = ldoc.addObject("App::Link", "Link")
        link.LinkedObject = lbox
        link.Placement.Base = App.Vector(20, 0, 0)
        ldoc.recompute()
        ldoc.commitTransaction()
        settle()
        ldoc.saveAs(os.path.join(tempfile.mkdtemp(prefix="mergelink-"), "link.FCStd"))
        ldoc.createTransactionBranch("side")
        ldoc.openTransaction("theirs: a face")
        link.ViewObject.setElementColors({"Face6": (1.0, 0.0, 0.0)})
        ldoc.commitTransaction()
        settle()
        ldoc.switchTransactionBranch("main")
        settle()
        link = ldoc.getObject("Link")
        check("link: ours has no face coloured before (%r)" % (link.ElementAppearance.keys(),),
              link.ElementAppearance.keys() == [] and not link.ViewObject.getElementColors())
        ldoc.openTransaction("ours: a look for all")
        link.ViewObject.OverrideMaterial = True
        over = link.ViewObject.ShapeAppearance.Base
        over.DiffuseColor = (0.0, 1.0, 0.0, 1.0)
        link.ViewObject.ShapeAppearance.Base = over
        ldoc.commitTransaction()
        settle()
        preview = ldoc.previewTransactionMerge("side")
        kinds = {c["key"]: c["kind"] for c in preview["changes"]}
        check("link: the looks are merged, and no conflict (%r)" % kinds,
              kinds.get("Link.ElementAppearance") == "merge" and preview["conflicts"] == 0
              and not [k for k in kinds if k.startswith("view:Link.")])
        merged = ldoc.mergeTransactionBranch("side")
        settle()
        link = ldoc.getObject("Link")
        lea = link.ElementAppearance
        shown = {k: tuple(round(c, 3) for c in v[:3])
                 for k, v in link.ViewObject.getElementColors().items()}
        check("link: merged with nothing left to ask (%r)" % (merged["unresolved"],),
              merged["unresolved"] == [] and merged["failed"] == [])
        check("link: the object has both (%r, %r)" % (lea.keys(), link.OverrideMaterial),
              lea.keys() == ["Face6"] and link.OverrideMaterial and "Face" in lea
              and tuple(round(c, 3) for c in lea.Face.DiffuseColor[:3]) == (0.0, 1.0, 0.0)
              and tuple(round(c, 3) for c in lea["Face6"].DiffuseColor[:3]) == (1.0, 0.0, 0.0))
        check("link: its view provider draws both (%r, %r)"
              % (shown, link.ViewObject.OverrideMaterial),
              shown.get("Face6") == (1.0, 0.0, 0.0) and link.ViewObject.OverrideMaterial
              and tuple(round(c, 3) for c in link.ViewObject.ShapeAppearance.Base.DiffuseColor[:3])
              == (0.0, 1.0, 0.0))
        ldoc.undo()
        settle()
        check("link: an undo takes theirs' face back and leaves ours' look (%r)"
              % (link.ElementAppearance.keys(),),
              link.ElementAppearance.keys() == [] and link.OverrideMaterial
              and not [k for k in link.ViewObject.getElementColors() if k != "Face"])
        App.closeDocument(ldoc.Name)
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
