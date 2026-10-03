"""A view's own visibility table costs no other view its pick culling.

The per-object separators under the views of one document are shared,
and so are their bounding box caches, the only thing SoSeparator::rayPick
culls with. Every object's switch used to read SoFCVisibilityElement,
whose match is the view's table: once one view had any entry, the caches
matched only the view that built them, and every pick in the other view
walked every object down to its triangles (400 boxes: about 20 times the
cost). Now only the switch of an object some view has an entry for reads
the element.

Two views of a 20x20 grid of boxes, top view. A pick through a gap
(inside the scene's bounds, so the top-level cull cannot take it) is
timed in view B, with no table anywhere, then with view A hiding one
box. Claims:
  - A still does not pick the box it hides, and B still does;
  - B's pick through the gap costs about the same as before A's entry
    (well under 3x; the defect was ~20x).
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PickCull"
N = 20
PITCH = 6.0
REPS = 200


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle():
    for _ in range(10):
        QtCore.QCoreApplication.processEvents()


def activate(view):
    FreeCADGui.getMainWindow().setActiveWindow(view)
    settle()
    view.redraw()
    settle()


def at(view, x, y):
    p = view.getPointOnViewport(FreeCAD.Vector(x, y, 0.5))
    return (int(p[0]), int(p[1]))


def picked(view, pt):
    info = view.getObjectInfo(pt)
    return info.get("Object") if info else None


def cost(view, pt):
    """Median of five batches, in us per pick."""
    view.getObjectInfo(pt)
    batches = []
    for _ in range(5):
        t = time.perf_counter()
        for _ in range(REPS):
            view.getObjectInfo(pt)
        batches.append((time.perf_counter() - t) / REPS * 1e6)
    return sorted(batches)[2]


def run():
    try:
        doc = FreeCAD.newDocument(DOC)
        for i in range(N):
            for j in range(N):
                b = doc.addObject("Part::Box", "Box%d" % (i * N + j))
                b.Length = b.Width = b.Height = 1
                b.Placement.Base = FreeCAD.Vector(i * PITCH, j * PITCH, 0)
        doc.recompute()
        gdoc = FreeCADGui.getDocument(DOC)
        va = gdoc.activeView()
        FreeCADGui.runCommand("Std_ViewCreate")
        settle()
        vb = gdoc.activeView()
        if va is vb or len(gdoc.mdiViewsOfType("Gui::View3DInventor")) != 2:
            note("ABORT no second 3D view")
            return
        for v in (va, vb):
            activate(v)
            v.viewTop()
            v.fitAll()
        for v in (va, vb, va, vb):
            activate(v)

        mid = PITCH * (N // 2)
        gap = at(vb, mid + PITCH / 2, mid + PITCH / 2)
        check("the gap picks nothing", picked(vb, gap) is None, picked(vb, gap))
        base = cost(vb, gap)

        box0 = doc.getObject("Box0")
        va.setObjectVisibility(box0, False, "")
        for v in (va, vb, va, vb):
            activate(v)
        check("A does not pick the box it hides",
              picked(va, at(va, 0.5, 0.5)) != "Box0", picked(va, at(va, 0.5, 0.5)))
        check("B still picks it", picked(vb, at(vb, 0.5, 0.5)) == "Box0",
              picked(vb, at(vb, 0.5, 0.5)))
        withtable = cost(vb, gap)
        note("B gap pick: %.1f us without a table, %.1f us with A's" % (base, withtable))
        check("B keeps its pick culling", withtable < 3.0 * base,
              "%.1f vs %.1f us" % (withtable, base))
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
