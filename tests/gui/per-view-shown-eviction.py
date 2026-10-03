"""Released per-view-shown objects are evicted first under memory pressure.

A hidden object one view shows on its own is captured for every view
(tagged, admitted only by that view). When no view shows it any more
the entry is RELEASED and kept for a quick show again, until the level
plan finds memory over the watermark (PerViewShownEvictWatermark, a
fraction of the GPU budget) and evicts released entries -- ahead of any
sweep that costs visible quality, the big and the long released first.

GL states no GPU budget, so one is simulated (GpuMemoryBudgetMB = 1, the
least it takes); the scene uploads under it, so the downgrade sweep has
nothing to do and the watermark alone decides. LevelDebug makes the plan
name what it evicts in the report view.

Two views: A shows hidden H1, B shows hidden H2; then A lets H1 go.
Claims:
  - at the default watermark (0.9), which the scene stands under,
    nothing is evicted: a released object is kept for a quick show;
  - with the watermark lowered under the scene, the plan evicts H1;
  - it never evicts H2, which B still shows;
  - showing H1 again in A after the eviction draws it again (the next
    capture brings it back).
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ShownEvict"
RENDER = "User parameter:BaseApp/Preferences/View/Render"


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


def run_for(seconds, views):
    """Keep both views drawing long enough for the level plan's timer
    (300 ms after the last observation) to fire."""
    end = time.time() + seconds
    while time.time() < end:
        for v in views:
            v.redraw()
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.05)


def report_text():
    mw = FreeCADGui.getMainWindow()
    for w in mw.findChildren(QtWidgets.QTextEdit):
        if w.objectName() == "Report view":
            return w.toPlainText()
    return ""


def pixel(view, pt, tag):
    """The backend frame at the projection of 3D point pt."""
    activate(view)
    path = os.path.join(OUT, "%s.png" % tag)
    view.saveRenderDump(path, "renderer")
    img = QtGui.QImage(path)
    x, y = view.getPointOnViewport(pt)
    c = QtGui.QColor(img.pixel(int(x), int(img.height() - 1 - y)))
    return (c.red(), c.green(), c.blue())


def run():
    try:
        params = FreeCAD.ParamGet(RENDER)
        params.SetBool("LevelDebug", True)
        params.SetInt("GpuMemoryBudgetMB", 1)
        # The default first: the scene stands under it.
        params.SetFloat("PerViewShownEvictWatermark", 0.9)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)

        doc = FreeCAD.newDocument(DOC)
        box = doc.addObject("Part::Box", "Box")
        # Both spheres in front of the box, inside the frame the box
        # alone fits: the test does not lean on a fit-all.
        h1 = doc.addObject("Part::Sphere", "H1")
        h1.Radius = 3
        h1.Placement.Base = FreeCAD.Vector(5, -10, 5)
        h2 = doc.addObject("Part::Sphere", "H2")
        h2.Radius = 3
        h2.Placement.Base = FreeCAD.Vector(5, -10, 5)
        doc.recompute()
        h1.ViewObject.ShapeColor = (1.0, 0.0, 0.0)
        h1.ViewObject.Visibility = False
        h2.ViewObject.Visibility = False

        gdoc = FreeCADGui.getDocument(DOC)
        va = gdoc.activeView()
        FreeCADGui.runCommand("Std_ViewCreate")
        settle()
        vb = gdoc.activeView()
        if va is vb:
            note("ABORT no second 3D view")
            return
        for v in (va, vb):
            activate(v)
            v.viewFront()
            v.fitAll()

        for v in (va, vb):
            v.PerViewVisibilities = True
        va.setObjectVisibility(h1, True)
        vb.setObjectVisibility(h2, True)
        # The show forces the hidden spheres to tessellate, and a fine
        # one takes a moment.
        run_for(3.0, (va, vb))
        for v in (va, vb, va, vb):
            activate(v)
        centre = FreeCAD.Vector(5, -13, 5)
        shown = pixel(va, centre, "a-shown")
        if not check("A draws H1 while it shows it",
                     shown[0] > 150 and shown[1] < 90, shown):
            return

        # A lets H1 go: released, kept, until pressure evicts it.
        va.setObjectVisibility(h1, None)
        for v in (va, vb):
            activate(v)
        run_for(3.0, (va, vb))
        check("under the watermark a released object is kept",
              "per-view-shown" not in report_text())

        # About 50 KB: the scene stands over it and under the budget, so
        # the eviction is the only thing the plan has to do.
        params.SetFloat("PerViewShownEvictWatermark", 0.05)
        run_for(3.0, (va, vb))

        text = report_text()
        lines = [l for l in text.splitlines() if "per-view-shown" in l]
        for l in lines:
            note("  log: " + l.strip())
        check("the plan evicts H1, released",
              any(("%s#H1" % DOC) in l for l in lines), len(lines))
        check("it never evicts H2, still shown by B",
              not any(("%s#H2" % DOC) in l for l in lines))

        va.setObjectVisibility(h1, True)
        for v in (va, vb, va):
            activate(v)
        again = pixel(va, centre, "a-again")
        check("H1 shown again after the eviction draws again",
              again[0] > 150 and again[1] < 90, again)
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
