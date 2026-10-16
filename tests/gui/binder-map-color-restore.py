"""A SubShapeBinder keeps its saved Map*Color settings when its file is
opened, in either open mode.

Found 2026-09-29 by progressive-load-diff.py on a user file
(FC0.21.1_Lead_Screw_12.12.23, docs/DocumentLoad.md sec 19): two binders
saved with MapFaceColor true opened with it false eagerly and true
progressively. ViewProviderSubShapeBinder::onChanged turns a Map*Color
off when its colour is set -- meant for a user picking a colour -- and
did it for the colour being read from the file too: properties restore
in name order, so MapFaceColor is read before ShapeColor, which then
switched it off again. Only UseBinderStyle had the restore guard
(b0621a4b5b).

Scene: a binder in the binder style (its own colour), saved with the
face colour mapped, as the user file's were. The flags are the object's
(docs/ShapeAppearanceDesign.md sec 14.6) and the view provider's are names
over them, so both are read. Claims, per open mode (ProgressiveLoad off,
then on): the binder's MapFaceColor, MapLineColor, MapPointColor and
MapTransparency are the saved ones, by either name. And after the opens,
setting the colour still turns the mapping off, as it did -- by the view
provider's name and by the object's (docs/TransactionLog.md sec 33.16) --
and the binder is drawn in the colour set.

Scored against the tree before the fix: the eager open reads
MapFaceColor false. And against the tree as first merged with the looks
kept on the object (docs/TransactionLog.md sec 33.14): the view provider's
flag reads false where the object's reads true, in both open modes, and a
colour set leaves the object's on -- the object's look arriving under the
view provider's name was taken for a colour picked.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
RENDER = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
    "AutoSaveEnabled", False)
FLAGS = ("MapFaceColor", "MapLineColor", "MapPointColor", "MapTransparency")


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


def flags(vp):
    return tuple(bool(getattr(vp, f)) for f in FLAGS)


def build(path):
    doc = FreeCAD.newDocument("BinderMap")
    box = doc.addObject("Part::Box", "Box")
    binder = doc.addObject("Part::SubShapeBinder", "Binder")
    binder.Support = [(box, ("",))]
    doc.recompute()
    wait(1)
    # Named "Binder...", it takes the binder style when attached: its own
    # colour (the datum yellow), with the face colour mapped as the user
    # file's binders had it. The object keeps that now; what the view
    # provider says of it is the same value under the older name.
    saved = flags(binder)
    doc.saveAs(path)
    FreeCAD.closeDocument(doc.Name)
    wait(0.5)
    return saved


def run():
    path = os.path.join(OUT, "binder-map.FCStd")
    saved = build(path)
    check("the binder is saved with its face colour mapped",
          saved == (True, False, False, False), dict(zip(FLAGS, saved)))
    for progressive in (False, True):
        mode = "progressive" if progressive else "eager"
        RENDER.SetBool("ProgressiveLoad", progressive)
        doc = FreeCAD.openDocument(path)
        while FreeCADGui.isBuildingVisuals():
            QtCore.QCoreApplication.processEvents()
        wait(1)
        obj = doc.getObject("Binder")
        vp = obj.ViewObject
        got = flags(vp)
        check("%s open: the binder keeps its saved Map*Color" % mode,
              got == saved and flags(obj) == saved,
              "view provider %s, object %s" % (dict(zip(FLAGS, got)),
                                              dict(zip(FLAGS, flags(obj)))))
        def near(color, want):
            return all(abs(a - b) < 0.01 for a, b in zip(color[:3], want))

        def faces():
            """The colour of each face as the object draws it"""
            ea = obj.ElementAppearance
            if ea.Faces.Count == 0:
                return [ea.Face.DiffuseColor]
            if ea.Faces.Count == 1:
                return [ea.Faces.Base.DiffuseColor]
            return [ea.Faces[i].DiffuseColor for i in range(len(obj.Shape.Faces))]

        def drawn_in(want):
            return all(near(c, want) for c in faces())

        if progressive:
            picked = (0.9, 0.9, 0.1)
            vp.ShapeColor = picked
            wait(0.2)
            check("a colour set after the open still turns its mapping off",
                  not vp.MapFaceColor and not obj.MapFaceColor,
                  "view provider %s, object %s" % (vp.MapFaceColor, obj.MapFaceColor))
        else:
            # By the object's name, which is what a script with no view and
            # the property editor's Data tab write (sec 33.16)
            picked = (0.1, 0.9, 0.1)
            obj.ShapeColor = picked
            wait(0.2)
            check("a colour set by the object's name turns its mapping off as well",
                  not vp.MapFaceColor and not obj.MapFaceColor,
                  "view provider %s, object %s" % (vp.MapFaceColor, obj.MapFaceColor))
        check("%s open: and the binder is the colour picked, by both names and drawn" % mode,
              near(vp.ShapeColor, picked) and near(obj.ShapeColor, picked) and drawn_in(picked),
              "view provider %s, object %s, drawn %s"
              % (vp.ShapeColor[:3], obj.ShapeColor[:3],
                 sorted({tuple(round(v, 2) for v in c[:3]) for c in faces()})))
        check("%s open: the other mappings are as they were" % mode,
              flags(obj)[1:] == saved[1:] and flags(vp)[1:] == saved[1:],
              "view provider %s, object %s" % (flags(vp), flags(obj)))
        FreeCAD.closeDocument(doc.Name)
        wait(0.5)


def main():
    try:
        run()
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        RENDER.SetBool("ProgressiveLoad", True)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, main)
