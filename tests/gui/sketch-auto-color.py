"""A sketch's edge and vertex colours follow the preferences (upstream
8def94e6f8, e260cf5c8a, 97e7b9d1f2).

AutoColor, on for a new sketch, takes LineColor and PointColor from
SketchEdgeColor and SketchVertexColor and keeps them out of the file, so a
sketch drawn on one theme is not stuck with its colours on another. A
preference change recolours every such sketch and is not a modification of
the document. Turned off, the colours are the sketch's own again and are
saved. A file from before AutoColor turns it on only where the colours were
never changed from white.

The old file is made by stripping every AutoColor entry from
GuiDocument.xml, the shared defaults block included: a value the fork's
save elided comes back from there, and must not read as an old file.
"""
import os
import re
import traceback
import zipfile

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchAutoColor"
VIEW = "User parameter:BaseApp/Preferences/View"
state = {"done": False}

RED = (1.0, 0.0, 0.0)
GREEN = (0.0, 1.0, 0.0)
BLUE = (0.0, 0.0, 1.0)
WHITE = (1.0, 1.0, 1.0)


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    import time
    end = time.monotonic() + seconds
    while True:
        QtCore.QCoreApplication.processEvents()
        if time.monotonic() >= end:
            break
        time.sleep(0.01)


def packed(rgb):
    r, g, b = (int(round(c * 255)) for c in rgb)
    return (r << 24) | (g << 16) | (b << 8) | 0xFF


def set_pref(edge, vertex):
    grp = FreeCAD.ParamGet(VIEW)
    grp.SetUnsigned("SketchEdgeColor", packed(edge))
    grp.SetUnsigned("SketchVertexColor", packed(vertex))
    settle(0.5)  # the handler is a delayed one


def same(color, rgb):
    return all(abs(a - b) < 0.01 for a, b in zip(color[:3], rgb))


def rgb(color):
    return tuple(round(c, 3) for c in color[:3])


def make_sketch(doc, name):
    import Part
    sk = doc.addObject("Sketcher::SketchObject", name)
    sk.addGeometry(Part.LineSegment(FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(10, 5, 0)), False)
    return sk


def strip_auto_color(src, dst):
    """GuiDocument.xml without any AutoColor property, as an older build wrote it.

    The reader loops over a record's properties by its Count, so each
    record that loses one says one fewer."""
    prop = re.compile(rb'\s*<Property name="AutoColor"[^>]*?(/>|>.*?</Property>)', re.S)
    block = re.compile(rb'(<Properties Count=")(\d+)(".*?</Properties>)', re.S)
    removed = [0]

    def fix(m):
        body, n = prop.subn(b"", m.group(3))
        removed[0] += n
        return m.group(1) + str(int(m.group(2)) - n).encode() + body

    with zipfile.ZipFile(src) as zin, zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as zout:
        for item in zin.infolist():
            data = zin.read(item.filename)
            if item.filename == "GuiDocument.xml":
                data = block.sub(fix, data)
            zout.writestr(item, data)
    return removed[0]


def reopen(path):
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    settle(0.5)
    doc = FreeCAD.openDocument(path)
    settle(1.0)
    return doc


def run():
    try:
        set_pref(RED, GREEN)
        doc = FreeCAD.newDocument(DOC)
        a = make_sketch(doc, "A")
        b = make_sketch(doc, "B")
        # A third sketch, so the view file shares one class default block
        # (three instances or more) and C's AutoColor is elided into it.
        make_sketch(doc, "C")
        doc.recompute()
        settle(0.5)
        va, vb = a.ViewObject, b.ViewObject

        check("a new sketch has AutoColor on", va.AutoColor is True)
        check("its edges take SketchEdgeColor", same(va.LineColor, RED), rgb(va.LineColor))
        check("its vertices take SketchVertexColor", same(va.PointColor, GREEN), rgb(va.PointColor))
        st = va.getPropertyStatus("LineColor")
        check("the automatic colour is not saved", "Transient" in st, st)

        gdoc = FreeCADGui.getDocument(DOC)
        path = os.path.join(OUT, "auto.FCStd")
        doc.saveAs(path)
        # An App save leaves the Gui document's Modified flag alone; the
        # command saves both.
        FreeCADGui.runCommand("Std_Save")
        settle(0.5)
        check("saved, the document is not modified", not gdoc.Modified)
        set_pref(BLUE, GREEN)
        check("a preference change recolours an open sketch", same(va.LineColor, BLUE),
              rgb(va.LineColor))
        check("and is not a modification of the document", not gdoc.Modified)

        # B's colours made its own.
        vb.AutoColor = False
        vb.LineColor = (1.0, 0.5, 0.0)
        st = vb.getPropertyStatus("LineColor")
        check("turned off, the colour is saved again", "Transient" not in st, st)
        set_pref(RED, GREEN)
        check("a preference change leaves a sketch with AutoColor off alone",
              same(vb.LineColor, (1.0, 0.5, 0.0)), rgb(vb.LineColor))
        check("while one with it on follows", same(va.LineColor, RED), rgb(va.LineColor))
        doc.save()
        settle(0.3)
        with zipfile.ZipFile(path) as z:
            gui = z.read("GuiDocument.xml")
        check("the view file shares a default block", b' Defaults="' in gui)
        check("and C's AutoColor is left to it",
              gui.count(b'<Property name="AutoColor"') < 3,
              "%d AutoColor entries" % gui.count(b'<Property name="AutoColor"'))

        # Reopened under another preference.
        set_pref(BLUE, BLUE)
        doc = reopen(path)
        va, vb = doc.getObject("A").ViewObject, doc.getObject("B").ViewObject
        check("reopened, AutoColor stays on", va.AutoColor is True)
        vc = doc.getObject("C").ViewObject
        check("reopened, an AutoColor left to the default block stays on",
              vc.AutoColor is True and same(vc.LineColor, BLUE),
              "%s %s" % (vc.AutoColor, rgb(vc.LineColor)))
        check("and the colour is the preference now, not the one saved",
              same(va.LineColor, BLUE), rgb(va.LineColor))
        check("reopened, AutoColor stays off", vb.AutoColor is False)
        check("and its own colour is kept", same(vb.LineColor, (1.0, 0.5, 0.0)),
              rgb(vb.LineColor))

        # A file from before AutoColor: A saved white, B orange.
        va.AutoColor = False
        va.LineColor = WHITE
        va.PointColor = WHITE
        doc.save()
        settle(0.3)
        old = os.path.join(OUT, "old.FCStd")
        removed = strip_auto_color(path, old)
        check("the old file has no AutoColor left", removed >= 2, "%d removed" % removed)
        set_pref(GREEN, RED)
        doc = reopen(old)
        va, vb = doc.getObject("A").ViewObject, doc.getObject("B").ViewObject
        check("an old sketch still white turns AutoColor on", va.AutoColor is True)
        check("and follows the preference", same(va.LineColor, GREEN), rgb(va.LineColor))
        check("an old sketch coloured by hand keeps AutoColor off", vb.AutoColor is False)
        check("and keeps its colour", same(vb.LineColor, (1.0, 0.5, 0.0)), rgb(vb.LineColor))

        # Turned on, the colours follow at once.
        vb.AutoColor = True
        check("turning AutoColor on applies the preference at once",
              same(vb.LineColor, GREEN) and same(vb.PointColor, RED),
              "%s %s" % (rgb(vb.LineColor), rgb(vb.PointColor)))
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
