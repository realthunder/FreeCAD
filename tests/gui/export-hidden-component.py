"""A hidden child of an assembly exports to STEP, hidden and in its own colour.

ImportGui.export of an App::Part whose child had Visibility off never
returned: OCCT's STEP writer (MakeSTEPStyles) dereferences a null override
item for an invisible component that has no colour of its own -- occ-issues
local05. ExportOCAF2 has worked around it since OCCT 7 by colouring a hidden
component, but checked with GetInstanceColor(), which falls back to the
referred shape's colour; the GUI exporter colours every shape, so the
workaround never ran there. On Windows the fault vanished where it crossed
the window procedure and the export just never came back; elsewhere it is a
crash. Now ExportOCAF2::setInvisible() looks at the component's own colour
and copies the referred shape's onto it -- the colour the instance shows.

What is asserted, each exported through ImportGui.export and read back
through ImportGui.insert:

  - App::Part of Box1 + Box2, Box2 hidden: the export returns, the file
    carries INVISIBILITY, Box2 comes back hidden and Box1 shown;
  - the same with Box2 red: Box2 comes back red, and the file styles the
    hidden instance red too -- no PRE_DEFINED_COLOUR('white'). On a kernel
    with local05 fixed but without setInvisible(), the export returns and
    the instance gets the writer's default white; FreeCAD's reader takes
    the shape's colour for it and shows red anyway, so only the file can
    tell;
  - Box2 hidden one level down, in an App::Part beside a shown Box3;
  - a collapsed link array (ShowElement off) with "1.!hide": the element
    hidden by a colour mark, not by Visibility, takes the same path.

Run by hand as `FreeCAD <this script>` with GT_OUT set (or through
scripts/gui-test.sh).
"""
import os
import tempfile
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))

state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def export(tag, obj, tmp):
    import ImportGui

    path = os.path.join(tmp, tag + ".step")
    # Noted first: a fault swallowed at the window procedure leaves no
    # exception behind, only this line without its answer.
    note("export " + tag)
    ImportGui.export([obj], path)
    check(tag + ": export returned", os.path.exists(path))
    with open(path, encoding="latin-1") as f:
        text = f.read()
    check(tag + ": INVISIBILITY written", "INVISIBILITY(" in text)
    return path, text


def read_back(path):
    """{label: (visible, colour)} of every shape the file brings back."""
    import ImportGui

    doc = FreeCAD.newDocument("ExportHiddenBack")
    ImportGui.insert(path, doc.Name)
    doc.recompute()
    res = {}
    for obj in doc.Objects:
        if obj.TypeId == "App::Part" or not hasattr(obj, "Shape"):
            continue
        res.setdefault(obj.Label, []).append(
            (bool(obj.Visibility), tuple(round(c, 2) for c in obj.ViewObject.ShapeColor[:3]))
        )
    FreeCAD.closeDocument(doc.Name)
    return res


def part_doc(nested=False, colour=None):
    doc = FreeCAD.newDocument("ExportHidden")
    part = doc.addObject("App::Part", "Asm")
    box1 = doc.addObject("Part::Box", "Box1")
    box2 = doc.addObject("Part::Box", "Box2")
    box2.Placement.Base = FreeCAD.Vector(20, 0, 0)
    part.addObject(box1)
    if nested:
        inner = doc.addObject("App::Part", "Inner")
        box3 = doc.addObject("Part::Box", "Box3")
        box3.Placement.Base = FreeCAD.Vector(40, 0, 0)
        part.addObject(inner)
        inner.addObject(box2)
        inner.addObject(box3)
    else:
        part.addObject(box2)
    doc.recompute()
    if colour:
        box2.ViewObject.ShapeColor = colour
    box2.Visibility = False
    doc.recompute()
    return doc, part


def finish():
    if state["done"]:
        return
    state["done"] = True
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0
        )
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Import").SetBool(
            "ExportHiddenObject", True
        )
        tmp = tempfile.mkdtemp(prefix="fc_export_hidden_")

        doc, part = part_doc()
        back = read_back(export("plain", part, tmp)[0])
        check("plain: Box1 shown", [v for v, _ in back.get("Box1", [])] == [True], back)
        check("plain: Box2 hidden", [v for v, _ in back.get("Box2", [])] == [False], back)
        FreeCAD.closeDocument(doc.Name)

        doc, part = part_doc(colour=(1.0, 0.0, 0.0))
        path, text = export("red", part, tmp)
        check(
            "red: no default white for the hidden instance",
            "PRE_DEFINED_COLOUR('white')" not in text,
        )
        back = read_back(path)
        check("red: Box2 hidden and red", back.get("Box2") == [(False, (1.0, 0.0, 0.0))], back)
        FreeCAD.closeDocument(doc.Name)

        doc, part = part_doc(nested=True)
        back = read_back(export("nested", part, tmp)[0])
        check("nested: Box2 hidden", [v for v, _ in back.get("Box2", [])] == [False], back)
        check("nested: Box3 shown", [v for v, _ in back.get("Box3", [])] == [True], back)
        FreeCAD.closeDocument(doc.Name)

        doc = FreeCAD.newDocument("ExportHidden")
        source = doc.addObject("Part::Box", "Source")
        array = doc.addObject("Part::LinkArrayLinear", "Array")
        array.LinkedObject = source
        array.ShowElement = False
        array.Occurrences = 3
        array.Length = 30
        doc.recompute()
        array.ViewObject.setElementColors({"1.!hide": (0.0, 0.0, 0.0, 0.0)})
        doc.recompute()
        back = read_back(export("array", array, tmp)[0])
        check(
            "array: one of three instances hidden",
            sorted(v for v, _ in back.get("Source", [])) == [False, True, True],
            back,
        )
        FreeCAD.closeDocument(doc.Name)
    except Exception:
        note("ABORT run:\n" + traceback.format_exc())
    finish()


QtCore.QTimer.singleShot(1500, run)
