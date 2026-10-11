# GUI check of docs/TransactionLog.md sec 27.7: a version of a file opened
# as a document of its own in the running application -- shown beside the
# document it came from, on the file's one log, read-only as a partial
# document is when saving, and made a branch by its first change. One GUI
# run in a fresh user home and cache, given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   XDG_CACHE_HOME=/tmp/vc/cache QT_QPA_PLATFORM=offscreen \
#     FREECAD_USER_HOME=/tmp/fchome-vc VERSIONCHECK_OUT=/tmp/vc/out.txt \
#     ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-version-check.py
#
# It writes PASS/FAIL lines to $VERSIONCHECK_OUT and exits.
import os, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["VERSIONCHECK_OUT"]
lines = []


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def settle():
    for _ in range(5):
        QtWidgets.QApplication.processEvents()


def colour(obj):
    return tuple(round(c, 3) for c in obj.ViewObject.ShapeColor[:3])


def run():
    try:
        doc = App.newDocument("Versions")
        doc.openTransaction("box")
        box = doc.addObject("Part::Box", "Box")
        doc.commitTransaction()
        doc.recompute()
        path = os.path.join(os.path.dirname(OUT), "versions.FCStd")
        doc.saveAs(path)
        first = doc.getTransactionVersions()[-1]["num"]
        doc.openTransaction("long red")
        box.Length = 30
        box.ViewObject.ShapeColor = (1.0, 0.0, 0.0)
        doc.commitTransaction()
        doc.recompute()
        settle()

        opened = doc.openTransactionVersion(first)
        settle()
        gdoc = Gui.getDocument(opened.Name)
        check("the version has a Gui document", gdoc is not None)
        # Named for the branch its first change will take (sec 27.23).
        check("named after the file and its branch: %s" % opened.FileName,
              opened.FileName == doc.FileName + "@main@v%d@v%d" % (first, first))
        check("and so labelled: %s" % opened.Label,
              opened.Label.endswith("@main@v%d@v%d" % (first, first)))
        vbox = opened.getObject("Box")
        check("the version's box is the saved one: %s" % (vbox and vbox.Length.Value),
              vbox is not None and abs(vbox.Length.Value - 10) < 1e-9)
        check("with its view as saved: %s" % (colour(vbox),), colour(vbox) != (1.0, 0.0, 0.0))
        check("and a shape to show", vbox is not None and not vbox.Shape.isNull())
        check("the document it came from is unchanged",
              abs(box.Length.Value - 30) < 1e-9 and colour(box) == (1.0, 0.0, 0.0))
        check("opening wrote nothing: %r" % (opened.UndoNames,), not opened.UndoNames)
        check("opened once", doc.openTransactionVersion(first) is opened)
        cursor = opened.getTransactionCursor()
        check("on no branch yet", cursor["detached"])

        # Edited in its own window: its first change makes it a branch, and
        # the document it came from does not move.
        opened.openTransaction("version edit")
        vbox.Height = 44
        vbox.ViewObject.ShapeColor = (0.0, 0.0, 1.0)
        opened.commitTransaction()
        opened.recompute()
        settle()
        cursor = opened.getTransactionCursor()
        names = {b["id"]: b["name"] for b in doc.getTransactionBranches()}
        check("its change made a branch: %s" % names.get(cursor["branch"]),
              not cursor["detached"] and names.get(cursor["branch"]) == "main@v%d" % first)
        check("the document it came from is still main",
              doc.getTransactionCursor()["branch"] == 1)
        check("its own box moved", abs(vbox.Height.Value - 44) < 1e-9)
        check("and not the other's", abs(box.Height.Value - 10) < 1e-9)
        check("its undo is its own: %r" % (opened.UndoNames,),
              "version edit" in opened.UndoNames and "version edit" not in doc.UndoNames)

        # Saving: refused, as a partial document's is; closing asks nothing.
        try:
            opened.save()
            check("save refused", False)
        except ValueError:
            check("save refused", True)
        name = opened.Name
        App.closeDocument(name)
        settle()
        check("closed", name not in App.listDocuments())
        # Its branch stays in the file's log.
        names = [b["name"] for b in doc.getTransactionBranches()]
        check("its branch stays in the log: %r" % (names,), "main@v%d" % first in names)
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
