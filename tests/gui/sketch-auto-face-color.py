"""A sketch's face colour follows the preference, as its edges do
(upstream 8def94e6f8 family, 8a6f859a57), with the fork's own default.

Under AutoColor a sketch's internal faces take Mod/Sketcher/General/
FaceColor -- 50% transparent blue (0x54abff7f) where it was never set; the
fork's colour, not upstream's orange -- and keep it out of the file. A
preference change recolours every such sketch and is not a modification.
Turned off, the face colour is the sketch's own again and is saved.

A file from before the face joined AutoColor saved its face colour. Where
that colour is the default, or the preference of the day, the sketch goes
on following the preference; where it was set by hand, AutoColor is
turned off so the file keeps its colours -- upstream would follow the
preference there and drop the colour on the next save.

Measured on ShapeColor, Transparency and their saved status.
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
DOC = "SketchAutoFaceColor"
GENERAL = "User parameter:BaseApp/Preferences/Mod/Sketcher/General"
state = {"done": False}

DEFAULT = ((84 / 255.0, 171 / 255.0, 1.0), 50)
RED40 = ((1.0, 0.0, 0.0), 40)
GREEN20 = ((0.0, 1.0, 0.0), 20)
ORANGE = ((1.0, 0.5, 0.0), 10)


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


def set_pref(face):
    grp = FreeCAD.ParamGet(GENERAL)
    if face is None:
        grp.RemUnsigned("FaceColor")
    else:
        (r, g, b), t = face
        opacity = int(round(255 * (100 - t) / 100.0))
        grp.SetUnsigned("FaceColor", (int(round(r * 255)) << 24) | (int(round(g * 255)) << 16)
                        | (int(round(b * 255)) << 8) | opacity)
    settle(0.5)  # the handler is a delayed one


def face(vo):
    return tuple(round(c, 3) for c in vo.ShapeColor[:3]), vo.Transparency


def same(vo, want):
    (rgb, t) = face(vo)
    return all(abs(a - b) < 0.01 for a, b in zip(rgb, want[0])) and abs(t - want[1]) <= 1


def make_sketch(doc, name):
    import Part
    sk = doc.addObject("Sketcher::SketchObject", name)
    sk.addGeometry(Part.LineSegment(FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(10, 5, 0)), False)
    return sk


def rewrite_gui(src, dst, fn):
    with zipfile.ZipFile(src) as zin, zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as zout:
        for item in zin.infolist():
            data = zin.read(item.filename)
            if item.filename == "GuiDocument.xml":
                data = fn(data)
            zout.writestr(item, data)


def strip_auto_color(data):
    prop = re.compile(rb'\s*<Property name="AutoColor"[^>]*?(/>|>.*?</Property>)', re.S)
    block = re.compile(rb'(<Properties Count=")(\d+)(".*?</Properties>)', re.S)

    def fix(m):
        body, n = prop.subn(b"", m.group(3))
        return m.group(1) + str(int(m.group(2)) - n).encode() + body
    return block.sub(fix, data)


def auto_color_on(data):
    return re.sub(rb'(<Property name="AutoColor"[^>]*>\s*<Bool value=")false', rb'\1true', data)


def reopen(path):
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    settle(0.5)
    doc = FreeCAD.openDocument(path)
    settle(1.0)
    return doc


def run():
    try:
        set_pref(None)
        doc = FreeCAD.newDocument(DOC)
        d = make_sketch(doc, "D")
        doc.recompute()
        check("unset, a new sketch's face is the fork's default", same(d.ViewObject, DEFAULT),
              face(d.ViewObject))

        set_pref(RED40)
        a = make_sketch(doc, "A")
        b = make_sketch(doc, "B")
        doc.recompute()
        settle(0.5)
        va, vb = a.ViewObject, b.ViewObject
        check("a new sketch's face takes FaceColor", same(va, RED40), face(va))
        st = va.getPropertyStatus("ShapeColor")
        check("the automatic face colour is not saved", "Transient" in st, st)
        check("nor its transparency", "Transient" in va.getPropertyStatus("Transparency"))

        gdoc = FreeCADGui.getDocument(DOC)
        path = os.path.join(OUT, "auto.FCStd")
        doc.saveAs(path)
        FreeCADGui.runCommand("Std_Save")
        settle(0.5)
        set_pref(GREEN20)
        check("a preference change recolours an open sketch's face", same(va, GREEN20), face(va))
        check("an existing sketch made before the change follows too", same(d.ViewObject, GREEN20),
              face(d.ViewObject))
        check("and is not a modification of the document", not gdoc.Modified)

        vb.AutoColor = False
        vb.ShapeColor = ORANGE[0]
        vb.Transparency = ORANGE[1]
        check("turned off, the face colour is saved again",
              "Transient" not in vb.getPropertyStatus("ShapeColor"))
        set_pref(RED40)
        check("a preference change leaves a face with AutoColor off alone", same(vb, ORANGE),
              face(vb))
        doc.save()
        settle(0.3)

        set_pref(GREEN20)
        doc = reopen(path)
        va, vb = doc.getObject("A").ViewObject, doc.getObject("B").ViewObject
        check("reopened, the face is the preference now", same(va, GREEN20), face(va))
        check("reopened, AutoColor off keeps its own face colour", vb.AutoColor is False
              and same(vb, ORANGE), "%s %s" % (vb.AutoColor, face(vb)))

        # Files written before the face joined AutoColor: the face colour was
        # saved. A carries the default, B its own orange; both with white
        # edges, as AutoColor's own old-file test wants.
        va.AutoColor = False
        vb.AutoColor = False
        va.ShapeColor, va.Transparency = DEFAULT
        for vo in (va, vb):
            vo.LineColor = (1.0, 1.0, 1.0)
            vo.PointColor = (1.0, 1.0, 1.0)
        doc.save()
        settle(0.3)
        for tag, fn in (("before AutoColor", strip_auto_color),
                        ("with AutoColor on, before the face joined it", auto_color_on)):
            old = os.path.join(OUT, "old.FCStd")
            rewrite_gui(path, old, fn)
            set_pref(RED40)
            doc = reopen(old)
            va, vb = doc.getObject("A").ViewObject, doc.getObject("B").ViewObject
            check("a file %s: a default face follows the preference" % tag,
                  va.AutoColor is True and same(va, RED40), "%s %s" % (va.AutoColor, face(va)))
            check("and a face set by hand keeps its colour, AutoColor off",
                  vb.AutoColor is False and same(vb, ORANGE), "%s %s" % (vb.AutoColor, face(vb)))
            doc = reopen(path)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    FreeCAD.ParamGet(GENERAL).RemUnsigned("FaceColor")
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
