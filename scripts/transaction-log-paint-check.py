# GUI check of docs/TransactionLog.md sec 31.16, 31.18: the faces of a shape
# painted by name -- the names on the object (ColoredElements), their
# colours on its view provider (MappedColors) -- put back whole by what
# puts a value back: an undo, a branch switched to, a merge that takes
# them. One GUI run in a fresh user home, given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-pt \
#     PAINTCHECK_OUT=/tmp/pt/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-paint-check.py
#
# It writes PASS/FAIL lines to $PAINTCHECK_OUT and exits. The log setting
# is put back before it exits.
import os, tempfile, traceback
import FreeCAD as App
import FreeCADGui as Gui
from FreeCAD import Vector as V
from PySide import QtCore

OUT = os.environ["PAINTCHECK_OUT"]
lines = []
RED, GREEN, BLUE = (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def rgb(c):
    return tuple(round(x, 1) for x in c[:3])


def named(obj):
    # The painted elements by name, each with its colour.
    subs = obj.ColoredElements[1] if obj.ColoredElements else []
    colors = [rgb(c) for c in obj.ViewObject.MappedColors]
    if len(subs) != len(colors):
        return {"subs": list(subs), "colors": colors}
    return dict(zip(subs, colors))


def shown(obj):
    # The faces drawn in another colour than the object's, by where they lie.
    vp = obj.ViewObject
    out = {}
    if len(vp.DiffuseColor) > 1:
        for i, c in enumerate(vp.DiffuseColor):
            if rgb(c) != rgb(vp.ShapeColor):
                b = obj.Shape.Faces[i].BoundBox
                out[tuple(round(v, 3) for v in (b.XMin, b.XMax, b.ZMin, b.ZMax))] = rgb(c)
    return out


def face(obj, **at):
    for i, f in enumerate(obj.Shape.Faces):
        if all(abs(getattr(f.BoundBox, k) - v) < 1e-6 for k, v in at.items()):
            return "Face%d" % (i + 1)
    return None


def paint(doc, name, obj, colors):
    doc.openTransaction(name)
    vp = doc.getObject(obj).ViewObject
    now = {k: v for k, v in vp.getElementColors().items() if k not in ("Face", "Edge", "Vertex")}
    now.update(colors)
    vp.setElementColors(now)
    doc.recompute()
    doc.commitTransaction()


def cut(name, folder):
    # A box a cylinder is cut from, the cylinder below it: six faces.
    doc = App.newDocument(name)
    doc.UndoMode = 1
    doc.openTransaction("base")
    box = doc.addObject("Part::Box", "Box")
    cyl = doc.addObject("Part::Cylinder", "Cyl")
    cyl.Radius = 2
    cyl.Height = 30
    cyl.Placement.Base = V(5, 5, -40)
    c = doc.addObject("Part::Cut", "Cut")
    c.Base = box
    c.Tool = cyl
    doc.recompute()
    doc.commitTransaction()
    doc.saveAs(os.path.join(folder, name + ".FCStd"))
    return doc


def run():
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 2)
    try:
        p.SetInt("TransactionLog", 2)
        folder = tempfile.mkdtemp(prefix="fc-paint-")

        # Undo and redo: each step's names and colours, and no others.
        doc = App.newDocument("PaintUndo")
        doc.UndoMode = 1
        doc.openTransaction("base")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        doc.commitTransaction()
        doc.saveAs(os.path.join(folder, "PaintUndo.FCStd"))
        top, bottom = face(doc.Box, ZMin=10), face(doc.Box, ZMax=0)
        front = face(doc.Box, XMax=0)
        paint(doc, "red top", "Box", {top: RED})
        paint(doc, "green bottom", "Box", {bottom: GREEN})
        both = {top: RED, bottom: GREEN}
        check("two faces painted, by name (%r)" % named(doc.Box), named(doc.Box) == both)
        doc.undo()
        check("undone: the first only (%r)" % named(doc.Box), named(doc.Box) == {top: RED})
        doc.undo()
        check("undone again: none (%r)" % named(doc.Box), named(doc.Box) == {})
        doc.redo()
        doc.redo()
        check("redone: both (%r)" % named(doc.Box), named(doc.Box) == both)

        # A branch that paints one more, left and come back to. The list
        # put back is longer than the one there: it was cut to that one's
        # length, the third name gone for good.
        doc.createTransactionBranch("side")
        paint(doc, "side blue front", "Box", {front: BLUE})
        three = dict(both)
        three[front] = BLUE
        check("the branch has three (%r)" % named(doc.Box), named(doc.Box) == three)
        there = shown(doc.Box)
        doc.switchTransactionBranch("main")
        check("main has its two (%r)" % named(doc.Box), named(doc.Box) == both)
        check("and draws two (%r)" % shown(doc.Box), len(shown(doc.Box)) == 2)
        doc.switchTransactionBranch("side")
        check("back on the branch: three by name (%r)" % named(doc.Box), named(doc.Box) == three)
        check("and drawn where they were", shown(doc.Box) == there)
        App.closeDocument(doc.Name)

        # A merge that takes a painted face: theirs paints, ours changes a
        # radius. Nothing is asked, and the colour came without its name --
        # and then not at all.
        doc = cut("PaintTake", folder)
        doc.createTransactionBranch("side")
        side = face(doc.Cut, XMin=10)
        paint(doc, "side red", "Cut", {side: RED})
        there = shown(doc.Cut)
        check("theirs paints one face of the cut (%r)" % there, len(there) == 1)
        doc.switchTransactionBranch("main")
        check("main has none", named(doc.Cut) == {} and shown(doc.Cut) == {})
        doc.openTransaction("main radius")
        doc.Cyl.Radius = 3
        doc.recompute()
        doc.commitTransaction()
        pv = doc.previewTransactionMerge("side")
        check("the merge asks nothing (%r)" % [c["key"] for c in pv["changes"] if c["kind"] == "conflict"],
              pv["conflicts"] == 0)
        merged = doc.mergeTransactionBranch("side")
        check("and is done", (merged["unresolved"], merged["failed"]) == ([], []))
        doc.recompute()
        check("merged: the face by name (%r)" % named(doc.Cut), named(doc.Cut) == {side: RED})
        check("and drawn where theirs drew it (%r)" % shown(doc.Cut), shown(doc.Cut) == there)

        # The same with ours drilling through: seven faces where theirs
        # had six, and the painted one is not the face it was by number.
        doc2 = cut("PaintDrill", folder)
        doc2.createTransactionBranch("side")
        paint(doc2, "side red", "Cut", {face(doc2.Cut, XMin=10): RED})
        there = shown(doc2.Cut)
        doc2.switchTransactionBranch("main")
        doc2.openTransaction("main drills")
        doc2.Cyl.Placement.Base = V(5, 5, -10)
        doc2.recompute()
        doc2.commitTransaction()
        check("ours has a seventh face", len(doc2.Cut.Shape.Faces) == 7)
        merged = doc2.mergeTransactionBranch("side")
        check("merged with nothing asked", (merged["unresolved"], merged["failed"]) == ([], []))
        doc2.recompute()
        check("the face by name, as the merged shape numbers it (%r)" % named(doc2.Cut),
              named(doc2.Cut) == {face(doc2.Cut, XMin=10): RED})
        check("and drawn on that face alone (%r)" % shown(doc2.Cut), shown(doc2.Cut) == there)
        App.closeDocument(doc.Name)
        App.closeDocument(doc2.Name)
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        p.SetInt("TransactionLog", mode)
        App.saveParameter()
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
