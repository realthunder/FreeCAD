"""Phase 0 of the transaction log (docs/TransactionLog.md sec 15): drive the
commit-path measurement over the scenarios the design names and write the CSV.

Run under FreeCADCmd:

    TXN_MEASURE_CSV=out.csv [TXN_MEASURE_STEP=file.step] [TXN_MEASURE_BASELINE=1] \
        FreeCADCmd scripts/measure-transactions.py

(Environment rather than arguments: FreeCADCmd opens every argument that is
not a script as a document.) Default output: ./txn-measure.csv.

Scenarios, each under a marker row in the CSV:

  scalar     one property on one object (Box.Length), with the recompute
             inside the transaction as a GUI edit has it
  sketch     a 1000-line sketch: creation, then one vertex moved (a drag's
             hundreds of intermediate writes collapse to one before/after)
  pad        a Pad edit: the input (Length) and the derived Shape, so each
             derived-value policy can be costed offline from the same rows
  import     a STEP import (data/examples/Schenkel.stp by default)
  bigimport  a synthesised large solid round-tripped through STEP
  snapshot   a save of the resulting document, timed, with the archive split
             into Document.xml and the rest (the cost of a version, sec 16.1)

The summary is printed at the end and written next to the CSV as .txt;
scripts/summarize-transaction-measure.py re-derives it from the CSV alone.
"""

import os
import time
import zipfile

import FreeCAD as App
import Part
import Sketcher

csv_path = os.environ.get("TXN_MEASURE_CSV")
step_path = os.environ.get("TXN_MEASURE_STEP")
# Same scenarios, no measurement: the commit times to compare against.
baseline = bool(os.environ.get("TXN_MEASURE_BASELINE"))

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
if csv_path is None:
    csv_path = os.path.join(os.getcwd(), "txn-measure.csv")
if step_path is None:
    step_path = os.path.join(root, "data", "examples", "Schenkel.stp")

timings = []


class Txn:
    """A GUI-shaped transaction: open, edit, recompute, commit, timed whole."""

    def __init__(self, doc, name, recompute=True):
        self.doc = doc
        self.name = name
        self.recompute = recompute

    def __enter__(self):
        self.t0 = time.perf_counter()
        App.setActiveTransaction(self.name)
        return self

    def __exit__(self, *exc):
        if self.recompute:
            self.doc.recompute()
        t1 = time.perf_counter()
        App.closeActiveTransaction()
        t2 = time.perf_counter()
        timings.append((self.name, t1 - self.t0, t2 - t1))
        return False


def mark(label):
    if not baseline:
        App.markTransactionMeasure(label)
    App.Console.PrintMessage("== %s\n" % label)


def scenario_scalar(doc):
    mark("scalar:setup")
    with Txn(doc, "make box"):
        box = doc.addObject("Part::Box", "Box")
    mark("scalar")
    for n in range(5):
        with Txn(doc, "edit Length"):
            box.Length = 10 + n
    mark("scalar:norecompute")
    for n in range(5):
        with Txn(doc, "edit Length norecompute", recompute=False):
            box.Length = 20 + n
    doc.recompute()


def make_sketch(doc, name, n):
    sk = doc.addObject("Sketcher::SketchObject", name)
    geo = []
    for k in range(n):
        x = (k % 50) * 2.0
        y = (k // 50) * 2.0
        geo.append(Part.LineSegment(App.Vector(x, y, 0), App.Vector(x + 1, y + 1, 0)))
    sk.addGeometry(geo, False)
    cons = []
    for k in range(n):
        cons.append(Sketcher.Constraint("DistanceX", k, 1, k, 2, 1.0))
    sk.addConstraint(cons)
    return sk


def scenario_sketch(doc):
    mark("sketch:create1000")
    with Txn(doc, "create sketch"):
        sk = make_sketch(doc, "Sketch1000", 1000)
    mark("sketch:moveone")
    for n in range(3):
        with Txn(doc, "move point"):
            sk.movePoint(0, 2, App.Vector(1.5 + n * 0.1, 1.5, 0), 0)
    mark("sketch:drag")
    # A drag: many intermediate writes inside one transaction.
    with Txn(doc, "drag point"):
        for step in range(200):
            sk.movePoint(1, 2, App.Vector(3.0 + step * 0.01, 1.5, 0), 1)
    mark("sketch:addline")
    with Txn(doc, "add line"):
        sk.addGeometry(Part.LineSegment(App.Vector(0, 0, 0), App.Vector(5, 5, 0)), False)
    mark("sketch:small")
    with Txn(doc, "create small sketch"):
        sk2 = make_sketch(doc, "Sketch10", 10)
    with Txn(doc, "move point small"):
        sk2.movePoint(0, 2, App.Vector(1.5, 1.5, 0), 0)


def scenario_pad(doc):
    mark("pad:setup")
    with Txn(doc, "make body"):
        body = doc.addObject("PartDesign::Body", "Body")
        sk = body.newObject("Sketcher::SketchObject", "PadSketch")
        sk.AttachmentSupport = (doc.getObject("XY_Plane"), [""])
        sk.MapMode = "FlatFace"
        pts = [
            App.Vector(0, 0, 0),
            App.Vector(30, 0, 0),
            App.Vector(30, 20, 0),
            App.Vector(0, 20, 0),
        ]
        for k in range(4):
            sk.addGeometry(Part.LineSegment(pts[k], pts[(k + 1) % 4]), False)
        for k in range(4):
            sk.addConstraint(Sketcher.Constraint("Coincident", k, 2, (k + 1) % 4, 1))
        doc.recompute()
        pad = body.newObject("PartDesign::Pad", "Pad")
        pad.Profile = sk
        pad.Length = 10
    mark("pad")
    for n in range(5):
        with Txn(doc, "edit Pad.Length"):
            pad.Length = 11 + n
    mark("pad:fillet")
    with Txn(doc, "add fillet"):
        fillet = body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (pad, ["Edge1", "Edge2", "Edge3", "Edge4"])
        fillet.Radius = 2
    mark("pad:editunderfillet")
    for n in range(3):
        with Txn(doc, "edit Pad.Length under fillet"):
            pad.Length = 20 + n


def scenario_import(doc):
    if not os.path.exists(step_path):
        App.Console.PrintWarning("no STEP file at %s\n" % step_path)
        return
    mark("import:" + os.path.basename(step_path))
    with Txn(doc, "import step"):
        Part.insert(step_path, doc.Name)


def scenario_bigimport(doc, tmpdir):
    mark("bigimport:build")
    # A solid with a few thousand faces: a fused grid of filleted boxes.
    boxes = []
    for i in range(12):
        for j in range(12):
            b = Part.makeBox(8, 8, 8, App.Vector(i * 10, j * 10, 0))
            b = b.makeFillet(1.0, b.Edges)
            boxes.append(b)
    base = Part.makeBox(120, 120, 2, App.Vector(-1, -1, -2))
    solid = base.fuse(boxes)
    solid = solid.removeSplitter()
    path = os.path.join(tmpdir, "big.step")
    solid.exportStep(path)
    App.Console.PrintMessage(
        "big.step: %d faces, %d bytes\n" % (len(solid.Faces), os.path.getsize(path))
    )
    mark("bigimport")
    with Txn(doc, "import big step"):
        Part.insert(path, doc.Name)
    mark("bigimport:setshape")
    # obj.Shape = s from Python: an input shape, not derived.
    with Txn(doc, "set shape"):
        f = doc.addObject("Part::Feature", "BigFeature")
        f.Shape = solid
    mark("bigimport:move")
    with Txn(doc, "move big"):
        f.Placement = App.Placement(App.Vector(0, 0, 50), App.Rotation())


def scenario_snapshot(doc, tmpdir):
    mark("snapshot")
    path = os.path.join(tmpdir, "snapshot.FCStd")
    t0 = time.perf_counter()
    doc.saveCopy(path)
    t1 = time.perf_counter()
    total = os.path.getsize(path)
    docxml = 0
    entries = 0
    with zipfile.ZipFile(path) as z:
        for info in z.infolist():
            entries += 1
            if info.filename == "Document.xml":
                docxml = info.file_size
    return {"time_s": t1 - t0, "bytes": total, "docxml_bytes": docxml, "entries": entries,
            "objects": len(doc.Objects)}


def main():
    tmpdir = os.path.join(os.path.dirname(csv_path), "txn-measure-tmp")
    os.makedirs(tmpdir, exist_ok=True)
    if not baseline and not App.startTransactionMeasure(csv_path):
        raise SystemExit("cannot open " + csv_path)
    doc = App.newDocument("TxnMeasure")
    # FreeCADCmd documents have undo off; without it no transaction opens.
    doc.UndoMode = 1
    try:
        scenario_scalar(doc)
        scenario_sketch(doc)
        scenario_pad(doc)
        scenario_import(doc)
        scenario_bigimport(doc, tmpdir)
        snap = scenario_snapshot(doc, tmpdir)
    finally:
        App.stopTransactionMeasure()

    lines = ["snapshot: %s" % snap, "", "transaction  edit+recompute_s  commit_s"]
    for name, edit, commit in timings:
        lines.append("%-32s %10.4f %10.4f" % (name, edit, commit))
    text = "\n".join(lines) + "\n"
    App.Console.PrintMessage(text)
    suffix = "-baseline.txt" if baseline else ".txt"
    with open(os.path.splitext(csv_path)[0] + suffix, "w") as f:
        f.write(text)
    App.Console.PrintMessage("CSV: %s\n" % csv_path)


main()
