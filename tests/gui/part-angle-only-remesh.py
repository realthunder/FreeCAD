"""A Part object re-meshes when only its angular deflection changes.

Found 2026-10-04 while checking a probe that read a fresh cylinder's
triangle count. OCCT records a triangulation's linear deflection, not its
angle, and its mesher keeps a resident mesh whose deflection still fits
the ask. ViewProviderPartExt never cleared the mesh before re-meshing
(upstream FreeCAD calls BRepTools::Clean before every mesh call), so an
AngularDeflection edit -- by property, by its preference, or with
OverrideTessellation -- changed nothing on screen while the Deviation
stayed. The build now clears a shape's mesh when the angle it asks differs
from the one its last build of the same shape asked.

Coarse-first tessellation is switched off (CoarseTessellation -1): with it
on, the view first shows a ladder rung and refines once its camera
settles, and what is measured here is the build's own mesh.

Claims, each against a fresh object built at the same values:
  - an angle-only edit on a displayed object meshes as the fresh one does;
  - editing back meshes as the original again;
  - an edit made while the object is hidden is applied when it is shown;
  - a change of the MeshAngularDeflection preference reaches the mesh.

Scored against the tree before the fix: the first, third and fourth fail
(the edited object keeps its 28.65 deg mesh), the second passes.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
RENDER = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
PART = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Part")
VIEW.SetInt("RenderCache", 3)
RENDER.SetInt("CoarseTessellation", -1)
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


def tris(obj):
    return sum(f.countTriangles() for f in obj.Shape.Faces)


def cylinder(doc, name, angle=None):
    obj = doc.addObject("Part::Cylinder", name)
    obj.Radius = 10
    obj.Height = 1
    if angle is not None:
        obj.ViewObject.AngularDeflection = angle
    doc.recompute()
    wait(0.5)
    return obj


def run():
    doc = FreeCAD.newDocument("AngleOnly")
    default = PART.GetFloat("MeshAngularDeflection", 28.65)
    fine = cylinder(doc, "Fine", 5.0)
    coarse = cylinder(doc, "Coarse")
    check("the two references differ", tris(fine) > tris(coarse),
          "5 deg %d, %s deg %d" % (tris(fine), default, tris(coarse)))

    edited = cylinder(doc, "Edited")
    edited.ViewObject.AngularDeflection = 5.0
    wait(0.5)
    check("an angle-only edit meshes as a fresh object does",
          tris(edited) == tris(fine), "%d, fresh %d" % (tris(edited), tris(fine)))
    edited.ViewObject.AngularDeflection = default
    wait(0.5)
    check("editing back meshes as the original again",
          tris(edited) == tris(coarse), "%d, fresh %d" % (tris(edited), tris(coarse)))

    hidden = cylinder(doc, "Hidden")
    hidden.ViewObject.Visibility = False
    hidden.ViewObject.AngularDeflection = 5.0
    hidden.ViewObject.Visibility = True
    wait(0.5)
    check("an edit made while hidden is applied when shown",
          tris(hidden) == tris(fine), "%d, fresh %d" % (tris(hidden), tris(fine)))

    pref = cylinder(doc, "Pref")
    try:
        PART.SetFloat("MeshAngularDeflection", 5.0)
        wait(1)
        check("a MeshAngularDeflection preference change reaches the mesh",
              tris(pref) == tris(fine),
              "%d (AngularDeflection %s), fresh %d"
              % (tris(pref), pref.ViewObject.AngularDeflection, tris(fine)))
    finally:
        PART.SetFloat("MeshAngularDeflection", default)
        wait(1)
    FreeCAD.closeDocument(doc.Name)
    wait(0.5)


def main():
    try:
        run()
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        RENDER.RemInt("CoarseTessellation")
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, main)
