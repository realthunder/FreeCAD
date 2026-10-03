"""A reopen builds each shape's visual once, and resolving the saved
per-view entries builds none.

Found 2026-09-29 (docs/CoinRetirement.md 5.27): the first resolution pass
after a reopen cost ~350 us per entry against ~8 on the loaded scene. A
path entry ("Part0.B0_3.") resolves through the box's getDetailPath, which
asked for the shape even with no element to look up; a progressive load
leaves the shape in the blob store, so the ask restored it, and the
restore's property change rebuilt the visual -- inside the pass, once per
entry. Fixed, the shapes were left to the load's drain, which built every
blob-held one TWICE: updateVisual's own read of the property is the
fault-in, the landing shape's notification builds the visual inside that
read, and the outer call went on to build it again.

Scene: four App::Parts of 50 boxes and a Link to each Part, one 3D view,
mode 3. Saved with no entries and with 100 path entries hiding boxes.
Claims, by FreeCADGui.visualBuildStats (builds since the open began) and
viewVisibilityStats (passBuilds: builds that happened inside a pass):
  - a reopen without entries builds each box once;
  - a reopen with entries: the pass resolves all 100 and builds nothing,
    and the load still builds each box once;
  - every box has geometry after the reopen (the early return after the
    fault-in did not leave one unbuilt).

Scored against the tree before the fix: builds are twice the boxes on
both reopens and the pass builds all 100 hidden boxes itself.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
NPART, NBOX, E = 4, 50, 100

FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
    "AutoSaveEnabled", False)


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(n=5):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()


def build():
    doc = FreeCAD.newDocument("ReopenBuilds")
    for p in range(NPART):
        part = doc.addObject("App::Part", "Part%d" % p)
        part.Placement.Base = FreeCAD.Vector(p * 60, 0, 0)
        boxes = []
        for i in range(NBOX):
            b = doc.addObject("Part::Box", "B%d_%d" % (p, i))
            b.Length = b.Width = b.Height = 1
            b.Placement.Base = FreeCAD.Vector((i % 10) * 5, (i // 10) * 5, 0)
            boxes.append(b)
        part.addObjects(boxes)
        link = doc.addObject("App::Link", "Link%d" % p)
        link.LinkedObject = part
        link.Placement.Base = part.Placement.Base + FreeCAD.Vector(0, 40, 0)
    doc.recompute()
    return doc


def view_of(doc):
    return FreeCADGui.getDocument(doc.Name).mdiViewsOfType("Gui::View3DInventor")[0]


def reopen(path):
    FreeCADGui.viewVisibilityStats(True)
    doc = FreeCAD.openDocument(path)
    while FreeCADGui.isBuildingVisuals():
        QtCore.QCoreApplication.processEvents()
    settle(10)
    return doc, FreeCADGui.visualBuildStats(), FreeCADGui.viewVisibilityStats(True)


def empty_boxes(doc):
    gdoc = FreeCADGui.getDocument(doc.Name)
    action = coin.SoGetBoundingBoxAction(coin.SbViewportRegion())
    empty = []
    for obj in doc.Objects:
        if obj.TypeId != "Part::Box":
            continue
        action.apply(gdoc.getObject(obj.Name).RootNode)
        if action.getBoundingBox().isEmpty():
            empty.append(obj.Name)
    return empty


def run():
    try:
        doc = build()
        view = view_of(doc)
        view.viewTop()
        view.fitAll()
        settle(10)
        try:
            view.saveRenderDump(os.path.join(OUT, "dump.png"))
            backend = True
        except Exception:
            backend = False
        if not check("render mode is 3", backend):
            return
        boxes = NPART * NBOX
        plain = os.path.join(OUT, "plain.FCStd")
        doc.saveAs(plain)
        view.ObjectVisibilities = {
            "Part%d.B%d_%d." % (i % NPART, i % NPART, (i * 7) % NBOX): "0"
            for i in range(E)}
        settle()
        hidden = os.path.join(OUT, "hidden.FCStd")
        doc.saveAs(hidden)
        FreeCAD.closeDocument(doc.Name)
        settle(10)

        doc, builds, vis = reopen(plain)
        note("plain: builds=%d (%.3fs)" % (builds["count"], builds["seconds"]))
        check("reopen without entries: each box built once",
              builds["count"] == boxes, "%d builds, %d boxes" % (builds["count"], boxes))
        check("reopen without entries: every box has geometry",
              not empty_boxes(doc), empty_boxes(doc)[:5])
        FreeCAD.closeDocument(doc.Name)
        settle(10)

        doc, builds, vis = reopen(hidden)
        note("hidden: builds=%d (%.3fs) passes=%d resolved=%d passBuilds=%d passMs=%.2f"
             % (builds["count"], builds["seconds"], vis["passes"], vis["passResolved"],
                vis["passBuilds"], vis["passNs"] / 1e6))
        check("reopen with entries: the map came back",
              len(view_of(doc).ObjectVisibilities) == E, len(view_of(doc).ObjectVisibilities))
        check("reopen with entries: the pass resolved every entry",
              vis["passes"] >= 1 and vis["passResolved"] == E, vis["passResolved"])
        check("reopen with entries: the pass built no visual",
              vis["passBuilds"] == 0, vis["passBuilds"])
        check("reopen with entries: each box built once",
              builds["count"] == boxes, "%d builds, %d boxes" % (builds["count"], boxes))
        check("reopen with entries: every box has geometry",
              not empty_boxes(doc), empty_boxes(doc)[:5])
        FreeCAD.closeDocument(doc.Name)
        settle(10)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, run)
