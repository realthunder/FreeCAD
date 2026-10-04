# GUI check of docs/TransactionLog.md sec 30.8: what the tree view and the
# 3D view do on their own -- rank the objects they show, draw a shape read
# from the file on first use -- is not an edit: no row in the log, no undo
# step, no redo lost. One GUI run in a fresh user home, given this script at
# startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-tc \
#     TREECHECK_OUT=/tmp/tc/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-tree-check.py
#
# It writes PASS/FAIL lines to $TREECHECK_OUT and exits. The log setting is
# put back before it exits.
import os, time, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["TREECHECK_OUT"]
lines = []


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def settle(n=20):
    # The tree works from a timer: give it time, not only events.
    for _ in range(n):
        QtWidgets.QApplication.processEvents()
        time.sleep(0.05)


def implicit(doc):
    return [t["seq"] for t in doc.getTransactionLog() if t["kind"] == "implicit"]


def ranks(doc):
    return dict((o.Name, o.TreeRank) for o in doc.Objects)


def run():
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 2)
    try:
        p.SetInt("TransactionLog", 1)
        mw = Gui.getMainWindow()
        trees = [w for w in mw.findChildren(QtWidgets.QTreeWidget)
                 if w.metaObject().className() == "Gui::TreeWidget"]
        for t in trees:
            w = t
            while w is not None and w is not mw:
                w.show()
                w = w.parentWidget()
        settle()
        check("the tree is shown", bool(trees) and all(t.isVisible() for t in trees))

        # Several objects made by one command, before the tree's timer runs.
        doc = App.newDocument("Tree")
        doc.UndoMode = 1
        settle()
        doc.openTransaction("three")
        for name in ("A", "B", "C"):
            doc.addObject("Part::Box", name)
        doc.recompute()
        doc.commitTransaction()
        made = ranks(doc)
        settle()
        check("three objects: the tree leaves their ranks %r" % (ranks(doc),),
              ranks(doc) == made and sorted(made.values()) == [1, 2, 3])
        check("three objects: one step %r" % (doc.UndoNames,), doc.UndoNames == ["three"])
        check("three objects: no row of the tree's %r" % (implicit(doc),), not implicit(doc))

        # An object back by undo is new to the tree and old to the document.
        doc.openTransaction("delete A")
        doc.removeObject("A")
        doc.commitTransaction()
        settle()
        doc.undo()
        settle()
        check("undo of a delete: its rank is the one it had %r" % (ranks(doc),),
              ranks(doc) == made)
        check("undo of a delete: the redo is still there %r" % (doc.RedoNames,),
              doc.RedoNames == ["delete A"])
        check("undo of a delete: no row of the tree's %r" % (implicit(doc),), not implicit(doc))

        # An object moved out to the root goes last, as part of the command
        # that moved it.
        doc.openTransaction("group")
        group = doc.addObject("App::DocumentObjectGroup", "G")
        group.addObject(doc.getObject("B"))
        doc.commitTransaction()
        settle()
        before = ranks(doc)
        doc.openTransaction("ungroup")
        group.removeObject(doc.getObject("B"))
        doc.commitTransaction()
        settle()
        after = ranks(doc)
        check("moved out to the root: it goes last %r" % (after,),
              after["B"] > max(v for k, v in after.items() if k != "B"))
        check("moved out to the root: one step, the command's %r" % (doc.UndoNames,),
              doc.UndoNames[:2] == ["ungroup", "group"] and not implicit(doc))
        row = [t for t in doc.getTransactionLog() if t["name"] == "ungroup"][-1]
        ops = [o for o in doc.getTransactionOps(row["seq"]) if o.get("prop") == "TreeRank"]
        check("moved out to the root: its rank is in the command's row", len(ops) == 1)
        doc.undo()
        settle()
        check("undo of it: back in the group with the rank it had %r" % (ranks(doc),),
              ranks(doc) == before and doc.getObject("B") in group.Group)
        check("undo of it: the redo is there, no step of the tree's %r" % (doc.RedoNames,),
              doc.RedoNames == ["ungroup"] and not implicit(doc))
        # Out again by an undo, with no command to put a rank in: it stays
        # where its rank puts it.
        doc.undo()
        settle()
        check("out by an undo: where its rank puts it, no step %r %r" % (ranks(doc), doc.RedoNames),
              ranks(doc)["B"] == made["B"] and sorted(doc.RedoNames) == ["group", "ungroup"]
              and not implicit(doc))
        doc.redo()
        doc.redo()
        settle()
        check("redone: last again %r" % (ranks(doc),), ranks(doc) == after and not implicit(doc))
        made = ranks(doc)

        # A file opened: the tree shows it and the view draws it.
        path = os.path.join(os.path.dirname(OUT), "tree.FCStd")
        doc.saveAs(path)
        settle()
        App.closeDocument(doc.Name)
        settle()
        doc = App.openDocument(path)
        settle(40)
        check("opened: the ranks saved %r" % (ranks(doc),), ranks(doc) == made)
        check("opened: no step %r" % (doc.UndoNames,), doc.UndoNames == [])
        check("opened: no row but the file as found %r"
              % ([t["kind"] for t in doc.getTransactionLog()],),
              not implicit(doc))
        check("opened: the shapes are there",
              all(abs(o.Shape.Volume - 1000.0) < 1e-6 for o in doc.Objects
                  if hasattr(o, "Shape")))
        settle()
        check("opened: reading them is no step either %r" % (doc.UndoNames,),
              doc.UndoNames == [] and not implicit(doc))
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        p.SetInt("TransactionLog", mode)
        App.saveParameter()
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
