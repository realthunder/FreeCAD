"""Bundled-effects showcase (docs/RenderEngine.md §5.11).

Builds a document exercising every shipped effect package as
STANDALONE shader objects on their built-in demo geometry
(rendereffects.instantiate + Demo/DemoPlacement — no proxy solids):
water (+ WaterSpray) on a demo box pool, a fountain (+ Droplets)
demo box rising from it so the splash rings engage, fire (+ Embers)
on a demo cylinder, and rain as an Emitter demo volume over the whole
scene. The only real solid is the terrace slab the effects need as a
shadow/light receiver. Saves the document and optionally captures a
screenshot.

The effect programs are copied into the document on activation, so the
saved .FCStd is self-contained. View-level activation state does NOT
persist with the document: after reopening, switch the view to the
Shadow draw style and set Render_Volumetric / Render_WaterSurface on
the view (or just re-run this script, which sets them).

Usage: FreeCAD scripts/demo-effects.py            (GUI or xvfb)
Env:   FX_DOC   save path (default ~/effects-showcase.FCStd)
       FX_SHOT  screenshot path (optional, saveRenderDump)
       FX_EXIT  "1" = exit after save/shot (for scripted runs)
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

# Must run before the first 3D view exists.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
    "RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
    "Type", "bgfx - OpenGL")

DOC = os.environ.get("FX_DOC",
                     os.path.join(os.path.expanduser("~"),
                                  "effects-showcase.FCStd"))
SHOT = os.environ.get("FX_SHOT", "")
EXIT = os.environ.get("FX_EXIT", "") == "1"


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


def build():
    try:
        from freecad import rendereffects

        doc = FreeCAD.newDocument("EffectsShowcase")

        # The one real solid: the terrace the effects sit on — shadow,
        # water-absorption backdrop and fire-light receiver.
        floor = doc.addObject("Part::Box", "Terrace")
        floor.Length = 44
        floor.Width = 44
        floor.Height = 2
        floor.Placement.Base = FreeCAD.Vector(-22, -22, -2)
        floor.ViewObject.ShapeColor = (0.55, 0.53, 0.50)
        doc.recompute()

        view = FreeCADGui.ActiveDocument.ActiveView
        pump()
        FreeCADGui.runCommand("Std_DrawStyleShadow", 0)
        view.Render_Volumetric = True

        # Water pool: demo box, top face = the water surface (z = 3);
        # DemoColor is the body color the absorption pulls toward.
        water = rendereffects.instantiate("water")
        water.Demo = "Box"
        water.DemoSize = FreeCAD.Vector(16, 16, 3)
        water.DemoPlacement = place(-10, -10, 1.5)
        water.DemoColor = (0.15, 0.35, 0.55)
        doc.getObject("water_WaterSpray").Enabled = True

        # Fountain plume rising from the pool water surface, so the
        # spray feeds splash rings on the water below it.
        fountain = rendereffects.instantiate("fountain")
        fountain.Demo = "Box"
        fountain.DemoSize = FreeCAD.Vector(3, 3, 8)
        fountain.DemoPlacement = place(-10, -10, 7)
        doc.getObject("fountain_Droplets").Enabled = True

        # Fire column on the dry side.
        fire = rendereffects.instantiate("fire")
        fire.Demo = "Cylinder"
        fire.DemoRadius = 3
        fire.DemoHeight = 9
        fire.DemoPlacement = place(11, 9, 4.5)
        doc.getObject("fire_Embers").Enabled = True

        # Fit the camera BEFORE the rain volume exists: its emitter
        # travel bounds would blow the fit far out.
        doc.recompute()
        view.viewIsometric()
        view.fitAll()

        # Rain over the whole scene: an Emitter demo volume — seeds
        # spread through the tall box, streaks fall and wrap inside it.
        rain = rendereffects.instantiate("rain")
        rain.Demo = "Emitter"
        rain.DemoSize = FreeCAD.Vector(44, 44, 24)
        rain.EmitterCount = 700
        rain.EmitterSeed = 5
        rain.DemoPlacement = place(0, 0, 12)

        doc.recompute()
        settle()
        settle()

        doc.saveAs(DOC)
        FreeCAD.Console.PrintMessage("showcase saved: %s\n" % DOC)
        if SHOT:
            view.saveRenderDump(SHOT)
            FreeCAD.Console.PrintMessage("showcase shot: %s\n" % SHOT)
    except Exception:
        traceback.print_exc()
        FreeCAD.Console.PrintError("showcase FAILED\n")
    finally:
        if EXIT:
            os._exit(0)


QTimer.singleShot(1000, build)
