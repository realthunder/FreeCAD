"""Many-object scene with a *distinct* mesh per object, for exercising the
scene stream itself rather than what a publish costs per object
(docs/SceneStreaming.md §6, §11).

`demo-many.py` is a grid of identical boxes, so its geometry deduplicates
to a handful of content keys. That is the right scene for measuring the
per-object cost of a publish — whatever is left scales with the object
count and nothing else — but it is a weak test of streaming: only a few
mesh chunks ever arrive, so batched fetch, the fidelity ladder and
eviction are barely touched.

Here every object is an ellipsoid with its own radii, drawn from a seeded
sequence at a granularity fine enough that no two tessellations agree.
The scene therefore has as many mesh chunks as it has objects, and it
arrives over many batches. Keep both scenes: they measure different
things (root cost vs. chunk traffic).

Env knobs: COUNT (objects, default 200 — each one is a real mesh, so
this is slower to build than the box grid), COLORS (distinct appearances,
default 8), SPACING, SEED.

Also defines touch_one()/touch_many(n) in the document's namespace, so a
driver (the MCP console) can repaint objects and watch what the next
publish costs.

Runs persistently (no auto-close) so it can be streamed.
"""
import os
import random
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

    COUNT = int(os.environ.get("COUNT", "200"))
    COLORS = int(os.environ.get("COLORS", "8"))
    SPACING = float(os.environ.get("SPACING", "6"))
    SEED = int(os.environ.get("SEED", "20260729"))

    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)                 # renderer (bgfx) path
    view.SetBool("ShowNaviCube", True)
    view.SetBool("CornerCoordSystem", True)
    view.SetBool("ShowFPS", False)

    render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    render.SetString("Type", "bgfx - OpenGL")
    # Everything expensive off: what is under test is the stream, and an
    # effect that costs frames costs nothing in the payload.
    render.SetBool("AO", False)
    render.SetBool("Volumetric", False)
    render.SetBool("WaterSurface", False)
    render.SetBool("Bloom", False)
    render.SetBool("GroundReflection", False)
    render.SetBool("Caustics", False)
    render.SetBool("PBR", False)

    doc = FreeCAD.newDocument("VariedObjects")

    rng = random.Random(SEED)
    side = int(COUNT ** 0.5) + 1
    shapes = []
    for i in range(COUNT):
        ball = doc.addObject("Part::Ellipsoid", "E%d" % i)
        # Radii to four decimals from a seeded sequence: far finer than
        # the tessellation tolerance, so every object produces mesh bytes
        # no other object produces and nothing deduplicates. A cyclic
        # pattern (i % k) would silently collapse back to k chunks.
        ball.Radius1 = round(rng.uniform(1.2, 2.6), 4)
        ball.Radius2 = round(rng.uniform(1.2, 2.6), 4)
        ball.Radius3 = round(rng.uniform(1.2, 2.6), 4)
        ball.Placement.Base = FreeCAD.Vector((i % side) * SPACING,
                                             (i // side) * SPACING, 0)
        # A handful of appearances shared across the field, as in
        # demo-many: materials are not what this scene is measuring, and
        # they still have to prove they deduplicate. Set before the
        # recompute — colouring a live view provider rebuilds its render
        # cache, and doing that once per object dominates the build.
        shade = (i % COLORS) / float(max(COLORS - 1, 1))
        ball.ViewObject.ShapeColor = (shade, 0.4, 1.0 - shade)
        shapes.append(ball)
    doc.recompute()

    note("BUILT %d objects" % COUNT)

    _touched = [0]

    def touch_many(n=1):
        """Repaint n objects and let the next publish show what that cost."""
        for _ in range(n):
            obj = shapes[_touched[0] % len(shapes)]
            _touched[0] += 1
            r, g, b = obj.ViewObject.ShapeColor[:3]
            obj.ViewObject.ShapeColor = (g, b, r)
        FreeCADGui.updateGui()
        return _touched[0]

    def touch_one():
        return touch_many(1)

    def reshape_many(n=1):
        """Resize n objects: a *geometry* delta, so the next publish has
        to carry new mesh chunks rather than only new materials."""
        for _ in range(n):
            obj = shapes[_touched[0] % len(shapes)]
            _touched[0] += 1
            obj.Radius1 = round(rng.uniform(1.2, 2.6), 4)
        doc.recompute()
        FreeCADGui.updateGui()
        return _touched[0]

    # Reachable from the MCP console (scripts/mcp-console.py), which
    # execs into this module's namespace.
    FreeCAD.touch_many = touch_many
    FreeCAD.touch_one = touch_one
    FreeCAD.reshape_many = reshape_many

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
