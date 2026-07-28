"""Many-object scene for measuring what a publish costs per object rather
than per pixel (docs/SceneStreaming.md §11). A grid of COUNT identical
boxes: the geometry deduplicates to a handful of content keys, so
whatever the stream still spends scales with the object count and
nothing else — which is exactly what the root manifest is, and what the
delta phase has to remove.

Env knobs: COUNT (objects, default 1000), COLORS (distinct appearances,
default 8 — enough to prove materials deduplicate, few enough that they
are not the thing being measured), SPACING.

Also defines touch_one()/touch_many(n) in the document's namespace, so a
driver (the MCP console) can repaint objects and watch what the next
publish costs.

Runs persistently (no auto-close) so it can be streamed.
"""
import os
import traceback

import FreeCAD
import FreeCADGui

_out = os.environ.get("SMOKE_RESULT")


def note(msg):
    if _out:
        with open(_out, "a") as f:
            f.write(str(msg) + "\n")


try:
    from PySide6 import QtCore

    COUNT = int(os.environ.get("COUNT", "1000"))
    COLORS = int(os.environ.get("COLORS", "8"))
    SPACING = float(os.environ.get("SPACING", "6"))

    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)                 # renderer (bgfx) path
    view.SetBool("ShowNaviCube", True)
    view.SetBool("CornerCoordSystem", True)
    view.SetBool("ShowFPS", False)

    render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    render.SetString("Type", "bgfx - OpenGL")
    # Everything expensive off: this scene is a payload benchmark, and an
    # effect that costs frames costs nothing in the stream.
    render.SetBool("AO", False)
    render.SetBool("Volumetric", False)
    render.SetBool("WaterSurface", False)
    render.SetBool("Bloom", False)
    render.SetBool("GroundReflection", False)
    render.SetBool("Caustics", False)
    render.SetBool("PBR", False)

    doc = FreeCAD.newDocument("ManyObjects")

    side = int(COUNT ** 0.5) + 1
    boxes = []
    for i in range(COUNT):
        box = doc.addObject("Part::Box", "B%d" % i)
        box.Length, box.Width, box.Height = 4, 4, 4
        box.Placement.Base = FreeCAD.Vector((i % side) * SPACING,
                                            (i // side) * SPACING, 0)
        # A handful of appearances shared across the grid, so the
        # material table stays small and the per-object cost is what is
        # left. Set before the recompute: assigning a colour to a live
        # view provider rebuilds its render cache, and doing that once
        # per object after the fact dominates the build.
        shade = (i % COLORS) / float(max(COLORS - 1, 1))
        box.ViewObject.ShapeColor = (shade, 0.4, 1.0 - shade)
        boxes.append(box)
    doc.recompute()

    note("BUILT %d objects" % COUNT)

    _touched = [0]

    def touch_many(n=1):
        """Repaint n objects and let the next publish show what that cost."""
        for _ in range(n):
            box = boxes[_touched[0] % len(boxes)]
            _touched[0] += 1
            r, g, b = box.ViewObject.ShapeColor[:3]
            box.ViewObject.ShapeColor = (g, b, r)
        FreeCADGui.updateGui()
        return _touched[0]

    def touch_one():
        return touch_many(1)

    # Reachable from the MCP console (scripts/mcp-console.py), which
    # execs into this module's namespace.
    FreeCAD.touch_many = touch_many
    FreeCAD.touch_one = touch_one

    def setup_view():
        try:
            FreeCADGui.activeDocument().activeView()
            FreeCADGui.SendMsgToActiveView("ViewFit")
            note("SETUP OK - serving %d objects" % COUNT)
        except Exception:
            note(traceback.format_exc())

    QtCore.QTimer.singleShot(2500, setup_view)
except Exception:
    note(traceback.format_exc())
