# GUI check of docs/TransactionLog.md sec 26: branches switched in place in
# the running application -- the model and the view providers follow a
# switch, and the log panel's switcher and row filter work. One GUI run in a
# fresh user home, given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-bc \
#     BRANCHCHECK_OUT=/tmp/bc/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-branch-check.py
#
# It writes PASS/FAIL lines to $BRANCHCHECK_OUT and exits. The log setting
# is put back before it exits.
import os, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["BRANCHCHECK_OUT"]
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
        return None, None, None
    parent = dock.parentWidget()
    if isinstance(parent, QtWidgets.QDockWidget):
        parent.show()
    dock.show()
    settle()
    combo = None
    for c in dock.findChildren(QtWidgets.QComboBox):
        if c.toolTip().startswith("The branch"):
            combo = c
    tree = None
    for t in dock.findChildren(QtWidgets.QTreeWidget):
        if t.headerItem().text(1) == "Seq":
            tree = t
    return dock, combo, tree


def column(tree, title):
    header = tree.headerItem()
    for c in range(header.columnCount()):
        if header.text(c) == title:
            return c
    return -1


def run():
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 2)
    try:
        p.SetInt("TransactionLog", 1)
        doc = App.newDocument("Branches")
        doc.openTransaction("box")
        box = doc.addObject("Part::Box", "Box")
        doc.commitTransaction()
        doc.recompute()
        doc.openTransaction("red")
        box.ViewObject.ShapeColor = (1.0, 0.0, 0.0)
        doc.commitTransaction()

        doc.createTransactionBranch("side")
        doc.openTransaction("longer")
        box.Length = 30
        doc.commitTransaction()
        doc.recompute()
        doc.openTransaction("blue")
        box.ViewObject.ShapeColor = (0.0, 0.0, 1.0)
        doc.commitTransaction()
        doc.openTransaction("cylinder")
        cyl = doc.addObject("Part::Cylinder", "Cylinder")
        doc.commitTransaction()
        doc.recompute()
        cyl.ViewObject.ShapeColor = (0.0, 1.0, 0.0)
        settle()

        doc.switchTransactionBranch("main")
        settle()
        check("main: length 10", abs(box.Length.Value - 10) < 1e-9)
        check("main: volume 1000", abs(box.Shape.Volume - 1000) < 1e-6)
        check("main: box red %r" % (colour(box),), colour(box) == (1.0, 0.0, 0.0))
        check("main: no cylinder", doc.getObject("Cylinder") is None)
        names = doc.UndoNames
        check(
            "main: its own steps %r" % (names,),
            "red" in names and "box" in names and "longer" not in names and "blue" not in names,
        )

        doc.switchTransactionBranch("side")
        settle()
        box = doc.getObject("Box")
        cyl = doc.getObject("Cylinder")
        check("side: length 30", abs(box.Length.Value - 30) < 1e-9)
        check("side: volume 3000", abs(box.Shape.Volume - 3000) < 1e-6)
        check("side: box blue %r" % (colour(box),), colour(box) == (0.0, 0.0, 1.0))
        check("side: cylinder back, green", cyl is not None and colour(cyl) == (0.0, 1.0, 0.0))
        check("side: cylinder has a view provider", cyl is not None and cyl.ViewObject is not None)
        names = doc.UndoNames
        check("side: its own steps %r" % (names,), "longer" in names and "blue" in names)

        dock, combo, tree = panel()
        check("panel found", dock is not None and combo is not None and tree is not None)
        if combo is not None:
            items = [combo.itemData(i) for i in range(combo.count())]
            check("panel: branches %r" % (items,), items == ["main", "side"])
            check("panel: on side", combo.currentText() == "side")
            seqs = [int(tree.topLevelItem(i).text(column(tree, "Seq")))
                    for i in range(tree.topLevelItemCount())]
            check("panel: newest first", seqs == sorted(seqs, reverse=True))
            # The graph is a pane of its own, scrolled with the list.
            graph = [v for v in dock.findChildren(QtWidgets.QTreeView)
                     if not isinstance(v, QtWidgets.QTreeWidget)]
            check("panel: graph pane", len(graph) == 1)
            if graph:
                g = graph[0]
                g.parentWidget().setSizes([150, 400])
                # Short enough that the list has to scroll.
                if isinstance(dock.parentWidget(), QtWidgets.QDockWidget):
                    dock.parentWidget().resize(700, 250)
                settle()
                bar = tree.verticalScrollBar()
                bar.setValue(bar.maximum())
                settle()
                check("graph pane follows the list (%d/%d)"
                      % (g.verticalScrollBar().value(), bar.value()),
                      bar.maximum() > 0 and g.verticalScrollBar().value() == bar.value())
                g.verticalScrollBar().setValue(0)
                settle()
                check("list follows the graph pane", bar.value() == 0)
                # A row's band runs on to the pane's edge, in step with the
                # list's -- with rows hidden above, so not by row number.
                if isinstance(dock.parentWidget(), QtWidgets.QDockWidget):
                    dock.parentWidget().resize(700, 520)
                g.parentWidget().setSizes([320, 380])
                tree.clearSelection()
                tree.setCurrentItem(None)
                settle()
                edge = g.header().length() - g.header().offset()
                wide = g.viewport().width()
                pane = g.viewport().grab().toImage()
                rows = tree.viewport().grab().toImage()
                seen = []
                index = g.indexAt(QtCore.QPoint(2, 2))
                while index.isValid():
                    rect = g.visualRect(index)
                    if rect.bottom() >= g.viewport().height():
                        break
                    y = rect.top() + 1
                    seen.append((pane.pixel(edge - 2, y), pane.pixel(wide - 3, y),
                                 rows.pixel(1, y)))
                    index = g.indexBelow(index)
                check("graph pane: wider than its column (%d/%d), %d rows"
                      % (edge, wide, len(seen)), wide > edge + 20 and len(seen) >= 4)
                check("graph pane: a row's band reaches the pane's edge",
                      all(a == b for a, b, c in seen))
                check("graph pane: the bands are the list's (%d colours)"
                      % len({b for a, b, c in seen}),
                      all(b == c for a, b, c in seen) and len({b for a, b, c in seen}) == 2)
            shown = [tree.topLevelItem(i) for i in range(tree.topLevelItemCount())]
            scol = column(tree, "Seq")
            visible = [i.text(scol) for i in shown if not i.isHidden()]
            bcol = column(tree, "Branch")
            others = [i for i in shown if i.isHidden() and i.text(bcol) == "main"]
            check("panel: main-only rows hidden (%d)" % len(others), len(others) > 0)
            # Switching through the panel.
            combo.activated.emit(items.index("main"))
            settle()
            box = doc.getObject("Box")
            check("panel switch: on main", combo.currentText() == "main")
            check("panel switch: box red", colour(box) == (1.0, 0.0, 0.0))
            check("panel switch: length 10", abs(box.Length.Value - 10) < 1e-9)
            nowVisible = [
                tree.topLevelItem(i).text(scol)
                for i in range(tree.topLevelItemCount())
                if not tree.topLevelItem(i).isHidden()
            ]
            check("panel switch: rows refiltered", nowVisible != visible)
            # Deleting the other branch (sec 16.7): the panel reloads.
            removed = doc.deleteTransactionBranch("side")
            settle()
            items = [combo.itemData(i) for i in range(combo.count())]
            check("delete: %d rows removed" % removed, removed > 0)
            check("delete: panel lists %r" % (items,), items == ["main"])
            sideRows = [
                tree.topLevelItem(i)
                for i in range(tree.topLevelItemCount())
                if tree.topLevelItem(i).text(bcol) == "side"
            ]
            check("delete: no side rows left (%d)" % len(sideRows), not sideRows)
            check("delete: box still red, short", colour(box) == (1.0, 0.0, 0.0)
                  and abs(box.Length.Value - 10) < 1e-9)
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        p.SetInt("TransactionLog", mode)
        App.saveParameter()
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
