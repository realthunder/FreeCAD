# GUI check of docs/TransactionLog.md sec 25: a session killed mid-work is
# recovered from its transaction log through the recovery dialog. Two runs of
# the GUI in the same fresh user home, each given this script at startup:
#
#   RECOVERCHECK_PHASE=crash    a saved box, then edits after the save -- a
#                               length, a colour, a new cylinder -- and the
#                               process kills itself (SIGKILL, nothing closed)
#   RECOVERCHECK_PHASE=recover  the start-up recovery dialog finds the log;
#                               the script starts the recovery and checks
#                               what came back
#
# Each run writes PASS/FAIL lines to $RECOVERCHECK_OUT (the crash run its
# notes to $RECOVERCHECK_OUT.crash) and exits. Section 25.8 says how to run it.
import os, signal, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["RECOVERCHECK_OUT"]
PHASE = os.environ.get("RECOVERCHECK_PHASE", "crash")
lines = []


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def write(path):
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def cameraPosition(view):
    for line in view.getCamera().splitlines():
        if line.strip().startswith("position"):
            return " ".join("%.3f" % float(v) for v in line.split()[1:4])
    return None


def crash():
    try:
        p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
        p.SetInt("TransactionLog", 1)
        App.saveParameter()
        doc = App.newDocument("Crashed")
        doc.openTransaction("box")
        box = doc.addObject("Part::Box", "Box")
        doc.commitTransaction()
        doc.recompute()
        # The camera is in GuiDocument.xml, so in every version, and not in
        # the log: recovered as the anchor had it.
        view = Gui.getDocument(doc.Name).ActiveView
        view.setCamera(
            "#Inventor V2.1 ascii\nOrthographicCamera {\n viewportMapping ADJUST_CAMERA\n"
            " position 11 -22 33\n orientation 0.267 0.535 0.802 0.646\n"
            " nearDistance 1\n farDistance 100\n aspectRatio 1\n focalDistance 40\n"
            " height 50\n}\n"
        )
        lines.append("camera " + cameraPosition(view))
        path = os.path.join(os.path.dirname(OUT), "recovercheck.FCStd")
        doc.saveAs(path)
        view.viewIsometric()
        doc.openTransaction("length")
        box.Length = 25
        doc.commitTransaction()
        doc.recompute()
        doc.openTransaction("colour")
        box.ViewObject.ShapeColor = (1.0, 0.0, 0.0)
        doc.commitTransaction()
        doc.openTransaction("cylinder")
        cyl = doc.addObject("Part::Cylinder", "Cylinder")
        cyl.Radius = 3
        doc.commitTransaction()
        doc.recompute()
        doc.getTransactionLog(0, 1)  # waits for the log's worker
        lines.append("dir " + doc.TransientDir)
        lines.append("undo " + repr(doc.UndoNames))
        lines.append("volume %.6f %.6f" % (box.Shape.Volume, cyl.Shape.Volume))
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    write(OUT + ".crash")
    os.kill(os.getpid(), signal.SIGKILL)


def recover():
    try:
        notes = {}
        with open(OUT + ".crash") as f:
            for line in f:
                key, _, value = line.strip().partition(" ")
                notes[key] = value
        dialog = None
        for w in QtWidgets.QApplication.topLevelWidgets():
            if w.metaObject().className() == "Gui::Dialog::DocumentRecovery" and w.isVisible():
                dialog = w
        check("the recovery dialog is up", dialog is not None)
        if dialog is None:
            return
        tree = dialog.findChild(QtWidgets.QTreeWidget)
        check(
            "it lists the crashed document: %s" % (tree.topLevelItem(0).text(0) if tree else None),
            tree is not None and tree.topLevelItemCount() == 1,
        )
        dialog.accept()  # Start Recovery
        check(
            "recovered: %s" % (tree.topLevelItem(0).text(1) if tree else None),
            tree is not None and tree.topLevelItem(0).text(1) == "Successfully recovered",
        )
        dialog.accept()  # Finish
        docs = [d for d in App.listDocuments().values() if d.getObject("Box")]
        check("one document with the box: %s" % list(App.listDocuments()), len(docs) == 1)
        doc = docs[0]
        box = doc.getObject("Box")
        cyl = doc.getObject("Cylinder")
        check("the length after the save: %s" % box.Length.Value, abs(box.Length.Value - 25) < 1e-9)
        check("the cylinder made after the save", cyl is not None)
        volumes = "%.6f %.6f" % (box.Shape.Volume, cyl.Shape.Volume if cyl else -1)
        check("the shapes as they were: %s" % volumes, volumes == notes.get("volume"))
        check(
            "the colour, a view op: %s" % (tuple(box.ViewObject.ShapeColor[:3]),),
            tuple(box.ViewObject.ShapeColor[:3]) == (1.0, 0.0, 0.0),
        )
        check("the cylinder has its view", cyl is not None and cyl.ViewObject is not None)
        check("marked modified", Gui.getDocument(doc.Name).Modified)
        views = Gui.getDocument(doc.Name).mdiViewsOfType("Gui::View3DInventor")
        camera = cameraPosition(views[0]) if views else None
        check(
            "the camera of the anchor (%s; saved %s)" % (camera, notes.get("camera")),
            camera == notes.get("camera"),
        )
        check(
            "the undo steps carry on: %s" % doc.UndoNames,
            repr(doc.UndoNames) == notes.get("undo"),
        )
        check("the old directory is gone", not os.path.exists(notes.get("dir", "")))
        doc.undo()  # the recompute after the cylinder
        doc.undo()  # the cylinder
        check(
            "undo across the crash takes the cylinder away: %s" % doc.UndoNames[:1],
            doc.getObject("Cylinder") is None,
        )
        doc.redo()
        check("and redo brings it back", doc.getObject("Cylinder") is not None)
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        write(OUT)
        os._exit(0)


QtCore.QTimer.singleShot(3000 if PHASE == "recover" else 0, crash if PHASE == "crash" else recover)
