"""Merge Sketches carries external geometry and remaps what refers to it
(upstream d34081b9fe).

The command copied each sketch's own geometry and its constraints, and
moved every geometry id in a constraint by the count of geometry merged so
far -- external ids included, which are negative and count the other way.
The merged sketch got no external geometry at all. So a constraint onto
external geometry either pointed at nothing (first sketch merged) or, moved
by the offset, at an axis or at some unrelated curve (later sketches),
without a word.

Two sketches on the XY plane, each with a line whose end lies on a
different edge of a box (PointOnObject onto external geometry); the second
also has a circle with a radius. A third refers to the first one's edge
again.

Claims about the merged sketch:
  - it has every sketch's geometry and the two external references,
    the shared one once;
  - every constraint is there, and each PointOnObject is onto the external
    geometry made from the edge its source used;
  - the radius constrains the circle;
  - it solves, and the line ends are where the sources had them.

Scored against the tree before the change: no external geometry, the
second PointOnObject onto the vertical axis.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
import Part
import Sketcher
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "MergeExternal"
V = FreeCAD.Vector


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(n=20):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()


def edge_name(box, p1, p2):
    """The box edge that runs between two points."""
    for i, e in enumerate(box.Shape.Edges):
        ends = [v.Point for v in e.Vertexes]
        if len(ends) == 2 and (
                (ends[0].isEqual(p1, 1e-6) and ends[1].isEqual(p2, 1e-6))
                or (ends[0].isEqual(p2, 1e-6) and ends[1].isEqual(p1, 1e-6))):
            return "Edge%d" % (i + 1)
    raise RuntimeError("no edge %s - %s" % (p1, p2))


def external_refs(sk):
    return sorted((o.Name, s) for o, subs in sk.ExternalGeometry for s in subs)


def external_of(sk, geo_id):
    """(object name, element) the external geometry `geo_id` was made from."""
    refs = [(o.Name, s) for o, subs in sk.ExternalGeometry for s in subs]
    index = -geo_id - 3
    return refs[index] if 0 <= index < len(refs) else None


def run():
    try:
        doc = FreeCAD.newDocument(DOC)
        box = doc.addObject("Part::Box", "Box")
        doc.recompute()
        front = edge_name(box, V(0, 0, 0), V(10, 0, 0))   # y = 0
        right = edge_name(box, V(10, 0, 0), V(10, 10, 0))  # x = 10

        a = doc.addObject("Sketcher::SketchObject", "A")
        a.addGeometry(Part.LineSegment(V(2, 4, 0), V(3, 1, 0)))
        a.addExternal("Box", front)
        a.addConstraint(Sketcher.Constraint("PointOnObject", 0, 2, -3))

        b = doc.addObject("Sketcher::SketchObject", "B")
        b.addGeometry(Part.LineSegment(V(5, 5, 0), V(8, 6, 0)))
        b.addGeometry(Part.Circle(V(5, 8, 0), V(0, 0, 1), 1.5))
        b.addExternal("Box", right)
        b.addConstraint(Sketcher.Constraint("Radius", 1, 1.5))
        b.addConstraint(Sketcher.Constraint("PointOnObject", 0, 2, -3))

        c = doc.addObject("Sketcher::SketchObject", "C")
        c.addGeometry(Part.LineSegment(V(7, 3, 0), V(8, 1, 0)))
        c.addExternal("Box", front)
        c.addConstraint(Sketcher.Constraint("PointOnObject", 0, 2, -3))
        doc.recompute()
        note("A end %s, B end %s" % (a.Geometry[0].EndPoint, b.Geometry[0].EndPoint))
        check("the sources solve onto their edges",
              abs(a.Geometry[0].EndPoint.y) < 1e-6
              and abs(b.Geometry[0].EndPoint.x - 10) < 1e-6)

        before = set(o.Name for o in doc.Objects)
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(a)
        FreeCADGui.Selection.addSelection(b)
        FreeCADGui.Selection.addSelection(c)
        settle()
        FreeCADGui.runCommand("Sketcher_MergeSketches")
        settle()
        new = [o for o in doc.Objects if o.Name not in before]
        if not check("the command made one sketch", len(new) == 1,
                     [o.Name for o in new]):
            return
        m = new[0]
        doc.recompute()

        check("it has every sketch's geometry", len(m.Geometry) == 4, len(m.Geometry))
        check("it has the two external references, the shared one once",
              external_refs(m) == sorted([("Box", front), ("Box", right)]),
              external_refs(m))
        cons = m.Constraints
        note("constraints: %s" % [
            (c.Type, c.First, c.FirstPos, c.Second) for c in cons])
        check("every constraint is there", len(cons) == 4, len(cons))
        on = [c for c in cons if c.Type == "PointOnObject"]
        rad = [c for c in cons if c.Type == "Radius"]
        check("A's point is on the external geometry of A's edge",
              len(on) == 3 and on[0].First == 0
              and external_of(m, on[0].Second) == ("Box", front),
              on and (on[0].First, on[0].Second, external_of(m, on[0].Second)))
        check("B's point is on the external geometry of B's edge",
              len(on) == 3 and on[1].First == 1
              and external_of(m, on[1].Second) == ("Box", right),
              len(on) == 3 and (on[1].First, on[1].Second,
                                external_of(m, on[1].Second)))
        check("C's point is on the external geometry A brought in",
              len(on) == 3 and on[2].First == 3 and on[2].Second == on[0].Second,
              len(on) == 3 and (on[2].First, on[2].Second))
        check("the radius constrains the circle",
              len(rad) == 1 and rad[0].First == 2, rad and rad[0].First)
        check("it solves", m.solve() == 0, m.solve())
        if len(m.Geometry) == 4:
            ea = m.Geometry[0].EndPoint
            eb = m.Geometry[1].EndPoint
            ec = m.Geometry[3].EndPoint
            check("the line ends are where the sources had them",
                  ea.isEqual(a.Geometry[0].EndPoint, 1e-6)
                  and eb.isEqual(b.Geometry[0].EndPoint, 1e-6)
                  and ec.isEqual(c.Geometry[0].EndPoint, 1e-6), (ea, eb, ec))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        finish()


def finish():
    try:
        FreeCAD.closeDocument(DOC)
    except Exception:
        pass
    note("DONE")
    QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, run)
