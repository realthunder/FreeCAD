"""An instanced Part object draws a uniform face colour list at the
transparency the list says.

Found 2026-09-30 by progressive-load-diff.py on a user file
(FC0.21.1_Lead_Screw_12.12.23, docs/DocumentLoad.md sec 19): a Lattice
Populate -- a compound of repeated solids, which the render cache builds
instanced -- opened progressively with its faces fully transparent while
its DiffuseColor and Transparency said opaque. The uniform branch of
ViewProviderPartExt::applyInstancedFaceColors handed the colour's alpha to
the Coin material as its transparency; an alpha is an opacity here
(Base::Color::transparency()), so an opaque list drew invisible and a
transparent one drew opaque. The flat path and the per-instance override
materials already converted. The user file's eager open escaped only
because its colours landed before the instanced representation was built.

Scene: a Part::Feature holding a compound of three placed copies of one
box (18 faces), under the render cache, so instanced. Claims:
  - the compound is instanced (else nothing below tests the branch);
  - a uniform opaque list of 18 colours draws opaque, and one at 75%
    transparency draws at 0.75 (not 50%: 1 - 0.5 hides the defect) --
    live, as a user or a script sets it;
  - saved with the opaque list and opened eagerly, then progressively,
    the face material is opaque, as its properties say.

Scored against the tree before the fix: all four fail -- 1.0, 0.25, and
1.0 for both opens (here the eager open's colours also arrive after the
instanced build).
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
V = FreeCAD.Vector
RENDER = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
    "AutoSaveEnabled", False)
GREY = (0.8, 0.8, 0.8)
FACES = 18


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


def face_transparencies(vp):
    """The transparency of every material node carrying the face colour.
    The line and point materials are a different colour."""
    found = []
    sa = coin.SoSearchAction()
    sa.setType(coin.SoMaterial.getClassTypeId())
    sa.setInterest(coin.SoSearchAction.ALL)
    sa.setSearchingAll(True)
    sa.apply(vp.RootNode)
    for i in range(sa.getPaths().getLength()):
        mat = sa.getPaths().get(i).getTail()
        d = mat.diffuseColor
        if d.getNum() and all(abs(a - b) < 1e-3 for a, b in zip(d[0].getValue(), GREY)):
            t = mat.transparency
            found.extend(round(t[j], 3) for j in range(t.getNum()))
    return sorted(set(found))


def count_groups(vp):
    sa = coin.SoSearchAction()
    sa.setType(coin.SoGroup.getClassTypeId())
    sa.setInterest(coin.SoSearchAction.ALL)
    sa.setSearchingAll(True)
    sa.apply(vp.RootNode)
    return sa.getPaths().getLength()


def uniform(alpha):
    return [GREY + (alpha,)] * FACES


def build(doc):
    box = Part.makeBox(2, 2, 2)
    kids = []
    for i in range(3):
        m = FreeCAD.Matrix()
        m.move(V(i * 3, 0, 0))
        kids.append(box.transformed(m, False))
    comp = doc.addObject("Part::Feature", "Comp")
    comp.Shape = Part.makeCompound(kids)
    doc.recompute()
    wait(1)
    comp.ViewObject.ShapeColor = GREY
    return comp


def open_mode(path, progressive):
    RENDER.SetBool("ProgressiveLoad", progressive)
    doc = FreeCAD.openDocument(path)
    while FreeCADGui.isBuildingVisuals():
        QtCore.QCoreApplication.processEvents()
    wait(2)
    vp = doc.getObject("Comp").ViewObject
    res = (vp.Transparency, face_transparencies(vp))
    FreeCAD.closeDocument(doc.Name)
    wait(0.5)
    return res


def run():
    path = os.path.join(OUT, "instanced-transparency.FCStd")
    doc = FreeCAD.newDocument("InstTransp")
    comp = build(doc)
    vp = comp.ViewObject
    inst = count_groups(vp)
    # another render cache mode is a setting only under the render type
    # "Legacy": with the render engine it is put back to 3
    kind = RENDER.GetString("Type", "Default")
    RENDER.SetString("Type", "Legacy")
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 0)
    wait(2)
    flat = count_groups(vp)
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
    RENDER.SetString("Type", kind)
    wait(2)
    check("the compound is built instanced", inst > flat,
          "groups: instanced %d, flat %d" % (inst, flat))

    vp.DiffuseColor = uniform(1.0)
    wait(0.5)
    check("a uniform opaque list draws opaque",
          face_transparencies(vp) == [0.0] and vp.Transparency == 0,
          "Transparency %s, material %s" % (vp.Transparency, face_transparencies(vp)))
    vp.DiffuseColor = uniform(0.25)
    wait(0.5)
    check("a uniform list at 75% transparency draws at 0.75",
          face_transparencies(vp) == [0.75],
          "material %s" % face_transparencies(vp))
    vp.DiffuseColor = uniform(1.0)
    wait(0.5)

    doc.saveAs(path)
    FreeCAD.closeDocument(doc.Name)
    wait(0.5)
    for progressive in (False, True):
        mode = "progressive" if progressive else "eager"
        transp, mat = open_mode(path, progressive)
        check("opened %s, the faces draw opaque as the properties say" % mode,
              transp == 0 and mat == [0.0],
              "Transparency %s, material %s" % (transp, mat))


try:
    run()
except Exception:
    note("ABORT " + traceback.format_exc())
note("DONE")
RENDER.SetBool("ProgressiveLoad", True)
QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)
