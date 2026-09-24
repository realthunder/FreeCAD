# Audit of docs/TransactionLog.md sec 24.10: which GUI workflows write view
# provider properties outside any command. Run by the GUI at startup (sec
# 24.10 says how); lists every log row with the step that produced it, view
# ops first, to $AUDIT_OUT, and exits.
import os, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["AUDIT_OUT"]
lines = []


def pump():
    for _ in range(3):
        QtWidgets.QApplication.processEvents()


def step(label, fn):
    before = doc.getTransactionLog()
    n = before[-1]["seq"] if before else 0
    try:
        fn()
    except Exception as e:
        lines.append("STEP %s raised %s" % (label, e))
    pump()
    for r in doc.getTransactionLog(n + 1):
        ops = doc.getTransactionOps(r["seq"])
        view = [o for o in ops if o["ckind"] == "view"]
        other = [o for o in ops if o["ckind"] != "view"]
        props = sorted({"%s.%s" % (o["cid"], o["prop"]) for o in view}) + sorted(
            {"D%s.%s" % (o["cid"], o["prop"]) for o in other if o["op"] == "set"}
        )
        tag = "VIEW-ONLY" if view and not other else ("mixed" if view else "data")
        lines.append(
            "%-28s seq %3d %-9s %-9s %-10s %-34s %s"
            % (
                label,
                r["seq"],
                r["kind"],
                tag,
                r["origin"] or "-",
                r["name"][:34],
                " ".join(props)[:200],
            )
        )


def run():
    global doc
    try:
        p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
        p.SetInt("TransactionLog", 1)
        doc = App.newDocument("Audit")
        pump()
        gdoc = Gui.ActiveDocument
        step("Part_Box", lambda: Gui.runCommand("Part_Box"))
        step("recompute", lambda: doc.recompute())
        box = doc.getObject("Box")

        def sel(o):
            Gui.Selection.clearSelection()
            Gui.Selection.addSelection(o)

        step("select", lambda: sel(box))
        step("ToggleVisibility", lambda: Gui.runCommand("Std_ToggleVisibility"))
        step("ToggleVisibility2", lambda: Gui.runCommand("Std_ToggleVisibility"))
        step(
            "ShapeColor via property",
            lambda: setattr(box.ViewObject, "ShapeColor", (1.0, 0.0, 0.0)),
        )
        step("Visibility via property", lambda: setattr(box.ViewObject, "Visibility", False))
        step("Visibility back", lambda: setattr(box.ViewObject, "Visibility", True))
        step("RandomColor", lambda: Gui.runCommand("Std_RandomColor"))
        step("ViewFit", lambda: Gui.runCommand("Std_ViewFitAll"))
        step("ViewIso", lambda: Gui.runCommand("Std_ViewIsometric"))

        def body():
            b = doc.addObject("PartDesign::Body", "Body")
            sk = b.newObject("Sketcher::SketchObject", "Sketch")
            import Part

            sk.addGeometry(Part.LineSegment(App.Vector(0, 0, 0), App.Vector(10, 0, 0)))
            sk.addGeometry(Part.LineSegment(App.Vector(10, 0, 0), App.Vector(10, 10, 0)))
            sk.addGeometry(Part.LineSegment(App.Vector(10, 10, 0), App.Vector(0, 0, 0)))
            pad = b.newObject("PartDesign::Pad", "Pad")
            pad.Profile = sk
            pad.Length = 5
            doc.recompute()

        step("PD body/sketch/pad (python)", body)
        pad = doc.getObject("Pad")
        sk = doc.getObject("Sketch")
        step("setEdit pad", lambda: gdoc.setEdit(pad))
        step("resetEdit pad", lambda: gdoc.resetEdit())
        step("select pad", lambda: sel(pad))
        step("Std_Edit pad", lambda: Gui.runCommand("Std_Edit"))
        step("Control.closeDialog", lambda: Gui.Control.closeDialog())
        step("resetEdit", lambda: gdoc.resetEdit())
        step("setEdit sketch", lambda: gdoc.setEdit(sk))
        step("resetEdit sketch", lambda: gdoc.resetEdit())

        def dbl(o):
            # What the tree's double click does: an application transaction
            # that persists while the edit lasts (Tree.cpp, AutoTransaction).
            App.setActiveTransaction("Edit " + o.Label, True)
            gdoc.setEdit(o)

        step("dblclick-edit sketch", lambda: dbl(sk))
        step("close sketch edit", lambda: (gdoc.resetEdit(), App.closeActiveTransaction()))
        step("dblclick-edit pad", lambda: dbl(pad))
        step("close pad edit", lambda: (gdoc.resetEdit(), App.closeActiveTransaction()))
        step("undo", lambda: doc.undo())
        step("redo", lambda: doc.redo())
        step(
            "hide all (Std_HideSelection)", lambda: (sel(pad), Gui.runCommand("Std_HideSelection"))
        )
        step("show (Std_ShowSelection)", lambda: Gui.runCommand("Std_ShowSelection"))
        step("DrawStyle", lambda: Gui.runCommand("Std_DrawStyle", 1))
        step("Std_ToggleSelectability", lambda: Gui.runCommand("Std_ToggleSelectability"))
        step("tree select only", lambda: sel(box))
        step("save", lambda: doc.saveAs(os.path.join(os.path.dirname(OUT), "audit.FCStd")))
        lines.append("undo names: %s" % doc.UndoNames[:12])
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        with open(OUT, "w") as f:
            f.write("\n".join(lines) + "\n")
        os._exit(0)


QtCore.QTimer.singleShot(500, run)
