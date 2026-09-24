"""The Sketcher's grid display preferences reach the grid.

The grid is drawn by `PartGui::ViewProviderGridExtension`, which takes its
look -- line pattern, width and colour for the minor and major lines, the
number of subdivisions, the auto-spacing pixel threshold -- from setters,
and reads no preference itself. Upstream calls those setters from
`ViewProviderSketch`'s ParameterObserver. This fork's ViewProviderSketch
has no ParameterObserver, and the 2023 merge that brought the extension
in (68326945dd) kept the extension and dropped the calls. Nothing else
calls them, so the whole "Grid display" preference page wrote values no
code read: every sketch drew the compiled-in defaults.

Two claims about the wiring:

  - the preferences are applied when an edit starts;
  - a preference changed DURING an edit is applied to that edit's grid,
    the way the other Sketcher display preferences are.

And the defaults a new sketch gets with nothing stored, which upstream
03bc80c060 and a357868691 changed together: the grid on, solid, and
drawn 60% transparent so solid lines stay quiet. Auto spacing is on as
well, as the preference page has always said it was.

Read from the Coin nodes the extension builds, not from a pixel: a
pattern, a width and a colour are exact there.

Scored against the tree before the fix: seven of the nine checks fail,
with the grid at the compiled-in 0x0f0f / width 1 / grey. "Both drawn"
passes there because the compiled-in 10 subdivisions draw both parts
too; the count that tells is 1, which is why it is set live, and the
live pattern check shows nothing reached the grid mid-edit either.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "GridPrefs"
PARAM = "User parameter:BaseApp/Preferences/Mod/Sketcher/General"
state = {"done": False}

# Deliberately none of them a default, so a grid drawn from the
# compiled-in values fails every check.
PATTERN = 0x3333
DIV_PATTERN = 0x5555
WIDTH = 3
DIV_WIDTH = 4
RED = 0xFF0000FF       # RGBA, alpha is opacity
BLUE = 0x0000FFFF
LIVE_PATTERN = 0x00FF
TRANSPARENCY = 25      # percent


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def grid_parts():
    """[(linePattern, lineWidth, (r, g, b), transparency)] per grid part,
    minor first. transparency is None where the part has no SoMaterial."""
    sa = coin.SoSearchAction()
    sa.setName("GridRoot")
    sa.setInterest(coin.SoSearchAction.FIRST)
    sa.setSearchingAll(True)
    view = FreeCADGui.activeDocument().activeView()
    path = None
    for graph in (view.getSceneGraph(), view.getAuxSceneGraph()):
        if graph is None:
            continue
        sa.apply(graph)
        path = sa.getPath()
        if path is not None:
            break
    if path is None:
        return None
    root = path.getTail()
    parts = []
    for i in range(root.getNumChildren()):
        group = root.getChild(i)
        pattern = width = rgb = alpha = None
        for j in range(group.getNumChildren()):
            node = group.getChild(j)
            if node.isOfType(coin.SoDrawStyle.getClassTypeId()):
                pattern = node.linePattern.getValue()
                width = node.lineWidth.getValue()
            elif node.isOfType(coin.SoBaseColor.getClassTypeId()):
                rgb = tuple(node.rgb[0].getValue())
            elif node.isOfType(coin.SoMaterial.getClassTypeId()):
                rgb = tuple(node.diffuseColor[0].getValue())
                alpha = node.transparency[0]
        parts.append((pattern, width, rgb, alpha))
    return parts


def near(rgb, want):
    return rgb is not None and all(abs(a - b) < 0.01 for a, b in zip(rgb, want))


INTS = ("GridLinePattern", "GridDivLinePattern", "GridLineWidth",
        "GridDivLineWidth", "GridNumberSubdivision", "GridSizePixelThreshold",
        "GridTransparency")
UNSIGNEDS = ("GridLineColor", "GridDivLineColor")
BOOLS = ("ShowGrid", "GridAuto")


def forget(hgrp):
    """The test's configuration directory outlives a run, so what the last
    run stored would otherwise be read here as a default."""
    for k in INTS:
        hgrp.RemInt(k)
    for k in UNSIGNEDS:
        hgrp.RemUnsigned(k)
    for k in BOOLS:
        hgrp.RemBool(k)


def defaults():
    """A new sketch with no grid preference stored: grid on, auto spacing on, solid minor
    lines drawn 60% transparent (upstream 03bc80c060, a357868691)."""
    doc = FreeCAD.newDocument(DOC + "Defaults")
    sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
    doc.recompute()
    vp = sketch.ViewObject
    check("a new sketch shows its grid by default", vp.ShowGrid, vp.ShowGrid)
    check("a new sketch spaces its grid automatically by default",
          vp.GridAuto, vp.GridAuto)
    FreeCADGui.activeDocument().setEdit(sketch)
    QtCore.QCoreApplication.processEvents()
    parts = grid_parts() or [(None, None, None, None)]
    note("default grid parts: %s" % (parts,))
    check("the default minor line pattern is solid",
          parts[0][0] == 0xFFFF, "0x%x" % (parts[0][0] or 0))
    check("the default grid transparency is 60%",
          parts[0][3] is not None and abs(parts[0][3] - 0.6) < 0.01, parts[0][3])
    FreeCADGui.activeDocument().resetEdit()
    QtCore.QCoreApplication.processEvents()
    FreeCAD.closeDocument(doc.Name)


def run():
    hgrp = FreeCAD.ParamGet(PARAM)
    try:
        forget(hgrp)
        defaults()
        hgrp.SetInt("GridLinePattern", PATTERN)
        hgrp.SetInt("GridDivLinePattern", DIV_PATTERN)
        hgrp.SetInt("GridLineWidth", WIDTH)
        hgrp.SetInt("GridDivLineWidth", DIV_WIDTH)
        hgrp.SetUnsigned("GridLineColor", RED)
        hgrp.SetUnsigned("GridDivLineColor", BLUE)
        hgrp.SetInt("GridNumberSubdivision", 5)
        hgrp.SetInt("GridTransparency", TRANSPARENCY)

        doc = FreeCAD.newDocument(DOC)
        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        doc.recompute()
        vp = sketch.ViewObject
        vp.ShowGrid = True

        FreeCADGui.activeDocument().setEdit(sketch)
        QtCore.QCoreApplication.processEvents()

        parts = grid_parts()
        note("grid parts at edit start: %s" % (parts,))
        if not parts:
            check("the grid is drawn", False, parts)
            finish()
            return
        minor = parts[0]
        check("minor line pattern follows GridLinePattern",
              minor[0] == PATTERN, "0x%x" % (minor[0] or 0))
        check("minor line width follows GridLineWidth",
              minor[1] == WIDTH, minor[1])
        check("minor line colour follows GridLineColor",
              near(minor[2], (1, 0, 0)), minor[2])
        check("grid transparency follows GridTransparency",
              all(p[3] is not None and abs(p[3] - TRANSPARENCY / 100.0) < 0.01
                  for p in parts), [p[3] for p in parts])
        # Both parts at 5 subdivisions, as at the default 10. The count
        # that tells is 1 -- a single part -- and it is checked live below.
        check("minor and major lines are both drawn", len(parts) == 2, len(parts))
        if len(parts) == 2:
            major = parts[1]
            check("major line pattern follows GridDivLinePattern",
                  major[0] == DIV_PATTERN, "0x%x" % (major[0] or 0))
            check("major line width follows GridDivLineWidth",
                  major[1] == DIV_WIDTH, major[1])
            check("major line colour follows GridDivLineColor",
                  near(major[2], (0, 0, 1)), major[2])

        # During the edit: the preference page's Apply lands here.
        hgrp.SetInt("GridLinePattern", LIVE_PATTERN)
        QtCore.QCoreApplication.processEvents()
        parts = grid_parts() or [(None, None, None)]
        note("grid parts after a live pattern change: %s" % (parts,))
        check("a pattern changed during the edit reaches the grid",
              parts[0][0] == LIVE_PATTERN, "0x%x" % (parts[0][0] or 0))

        # One subdivision means no major lines: a single part.
        hgrp.SetInt("GridNumberSubdivision", 1)
        QtCore.QCoreApplication.processEvents()
        parts = grid_parts() or []
        note("grid parts after GridNumberSubdivision = 1: %s" % (parts,))
        check("GridNumberSubdivision = 1 leaves only the minor lines",
              len(parts) == 1, len(parts))

        FreeCADGui.activeDocument().resetEdit()
        QtCore.QCoreApplication.processEvents()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    forget(hgrp)
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
