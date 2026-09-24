# GUI check of docs/TransactionLog.md sec 24.9: view-provider state through
# cold undo and restore to a version. Run by the GUI at startup (sec 24.9 says
# how); writes one PASS/FAIL line per check to $VIEWCHECK_OUT and exits.
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
        v1 = doc.snapshotTransactionLog()
        check("snapshot is version 1 (%s)" % v1, v1 == 1)

        doc.openTransaction("colour")
        vp.ShapeColor = (1.0, 0.0, 0.0)
        doc.commitTransaction()
        red = tuple(vp.ShapeColor[:3])
        rows = doc.getTransactionLog()
        colour = [r for r in rows if r["name"] == "colour"]
        ops = doc.getTransactionOps(colour[-1]["seq"]) if colour else []
        check(
            "colour logged under the box's id: %s"
            % [(o.get("ckind"), o.get("cid"), o.get("prop")) for o in ops],
            any(
                o.get("ckind") == "view"
                and o.get("cid") == box.ID
                and o.get("prop") == "ShapeColor"
                for o in ops
            ),
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
        check("and the length: %s" % box.Length.Value, abs(box.Length.Value - 10) < 1e-9)
        for i in range(4):
            doc.redo()
        check("redo brings the colour back", tuple(vp.ShapeColor[:3]) == red)
        check("and the length: %s" % box.Length.Value, abs(box.Length.Value - 13) < 1e-9)

        ok = doc.restoreTransactionVersion(1)
        check("restore to version 1 ran", ok)
        check(
            "restore: colour of version 1: %s" % (tuple(vp.ShapeColor[:3]),),
            tuple(vp.ShapeColor[:3]) == grey,
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
