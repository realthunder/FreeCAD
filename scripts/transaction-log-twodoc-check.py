# GUI check of docs/TransactionLog.md sec 30.3 S.a: a branch opened in a
# second document of the file, in the running application -- neither follows
# the other, a merge that is asked for takes the other's rows as they are,
# model and view providers both; and the log panel's "Branch to document"
# button and status. One GUI run in a fresh user home, given this script at
# startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-td \
#     TWODOCCHECK_OUT=/tmp/td/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-twodoc-check.py
#
# It writes PASS/FAIL lines to $TWODOCCHECK_OUT and exits. The log setting is
# put back before it exits.
import os, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["TWODOCCHECK_OUT"]
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
        return None
    parent = dock.parentWidget()
    if isinstance(parent, QtWidgets.QDockWidget):
        parent.show()
    dock.show()
    settle()
    return dock


def status(dock):
    for label in dock.findChildren(QtWidgets.QLabel):
        if " transactions, " in label.text():
            return label.text()
    return ""


def button(dock, text):
    for b in dock.findChildren(QtWidgets.QPushButton):
        if b.text().replace("&", "") == text:
            return b
    return None


def run():
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 2)
    try:
        p.SetInt("TransactionLog", 1)
        doc = App.newDocument("Branches")
        doc.openTransaction("box")
        box = doc.addObject("Part::Box", "Box")
        doc.recompute()
        doc.commitTransaction()
        doc.openTransaction("red")
        box.ViewObject.ShapeColor = (1.0, 0.0, 0.0)
        doc.commitTransaction()
        settle()

        other = doc.openTransactionBranch("desk")
        settle()
        check("branch: a document of its own, shown",
              other.Name != doc.Name and Gui.getDocument(other.Name) is not None)
        obox = other.getObject("Box")
        check("branch: starts as main is %r" % (colour(obox),),
              abs(obox.Length.Value - 10) < 1e-9 and colour(obox) == (1.0, 0.0, 0.0))

        # What the branch does stays on it until main asks.
        other.openTransaction("longer")
        obox.Length = 30
        other.recompute()
        other.commitTransaction()
        other.openTransaction("blue")
        obox.ViewObject.ShapeColor = (0.0, 0.0, 1.0)
        other.commitTransaction()
        other.openTransaction("cylinder")
        ocyl = other.addObject("Part::Cylinder", "Cylinder")
        other.recompute()
        other.commitTransaction()
        other.openTransaction("green")
        ocyl.ViewObject.ShapeColor = (0.0, 1.0, 0.0)
        other.commitTransaction()
        settle()
        box = doc.getObject("Box")
        check("main does not follow: length %g, %r" % (box.Length.Value, colour(box)),
              abs(box.Length.Value - 10) < 1e-9 and colour(box) == (1.0, 0.0, 0.0)
              and doc.getObject("Cylinder") is None)
        state = other.getTransactionBranchState()
        check("branch: ahead of main %r" % (state,),
              state["target"] == "main" and state["ahead"] >= 4 and state["behind"] == 0)

        # Main asks: it has not moved, so the rows are taken as they are.
        preview = doc.previewTransactionMerge("desk")
        check("preview: a fast-forward of %d rows" % preview["forward"],
              preview["forward"] > 0 and preview["conflicts"] == 0)
        merged = doc.mergeTransactionBranch("desk")
        settle()
        box = doc.getObject("Box")
        check("merge: %d rows taken, no merge row" % merged["forwarded"],
              merged["forwarded"] == preview["forward"]
              and not [t for t in doc.getTransactionLog() if t["kind"] == "merge"])
        check("main has it: length 30, volume %g" % box.Shape.Volume,
              abs(box.Length.Value - 30) < 1e-9 and abs(box.Shape.Volume - 3000) < 1e-6)
        check("main has it: blue %r" % (colour(box),), colour(box) == (0.0, 0.0, 1.0))
        cyl = doc.getObject("Cylinder")
        check("main has it: the cylinder, shown, green",
              cyl is not None and cyl.ViewObject is not None and colour(cyl) == (0.0, 1.0, 0.0))
        check("main has it: its shape", cyl is not None
              and abs(cyl.Shape.Volume - ocyl.Shape.Volume) < 1e-6)
        rows = dict((t["name"], t) for t in doc.getTransactionLog())
        check("the rows are main's, as they were made",
              all(rows[n]["branch"] == "main" and rows[n]["kind"] == "user"
                  for n in ("longer", "blue", "cylinder", "green")))
        check("main's steps are the rows %r" % (doc.UndoNames[:4],),
              doc.UndoNames[:4] == ["green", "cylinder", "blue", "longer"])
        state = other.getTransactionBranchState()
        check("branch: level %r" % (state,), state["ahead"] == 0 and state["behind"] == 0)

        # Main's own operation: the branch has it when it asks.
        doc.openTransaction("lower")
        box.Height = 5
        doc.recompute()
        doc.commitTransaction()
        settle()
        check("branch does not follow: height %g" % obox.Height.Value,
              abs(obox.Height.Value - 10) < 1e-9)
        pulled = other.mergeTransactionBranch("main")
        settle()
        obox = other.getObject("Box")
        check("branch asks: height 5, volume %g" % obox.Shape.Volume,
              pulled["forwarded"] > 0 and abs(obox.Height.Value - 5) < 1e-9
              and abs(obox.Shape.Volume - 30 * 10 * 5) < 1e-6)

        # Undo in the document that took the rows walks them back.
        doc.undo()
        settle()
        check("main undo: height 10, the branch's stays",
              abs(box.Height.Value - 10) < 1e-9 and abs(obox.Height.Value - 5) < 1e-9)
        doc.undo()
        doc.undo()
        settle()
        check("main undo: the cylinder gone here, not there",
              doc.getObject("Cylinder") is None and other.getObject("Cylinder") is not None)

        dock = panel()
        check("panel found", dock is not None)
        if dock is not None:
            Gui.setActiveDocument(other.Name)
            App.setActiveDocument(other.Name)
            settle()
            text = status(dock)
            check("panel: the branch says what it was made from (%s)" % text[:90],
                  "from main" in text and "branch desk" in text)
            Gui.setActiveDocument(doc.Name)
            App.setActiveDocument(doc.Name)
            settle()
            text = status(dock)
            check("panel: main is from nothing (%s)" % text[:90],
                  "from " not in text and "branch main" in text)
            b = button(dock, "Branch to document")
            check("panel: Branch to document offered", b is not None and b.isEnabled())
            before = set(App.listDocuments())
            if b is not None:
                b.click()
                settle()
            made = set(App.listDocuments()) - before
            check("panel: it opens one (%r)" % (sorted(made),), len(made) == 1)
            names = [x["name"] for x in doc.getTransactionBranches()]
            check("panel: its branch %r" % (names,), "main~1" in names)
            for name in made:
                App.closeDocument(name)
            settle()
            names = [x["name"] for x in doc.getTransactionBranches()]
            check("closed: its branch stays %r" % (names,), "main~1" in names)
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        p.SetInt("TransactionLog", mode)
        App.saveParameter()
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
