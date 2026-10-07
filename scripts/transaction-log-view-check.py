# GUI check of docs/TransactionLog.md sec 24.9: view-provider state through
# cold undo and restore to a version. Run by the GUI at startup (sec 24.9 says
# how); writes one PASS/FAIL line per check to $VIEWCHECK_OUT and exits.
#
# The view provider's state is a line width. A colour is the object's
# (docs/ShapeAppearanceDesign.md sec 14.6.5): written through the view
# provider's name it is the object's row and no row of the view's, and goes
# back and forth with the rest all the same.
import os, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore

OUT = os.environ["VIEWCHECK_OUT"]
lines = []


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def run():
    try:
        p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
        p.SetInt("TransactionLog", 1)
        p.SetBool("ViewObjectTransaction", True)
        p.SetInt("MaxUndoSize", 2)
        doc = App.newDocument("ViewCheck")
        doc.openTransaction("box")
        box = doc.addObject("Part::Box", "Box")
        doc.commitTransaction()
        doc.recompute()
        vp = box.ViewObject
        grey = tuple(vp.ShapeColor[:3])
        thin = vp.LineWidth
        v1 = doc.snapshotTransactionLog()
        check("snapshot is version 1 (%s)" % v1, v1 == 1)

        doc.openTransaction("colour")
        vp.ShapeColor = (1.0, 0.0, 0.0)
        vp.LineWidth = thin + 3
        doc.commitTransaction()
        red = tuple(vp.ShapeColor[:3])
        rows = doc.getTransactionLog()
        colour = [r for r in rows if r["name"] == "colour"]
        ops = doc.getTransactionOps(colour[-1]["seq"]) if colour else []
        logged = [(o.get("ckind"), o.get("cid"), o.get("prop")) for o in ops]
        check(
            "line width logged under the box's id: %s" % logged,
            ("view", box.ID, "LineWidth") in logged,
        )
        check(
            "colour logged as the object's, and no row of the view's: %s" % logged,
            ("obj", box.ID, "ElementAppearance") in logged
            and not any(kind == "view" and prop != "LineWidth" for kind, _, prop in logged),
        )
        for i in range(3):
            doc.openTransaction("length")
            box.Length = 11 + i
            doc.commitTransaction()
        for i in range(4):
            doc.undo()
        check(
            "cold undo restores the colour: %s" % (tuple(vp.ShapeColor[:3]),),
            tuple(vp.ShapeColor[:3]) == grey,
        )
        check(
            "and the line width: %s" % vp.LineWidth,
            abs(vp.LineWidth - thin) < 1e-9,
        )
        check("and the length: %s" % box.Length.Value, abs(box.Length.Value - 10) < 1e-9)
        for i in range(4):
            doc.redo()
        check("redo brings the colour back", tuple(vp.ShapeColor[:3]) == red)
        check("and the line width", abs(vp.LineWidth - thin - 3) < 1e-9)
        check("and the length: %s" % box.Length.Value, abs(box.Length.Value - 13) < 1e-9)

        ok = doc.restoreTransactionVersion(1)
        check("restore to version 1 ran", ok)
        check(
            "restore: colour of version 1: %s" % (tuple(vp.ShapeColor[:3]),),
            tuple(vp.ShapeColor[:3]) == grey,
        )
        check(
            "restore: line width of version 1: %s" % vp.LineWidth,
            abs(vp.LineWidth - thin) < 1e-9,
        )
        check(
            "restore: length of version 1: %s" % box.Length.Value, abs(box.Length.Value - 10) < 1e-9
        )
        check(
            "restore is an undo step: %s" % doc.UndoNames[:1],
            doc.UndoNames[:1] == ["Restore version 1"],
        )
        check(
            "same view provider object", box.ViewObject is not None and doc.getObject("Box") is box
        )
        doc.undo()
        check(
            "undo of the restore: colour back: %s" % (tuple(vp.ShapeColor[:3]),),
            tuple(vp.ShapeColor[:3]) == red,
        )
        check(
            "undo of the restore: line width back: %s" % vp.LineWidth,
            abs(vp.LineWidth - thin - 3) < 1e-9,
        )
        check(
            "undo of the restore: length back: %s" % box.Length.Value,
            abs(box.Length.Value - 13) < 1e-9,
        )
        check(
            "no scratch document left: %s" % list(App.listDocuments()),
            list(App.listDocuments()) == ["ViewCheck"],
        )
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        with open(OUT, "w") as f:
            f.write("\n".join(lines) + "\n")
        os._exit(0)


QtCore.QTimer.singleShot(0, run)
