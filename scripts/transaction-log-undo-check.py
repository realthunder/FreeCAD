# GUI check of docs/TransactionLog.md sec 33.4, 33.11 and 33.13: the user's
# own undo and redo. A change made with no name does not empty the redo list;
# a redo it stands in the way of is refused, and the refusal is said in a
# dialog with its reason and the step after it offered, where it only went to
# the report view. And several rows of the log panel undone at once are one
# step. One GUI run in a fresh user home, given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-ur \
#     UNDOCHECK_OUT=/tmp/ur/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-undo-check.py
#
# It writes PASS/FAIL lines to $UNDOCHECK_OUT and exits. The log setting is
# put back before it exits.
import os, time, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["UNDOCHECK_OUT"]
lines = []


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def settle(n=10):
    for _ in range(n):
        QtWidgets.QApplication.processEvents()
        time.sleep(0.05)


def refusals():
    return [
        w
        for w in QtWidgets.QApplication.topLevelWidgets()
        if isinstance(w, QtWidgets.QMessageBox) and w.objectName() == "undoRefused" and w.isVisible()
    ]


asked = [0]


def command(name, press=None):
    """Run a command; what the dialog it put up said, None for no dialog. The
    dialog is answered with the button whose text holds `press`."""
    seen = []
    asked[0] += 1
    mine = asked[0]

    def answer():
        for box in refusals():
            seen.append((box.text(), box.informativeText(), [b.text() for b in box.buttons()]))
            for button in box.buttons():
                if press and press in button.text():
                    button.click()
                    return
            box.reject()

    def unstick():
        # Whatever this command still has up, so a dialog nobody answered
        # ends the run with a failure and not with a hang.
        modal = QtWidgets.QApplication.activeModalWidget()
        if mine == asked[0] and modal is not None:
            seen.append(("left open", "", []))
            modal.reject()

    QtCore.QTimer.singleShot(300, answer)
    QtCore.QTimer.singleShot(4000, unstick)
    Gui.runCommand(name)
    settle()
    return seen[0] if seen else None


def panel():
    dock = None
    for w in Gui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "Gui::DockWnd::TransactionLogView":
            dock = w
    if dock is None:
        return None
    parent = dock.parentWidget()
    if isinstance(parent, QtWidgets.QDockWidget):
        parent.show()
    dock.show()
    settle()
    return dock


def severalRows():
    # Sec 33.13: what the panel's "Undo N rows" does with the rows selected.
    doc = App.newDocument("Rows")
    doc.UndoMode = 1
    box = doc.addObject("Part::Box", "Box")
    doc.recompute()
    settle()

    def step(name, prop, value):
        doc.openTransaction(name)
        setattr(box, prop, value)
        doc.commitTransaction()
        doc.recompute()

    step("longer", "Length", 20)
    step("wider", "Width", 20)
    step("longest", "Length", 30)
    step("higher", "Height", 20)
    settle()
    dock = panel()
    check("the log panel is there", dock is not None)
    if dock is None:
        return

    def seqs(*names):
        rows = {t["name"]: t["seq"] for t in doc.getTransactionLog() if t["kind"] == "user"}
        return ",".join(str(rows[n]) for n in names)

    told = []

    def undoRows(rows):
        QtCore.QMetaObject.invokeMethod(dock, "undoRows", QtCore.Qt.DirectConnection,
                                        QtCore.Q_ARG(str, rows))
        # What the panel says of it, read before anything else moves the log
        told[:] = [l.text() for l in dock.findChildren(QtWidgets.QLabel)]
        doc.recompute()
        settle()

    def said(text):
        return any(text in t for t in told)

    # Two rows on one property, another row between them on another one.
    before = len(doc.getTransactionLog())
    undoRows(seqs("longer", "longest"))
    check("two rows on one property undone together: %r %r"
          % (box.Length.Value, doc.UndoNames[:1]),
          box.Length.Value == 10 and box.Width.Value == 20 and box.Height.Value == 20
          and doc.UndoNames[:1] == ["Undo longer, longest"])
    undos = [t for t in doc.getTransactionLog()[before:] if t["kind"] == "undo"]
    check("as one row of the log, which names no one row it inverts: %r"
          % ([(t["name"], t["inverts"]) for t in undos],),
          len(undos) == 1 and undos[0]["inverts"] == 0)
    check("its shape with it: %.1f" % box.Shape.Volume,
          abs(box.Shape.Volume - 10 * 20 * 20) < 1e-6)
    answer = command("Std_Undo")
    check("and one step: a single undo brings both back: %r" % (box.Length.Value,),
          answer is None and box.Length.Value == 30 and box.Width.Value == 20)

    # A row that is not selected, in the way of the older one selected.
    names = list(doc.UndoNames)
    before = len(doc.getTransactionLog())
    undoRows(seqs("longer", "higher"))
    check("a row not selected in the way: none is undone: %r %r"
          % (box.Length.Value, box.Height.Value),
          box.Length.Value == 30 and box.Height.Value == 20 and list(doc.UndoNames) == names
          and len(doc.getTransactionLog()) == before)
    check("and the panel says so", said("Undo of 2 rows refused"))


def run():
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 2)
    try:
        p.SetInt("TransactionLog", 1)
        doc = App.newDocument("Undo")
        doc.UndoMode = 1
        box = doc.addObject("Part::Box", "Box")
        doc.recompute()
        settle()

        def step(name, prop, value):
            doc.openTransaction(name)
            setattr(box, prop, value)
            doc.commitTransaction()
            doc.recompute()

        step("longer", "Length", 20)
        step("wider", "Width", 20)
        check("the list has the two steps: %r" % (doc.UndoNames,), doc.UndoNames == ["wider", "longer"])

        said = command("Std_Undo")
        check("an undo with its recompute, no dialog: %r" % (said,),
              said is None and box.Width.Value == 10 and abs(box.Shape.Volume - 2000) < 1e-6)
        said = command("Std_Undo")
        check("and the one before: %r" % (doc.RedoNames,),
              said is None and box.Length.Value == 10 and doc.RedoNames == ["longer", "wider"])

        # A change with no name, on what the first redo sets.
        box.Length = 15
        doc.recompute()
        settle()
        check("a change with no name leaves the redo list: %r" % (doc.RedoNames,),
              doc.RedoNames == ["longer", "wider"] and doc.UndoNames == [])

        said = command("Std_Redo", "Cancel")
        check("the redo it stands in the way of is said: %r" % (said,),
              said is not None and "longer" in said[0] and "redone" in said[0])
        check("with its reason, the object by its label: %r" % (said and said[1],),
              said is not None and "Length of Box was changed since" in said[1])
        check("and the step after it offered: %r" % (said and said[2],),
              said is not None and "'wider'" in said[1]
              and any("Skip" in b for b in said[2]) and any("Cancel" in b for b in said[2]))
        check("cancelled, nothing moved: %r" % (doc.RedoNames,),
              doc.RedoNames == ["longer", "wider"] and box.Width.Value == 10
              and box.Length.Value == 15)

        said = command("Std_Redo", "Skip")
        check("gone past: the step after is redone: %r %r" % (doc.UndoNames, doc.RedoNames),
              said is not None and box.Width.Value == 20 and box.Length.Value == 15
              and doc.UndoNames == ["wider"] and doc.RedoNames == ["longer"])
        check("its shape with it: %.1f" % box.Shape.Volume,
              abs(box.Shape.Volume - 15 * 20 * 10) < 1e-6 and "Touched" not in box.State)

        said = command("Std_Redo", "OK")
        check("the refused one alone: said, nothing offered: %r" % (said,),
              said is not None and "longer" in said[0]
              and not any("Skip" in b for b in said[2]) and doc.RedoNames == ["longer"])

        said = command("Std_Undo")
        check("the step redone is undone as any: %r" % (doc.UndoNames,),
              said is None and box.Width.Value == 10 and doc.UndoNames == [])
        severalRows()
        check("no dialog left", not refusals())
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        p.SetInt("TransactionLog", mode)
        App.saveParameter()
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
