"""A Part object keeps the tessellation settings its file was saved with
when a view opens or closes, and the instancing gate still rebuilds.

Found 2026-09-29 (docs/DocumentLoad.md sec 16) from pixel differences
between an eager and a progressive open of a user file: the shape-
instancing gate re-runs ViewProviderPartExt::reload() on every Part view
provider whenever a view's renderer attaches or goes away, and reload()
wrote the Deviation and AngularDeflection preferences into every object
whose own values differed. A file saved at 0.5 / 5 deg per object came
out at 0.2 / 28.65 once its own view opened -- before its exact mesh in
an eager open, after it in a progressive one, so the two meshed
differently. Only a change of a tessellation preference writes them now.

The gate itself had leaned on that overwrite: an object whose values
already matched the preferences was not rebuilt when the gate flipped, and
kept the instanced representation under plain Coin.

Claims:
  - an object's saved Deviation / AngularDeflection survive the open, a
    second document's view opening, and that view closing;
  - a change of the Deviation preference still reaches every object;
  - a file value below the minimum preferences meshes as the minimum
    does (the limit that keeps an unreasonable value from hanging an
    open is enforced where the mesh is made, not by the overwrite);
  - a compound of shared solids is built instanced under the render
    cache, flattened when the render cache is switched off, and
    instanced again when it is back.

Scored against the tree before the fix: the first three fail (0.2 /
28.65 from the open on) and the flattening fails (still instanced).
"""
import os
import time
import traceback
import zipfile

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
V = FreeCAD.Vector
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
PART = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Part")
VIEW.SetInt("RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
    "AutoSaveEnabled", False)


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def wait(seconds):
    t = time.perf_counter()
    while time.perf_counter() - t < seconds:
        QtCore.QCoreApplication.processEvents()


def count(vp, node_type):
    sa = coin.SoSearchAction()
    sa.setType(node_type.getClassTypeId())
    sa.setInterest(coin.SoSearchAction.ALL)
    sa.setSearchingAll(True)
    sa.apply(vp.RootNode)
    return sa.getPaths().getLength()


def points(vp):
    sa = coin.SoSearchAction()
    sa.setType(coin.SoCoordinate3.getClassTypeId())
    sa.setInterest(coin.SoSearchAction.ALL)
    sa.setSearchingAll(True)
    sa.apply(vp.RootNode)
    paths = sa.getPaths()
    return max([paths.get(i).getTail().point.getNum()
                for i in range(paths.getLength())] or [0])


def tess(vp):
    return (round(vp.Deviation, 4), round(float(vp.AngularDeflection), 4))


def patch_gui_xml(path, name, values):
    """Rewrite the saved Deviation / AngularDeflection of one view
    provider -- a file value below the constraint the property would take
    from a setter."""
    with zipfile.ZipFile(path) as z:
        entries = {i.filename: z.read(i.filename) for i in z.infolist()}
    xml = entries["GuiDocument.xml"].decode("utf-8")
    start = xml.index('<ViewProvider name="%s"' % name)
    end = xml.index("</ViewProvider>", start)
    block = xml[start:end]
    for prop, value in values.items():
        at = block.index('<Property name="%s"' % prop)
        val = block.index('value="', at) + len('value="')
        block = block[:val] + value + block[block.index('"', val):]
    entries["GuiDocument.xml"] = (xml[:start] + block + xml[end:]).encode("utf-8")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for n, data in entries.items():
            z.writestr(n, data)


def build(path):
    doc = FreeCAD.newDocument("TessReload")
    kept = doc.addObject("Part::Cylinder", "Kept")
    box = Part.makeBox(2, 2, 2)
    kids = []
    for i in range(4):
        m = FreeCAD.Matrix()
        m.move(V(i * 3, 0, 20))
        kids.append(box.transformed(m, False))
    comp = doc.addObject("Part::Feature", "Comp")
    comp.Shape = Part.makeCompound(kids)
    doc.recompute()
    wait(1)
    kept.ViewObject.Deviation = 0.5
    kept.ViewObject.AngularDeflection = 5
    doc.saveAs(path)
    FreeCAD.closeDocument(doc.Name)
    wait(0.5)


def build_limit(path, low):
    """One cylinder per file: the blob store keeps one entry for equal
    shapes in a document, and equal shapes restored from it share their
    mesh, so the two sides of the comparison live in different files."""
    doc = FreeCAD.newDocument("TessLimit")
    cyl = doc.addObject("Part::Cylinder", "Cyl")
    doc.recompute()
    wait(1)
    cyl.ViewObject.Deviation = PART.GetFloat("MinimumDeviation", 0.05)
    cyl.ViewObject.AngularDeflection = PART.GetFloat("MinimumAngularDeflection", 5.0)
    doc.saveAs(path)
    FreeCAD.closeDocument(doc.Name)
    wait(0.5)
    if low:
        patch_gui_xml(path, "Cyl", {"Deviation": "0.001", "AngularDeflection": "0.5"})


def open_limit(path):
    doc = FreeCAD.openDocument(path)
    wait(2)
    vp = doc.getObject("Cyl").ViewObject
    res = (tess(vp), points(vp))
    FreeCAD.closeDocument(doc.Name)
    wait(0.5)
    return res


def run():
    path = os.path.join(OUT, "tess-reload.FCStd")
    build(path)
    doc = FreeCAD.openDocument(path)
    wait(2)
    kept = doc.getObject("Kept").ViewObject
    check("the open keeps the saved tessellation", tess(kept) == (0.5, 5.0), tess(kept))
    other = FreeCAD.newDocument("Other")
    other.addObject("Part::Box", "B")
    other.recompute()
    wait(2)
    check("a second view opening keeps it", tess(kept) == (0.5, 5.0), tess(kept))
    FreeCAD.closeDocument(other.Name)
    wait(2)
    check("that view closing keeps it", tess(kept) == (0.5, 5.0), tess(kept))

    comp = doc.getObject("Comp").ViewObject
    inst = count(comp, coin.SoGroup)
    VIEW.SetInt("RenderCache", 0)
    wait(2)
    flat = count(comp, coin.SoGroup)
    VIEW.SetInt("RenderCache", 3)
    wait(2)
    again = count(comp, coin.SoGroup)
    check("the compound is flattened when the render cache goes off",
          flat < inst, "groups: instanced %d, flat %d" % (inst, flat))
    check("and instanced again when it is back", again == inst,
          "groups: %d then %d" % (inst, again))

    old = PART.GetFloat("MeshDeviation", 0.2)
    try:
        PART.SetFloat("MeshDeviation", 0.3)
        wait(1)
        check("a Deviation preference change reaches the objects",
              round(kept.Deviation, 4) == 0.3, kept.Deviation)
    finally:
        PART.SetFloat("MeshDeviation", old)
        wait(1)
    FreeCAD.closeDocument(doc.Name)
    wait(0.5)

    lpath = os.path.join(OUT, "tess-low.FCStd")
    mpath = os.path.join(OUT, "tess-min.FCStd")
    build_limit(lpath, True)
    build_limit(mpath, False)
    low, atmin = open_limit(lpath), open_limit(mpath)
    check("a value below the minimum meshes as the minimum does",
          low[1] == atmin[1] > 0,
          "below %s %d points, at minimum %s %d" % (low[0], low[1], atmin[0], atmin[1]))


def main():
    try:
        run()
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        VIEW.SetInt("RenderCache", 3)
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, main)
