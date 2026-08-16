"""A scene that publishes objects the producer holds only part of --
the element contract's "late companion" case (docs/SceneStreaming.md
#13b), reproduced on purpose so the streaming tier can be watched
handling it.

The lever is the vertex capture budget: a publish that has spent it
keeps the shapes it has not reached out of the frame and marks their
objects incomplete (SoFCRenderCacheManager, "the capture budget"), and
that mark is what SceneDump v55 carries to a viewer. Set the budget to
1ms and every publish reaches a handful of shapes, so the scene arrives
over many publishes with most objects marked -- the load storm a big
model produces, at a size that fits on a laptop and drains in seconds.

Two scenes, and they answer different halves:

  the synthetic grid (default) proves the MARK TRAVELS. Every object
  publishes all three drawable classes together, so no companion is
  ever absent and the dependency rule has nothing to hold -- which is
  the correct answer, and the reason this scene cannot be the whole
  test.

  MODEL=<path.FCStd> serves a real document, opened OPEN_DELAY seconds
  after the backend starts, so a viewer can be attached BEFORE the load
  begins. A load is where worker-built caches are adopted (an adoption
  is never deferred) while the face sets around them are, which is the
  ordering that puts a point or edge set on the wire ahead of its
  faces -- the case the rule exists for.

What to look for in the viewer's console (?leveldebug&shapevertices=1):

  fcviewer: apply snapshot: ... N objects incomplete
      the mark travelled. Zero throughout means the wire lost it.
  render levels: element dependency 0 -> N draws held for a late
  companion
      the rule acted on it: point or edge draws whose companion class
      the producer had not published yet, held back rather than drawn
      alone.

Env knobs: COUNT (objects, default 240), BUDGET (capture budget ms,
default 1), SPACING, RESTORM (seconds between re-storms, default 0 =
once; the storm drains in a few seconds, which is too short to attach a
viewer to by hand), MODEL, OPEN_DELAY.

Runs persistently so it can be streamed.
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

    COUNT = int(os.environ.get("COUNT", "240"))
    BUDGET = int(os.environ.get("BUDGET", "1"))
    SPACING = float(os.environ.get("SPACING", "6"))
    RESTORM = int(os.environ.get("RESTORM", "0"))
    MODEL = os.path.expanduser(os.environ.get("MODEL", ""))
    OPEN_DELAY = int(os.environ.get("OPEN_DELAY", "25"))

    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)                 # renderer (bgfx) path
    view.SetBool("ShowNaviCube", False)
    view.SetBool("ShowFPS", False)

    render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    render.SetString("Type", "bgfx - OpenGL")
    # The whole point of the scene: a budget this small is spent on the
    # first shapes of every publish, so the rest defer and their objects
    # go out marked.
    render.SetInt("CaptureBudgetMS", BUDGET)
    # Both halves of the contract on, so a held draw is held by the
    # dependency rule and not by a parameter that would have dropped it
    # anyway. The viewer needs &shapevertices=1 to match: its own vertex
    # default is off, and a point dropped for that reason tells us
    # nothing about companions.
    render.SetBool("ShapeVertices", True)
    render.SetBool("PressureDropEdges", True)
    render.SetBool("LevelDebug", True)
    # Nothing expensive: this scene is about what the publish says, not
    # about what a frame costs.
    render.SetBool("AO", False)
    render.SetBool("Volumetric", False)
    render.SetBool("WaterSurface", False)
    render.SetBool("Bloom", False)
    render.SetBool("GroundReflection", False)
    render.SetBool("PBR", False)

    _state = {"touched": 0, "timers": []}

    def open_model():
        note("OPENING %s" % MODEL)
        FreeCAD.open(MODEL)
        FreeCADGui.updateGui()
        note("OPENED %s" % MODEL)

    def build_grid():
        doc = FreeCAD.newDocument("Incomplete")
        # Cylinders rather than boxes: a curved face tessellates into
        # enough triangles that capturing one costs real time, which is
        # what the budget is measured against. Every object carries all
        # three drawable classes (faces, the edges that bound them, the
        # vertices those edges end at).
        side = int(COUNT ** 0.5) + 1
        parts = []
        for i in range(COUNT):
            cyl = doc.addObject("Part::Cylinder", "C%d" % i)
            cyl.Radius, cyl.Height = 2, 5
            cyl.Angle = 300            # an open sweep: more edges, more ends
            cyl.Placement.Base = FreeCAD.Vector((i % side) * SPACING,
                                                (i // side) * SPACING, 0)
            shade = (i % 8) / 7.0
            cyl.ViewObject.ShapeColor = (shade, 0.4, 1.0 - shade)
            parts.append(cyl)
        doc.recompute()
        note("BUILT %d objects, capture budget %dms" % (COUNT, BUDGET))
        return parts

    parts = [] if MODEL else build_grid()

    def touch_many(n=1):
        """Repaint n objects: their caches are dropped and re-captured
        against the same tiny budget, so the storm can be re-run without
        restarting the backend."""
        for _ in range(n):
            part = parts[_state["touched"] % len(parts)]
            _state["touched"] += 1
            r, g, b = part.ViewObject.ShapeColor[:3]
            part.ViewObject.ShapeColor = (g, b, r)
        FreeCADGui.updateGui()
        return _state["touched"]

    def touch_all():
        return touch_many(len(parts)) if parts else 0

    # Reachable from the MCP console (scripts/mcp-console.py).
    FreeCAD.touch_many = touch_many
    FreeCAD.touch_all = touch_all

    def setup_view():
        try:
            if MODEL:
                # Nothing to fit yet -- the document arrives later, and
                # the viewer is meant to be attached before it does.
                QtCore.QTimer.singleShot(OPEN_DELAY * 1000, open_model)
                note("SETUP OK - serving empty, opening %s in %ds"
                     % (MODEL, OPEN_DELAY))
                return
            FreeCADGui.activeDocument().activeView()
            FreeCADGui.SendMsgToActiveView("ViewFit")
            note("SETUP OK - serving %d objects" % COUNT)
            if RESTORM > 0:
                timer = QtCore.QTimer()
                timer.timeout.connect(touch_all)
                timer.start(RESTORM * 1000)
                _state["timers"].append(timer)   # unheld timers are collected
                note("RESTORM every %ds" % RESTORM)
        except Exception:
            note(traceback.format_exc())

    QtCore.QTimer.singleShot(2500, setup_view)
except Exception:
    note(traceback.format_exc())
