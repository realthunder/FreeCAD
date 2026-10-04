# GUI check of docs/TransactionLog.md sec 29: writers of a file in the running
# application -- two documents of one file, each following what the other
# does, model and view providers both, each with its own undo; and the log
# panel's Writer button and status. One GUI run in a fresh user home, given
# this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-wc \
#     WRITERCHECK_OUT=/tmp/wc/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-writer-check.py
#
# It writes PASS/FAIL lines to $WRITERCHECK_OUT and exits. The log setting is
# put back before it exits.
import os, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["WRITERCHECK_OUT"]
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
        doc = App.newDocument("Writers")
        doc.openTransaction("box")
        box = doc.addObject("Part::Box", "Box")
        doc.recompute()
        doc.commitTransaction()
        doc.openTransaction("red")
        box.ViewObject.ShapeColor = (1.0, 0.0, 0.0)
        doc.commitTransaction()
        settle()

        writer = doc.openTransactionWriter("desk")
        settle()
        check("writer: a document of its own, shown",
              writer.Name != doc.Name and Gui.getDocument(writer.Name) is not None)
        wbox = writer.getObject("Box")
        check("writer: starts as the target is %r" % (colour(wbox),),
              abs(wbox.Length.Value - 10) < 1e-9 and colour(wbox) == (1.0, 0.0, 0.0))

        # The writer's operations: the target's document has them at once.
        writer.openTransaction("longer")
        wbox.Length = 30
        writer.recompute()
        writer.commitTransaction()
        writer.openTransaction("blue")
        wbox.ViewObject.ShapeColor = (0.0, 0.0, 1.0)
        writer.commitTransaction()
        settle()
        box = doc.getObject("Box")
        check("target follows: length 30, volume %g" % box.Shape.Volume,
              abs(box.Length.Value - 30) < 1e-9 and abs(box.Shape.Volume - 3000) < 1e-6)
        check("target follows: blue %r" % (colour(box),), colour(box) == (0.0, 0.0, 1.0))
        check("target: not its steps %r" % (doc.UndoNames,),
              "longer" not in doc.UndoNames and "blue" not in doc.UndoNames)

        writer.openTransaction("cylinder")
        wcyl = writer.addObject("Part::Cylinder", "Cylinder")
        writer.recompute()
        writer.commitTransaction()
        writer.openTransaction("green")
        wcyl.ViewObject.ShapeColor = (0.0, 1.0, 0.0)
        writer.commitTransaction()
        settle()
        cyl = doc.getObject("Cylinder")
        check("target follows: the cylinder, shown, green",
              cyl is not None and cyl.ViewObject is not None and colour(cyl) == (0.0, 1.0, 0.0))
        check("target follows: its shape", cyl is not None
              and abs(cyl.Shape.Volume - wcyl.Shape.Volume) < 1e-6)

        # The target's own operation: the writer has it.
        doc.openTransaction("lower")
        box.Height = 5
        doc.recompute()
        doc.commitTransaction()
        settle()
        check("writer follows: height 5, volume %g" % wbox.Shape.Volume,
              abs(wbox.Height.Value - 5) < 1e-9 and abs(wbox.Shape.Volume - 30 * 10 * 5) < 1e-6)
        state = writer.getTransactionWriter()
        check("writer: level %r" % (state,),
              state["writer"] and state["unpushed"] == 0 and not state["behind"])

        # Each undoes its own.
        check("writer: its own steps %r" % (writer.UndoNames,),
              writer.UndoNames[:4] == ["green", "cylinder", "blue", "longer"])
        writer.undo()
        writer.undo()
        settle()
        check("writer undo: the cylinder gone in both",
              writer.getObject("Cylinder") is None and doc.getObject("Cylinder") is None)
        check("writer undo: the target's height stays", abs(box.Height.Value - 5) < 1e-9)
        doc.undo()
        settle()
        check("target undo: height 10 in both",
              abs(box.Height.Value - 10) < 1e-9 and abs(wbox.Height.Value - 10) < 1e-9)

        dock = panel()
        check("panel found", dock is not None)
        if dock is not None:
            Gui.setActiveDocument(writer.Name)
            App.setActiveDocument(writer.Name)
            settle()
            text = status(dock)
            check("panel: the writer says what it writes to (%s)" % text[:80],
                  "writer of main" in text and "branch desk" in text)
            b = button(dock, "Writer")
            check("panel: no writer of a writer", b is not None and not b.isEnabled())
            Gui.setActiveDocument(doc.Name)
            App.setActiveDocument(doc.Name)
            settle()
            text = status(dock)
            check("panel: the target is no writer", "writer of" not in text and "branch main" in text)
            b = button(dock, "Writer")
            check("panel: Writer offered", b is not None and b.isEnabled())
            before = set(App.listDocuments())
            if b is not None:
                b.click()
                settle()
            made = set(App.listDocuments()) - before
            check("panel: Writer opens one (%r)" % (sorted(made),), len(made) == 1)
            names = [x["name"] for x in doc.getTransactionBranches()]
            check("panel: its branch %r" % (names,), "main~1" in names)
            for name in made:
                App.closeDocument(name)
            settle()
            names = [x["name"] for x in doc.getTransactionBranches()]
            check("closed level: its branch gone %r" % (names,), "main~1" not in names)
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        p.SetInt("TransactionLog", mode)
        App.saveParameter()
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
