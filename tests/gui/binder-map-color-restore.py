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
face colour mapped, as the user file's were. Claims, per open mode (ProgressiveLoad
off, then on): the binder's MapFaceColor, MapLineColor, MapPointColor and
MapTransparency are the saved ones. And after the opens, setting the colour still turns the
mapping off, as it did.

Scored against the tree before the fix: the eager open reads
MapFaceColor false.
"""
import os
import time
import traceback
import zipfile

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
    # colour (the datum yellow). The user file's binders were saved with the
    # face colour mapped as well, which a binder made today does not keep
    # (the style's own colour update switches it off again), so the saved
    # flag is written in as that file has it.
    doc.saveAs(path)
    FreeCAD.closeDocument(doc.Name)
    wait(0.5)
    with zipfile.ZipFile(path) as z:
        entries = {i.filename: z.read(i.filename) for i in z.infolist()}
    xml = entries["GuiDocument.xml"].decode("utf-8")
    start = xml.index('<ViewProvider name="Binder"')
    end = xml.index("</ViewProvider>", start)
    block = xml[start:end]
    at = block.index('<Property name="MapFaceColor"')
    val = block.index('value="', at) + len('value="')
    block = block[:val] + "true" + block[block.index('"', val):]
    entries["GuiDocument.xml"] = (xml[:start] + block + xml[end:]).encode("utf-8")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for n, data in entries.items():
            z.writestr(n, data)
    return (True, False, False, False)


def run():
    path = os.path.join(OUT, "binder-map.FCStd")
    saved = build(path)
    for progressive in (False, True):
        mode = "progressive" if progressive else "eager"
        RENDER.SetBool("ProgressiveLoad", progressive)
        doc = FreeCAD.openDocument(path)
        while FreeCADGui.isBuildingVisuals():
            QtCore.QCoreApplication.processEvents()
        wait(1)
        vp = doc.getObject("Binder").ViewObject
        got = flags(vp)
        check("%s open: the binder keeps its saved Map*Color" % mode,
              got == saved, dict(zip(FLAGS, got)))
        if progressive:
            vp.ShapeColor = (0.9, 0.9, 0.1)
            wait(0.2)
            check("a colour set after the open still turns its mapping off",
                  not vp.MapFaceColor, vp.MapFaceColor)
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
