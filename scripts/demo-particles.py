"""Stateful-particle showcase (docs/RenderEngine.md §5.8).

Puts the stateful tier next to the stateless one so the difference is
visible rather than asserted:

- Two `sparks` emitters (stateful): embers launched off a plinth, pulled
  back by gravity against drag, bouncing off the emitter floor and
  resting once they have spent their energy. Parameterised differently
  — one tall and lively, one heavy and low — from the same package, to
  show the step program is tuned, not rewritten.
- One `fountain` with its stateless Droplets companion: a closed-form
  ballistic arc on the shared clock. It cannot bounce, and next to the
  sparks that is the whole point.

Three stateful emitters per view is the renderer's slot budget (§5.8);
this scene uses two, leaving one spare so a bound effect added while
looking around still simulates.

Usage: FreeCAD scripts/demo-particles.py           (GUI or xvfb)
       FC_BGFX_SERVE_SCENE=<port> FreeCAD scripts/demo-particles.py
Env:   PX_DOC   save path (default: do not save)
       PX_SHOT  screenshot path (optional, saveRenderDump)
       PX_EXIT  "1" = exit after save/shot (for scripted runs)
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

# Must run before the first 3D view exists.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
    "RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
    "Type", "bgfx - OpenGL")

DOC = os.environ.get("PX_DOC", "")
SHOT = os.environ.get("PX_SHOT", "")
EXIT = os.environ.get("PX_EXIT", "") == "1"


def pump(n=10):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()


def settle(rounds=30):
    for _ in range(rounds):
        time.sleep(0.2)
        pump(3)


def place(x, y, z):
    return FreeCAD.Placement(FreeCAD.Vector(x, y, z), FreeCAD.Rotation())


def plinth(doc, name, x, y, size, height, color):
    """A real solid under an emitter — the sparks need something to
    look like they are coming off, and the scene needs a shadow and
    light receiver that is not a particle."""
    box = doc.addObject("Part::Box", name)
    box.Length = size
    box.Width = size
    box.Height = height
    box.Placement.Base = FreeCAD.Vector(x - size / 2.0, y - size / 2.0, 0)
    box.ViewObject.ShapeColor = color
    return box


def build():
    try:
        from freecad import rendereffects

        doc = FreeCAD.newDocument("ParticleShowcase")

        floor = doc.addObject("Part::Box", "Terrace")
        floor.Length = 52
        floor.Width = 52
        floor.Height = 2
        floor.Placement.Base = FreeCAD.Vector(-26, -26, -2)
        floor.ViewObject.ShapeColor = (0.42, 0.41, 0.40)

        plinth(doc, "PlinthA", -13, 0, 6, 5, (0.30, 0.30, 0.33))
        plinth(doc, "PlinthB", 6, 8, 5, 3, (0.30, 0.30, 0.33))
        doc.recompute()

        view = FreeCADGui.ActiveDocument.ActiveView
        pump()
        FreeCADGui.runCommand("Std_DrawStyleShadow", 0)
        view.Render_Volumetric = True

        # --- stateful: tall lively sparks off the left plinth ---
        tall = rendereffects.instantiate("sparks")
        tall.Label = "SparksTall"
        # Demo=Emitter: the seed quads ARE the shape, so no preview
        # solid competes with the plinth underneath.
        tall.Demo = "Emitter"
        tall.DemoSize = FreeCAD.Vector(7, 7, 14)
        tall.DemoPlacement = place(-13, 0, 12)
        tall.EmitterCount = 900
        tall.EmitterSeed = 3
        prog = tall.Programs[0]
        prog.Label = "SparksTall_Step"
        prog.Param_Launch = 17.0
        prog.Param_Gravity = 20.0
        prog.Param_Bounce = 0.62
        prog.Param_Size = 0.17
        prog.Param_Nozzle = FreeCAD.Vector(0.10, 0.30, 0.0)

        # --- stateful: heavy low sparks off the right plinth ---
        low = rendereffects.instantiate("sparks")
        low.Label = "SparksLow"
        low.Demo = "Emitter"
        low.DemoSize = FreeCAD.Vector(6, 6, 7)
        low.DemoPlacement = place(6, 8, 6)
        low.EmitterCount = 600
        low.EmitterSeed = 11
        prog2 = low.Programs[0]
        prog2.Label = "SparksLow_Step"
        prog2.Param_Launch = 11.0
        prog2.Param_Gravity = 30.0
        prog2.Param_Bounce = 0.38
        prog2.Param_Drag = 0.9
        prog2.Param_Size = 0.15
        prog2.Param_Nozzle = FreeCAD.Vector(0.26, 0.30, 0.0)

        # --- stateless counterpart: ballistic droplets, no bounce ---
        fountain = rendereffects.instantiate("fountain")
        fountain.Label = "FountainStateless"
        fountain.Demo = "Box"
        fountain.DemoSize = FreeCAD.Vector(3, 3, 9)
        fountain.DemoPlacement = place(12, -10, 4.5)
        for p in fountain.Programs:
            if p.EmitterCount > 0:
                p.Enabled = True

        doc.recompute()
        view.viewIsometric()
        view.fitAll()
        settle()
        settle()

        FreeCAD.Console.PrintMessage(
            "particle showcase: 2 stateful sparks emitters + 1 stateless "
            "fountain\n")
        if DOC:
            os.makedirs(os.path.dirname(DOC), exist_ok=True)
            doc.saveAs(DOC)
            FreeCAD.Console.PrintMessage("showcase saved: %s\n" % DOC)
        if SHOT:
            view.saveRenderDump(SHOT)
            FreeCAD.Console.PrintMessage("showcase shot: %s\n" % SHOT)
    except Exception:
        traceback.print_exc()
        FreeCAD.Console.PrintError("particle showcase FAILED\n")
    finally:
        if EXIT:
            os._exit(0)


QTimer.singleShot(1000, build)
