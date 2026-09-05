"""The committed render-test scene (docs/RenderDebug.md section 5.2).

A deliberately small, fully deterministic scene for the golden render
tests wired into ctest. Every value here is a literal: no environment
knobs, no randomness, no time-dependent content, nothing sized from the
wall clock. Two runs of this file must describe the same picture, or the
goldens it is blessed into are worthless.

What it covers, chosen so one cheap scene exercises the parts of the
pipeline a regression actually lands in:

  - a matte floor, so every shadow has somewhere to fall;
  - a metal sphere (Render_Metallic 1.0) for the PBR/IBL path;
  - a rough dielectric box for the ordinary shaded case;
  - a glass cylinder (Render_Glass) for refraction and Beer-Lambert
    density, which is also the one surface class the Cycles leg
    translates differently from the raster one;
  - one shadow-casting bulb, so the shadow buffer (RenderDebug_ViewMode
    4) has content rather than being uniformly empty.

It is small on purpose: the Cycles leg path traces it on the CPU, and
the whole point of a test set is that it runs.

Kept out on purpose: the NaviCube and the FPS counter (chrome adds
pixels to a capture and the counter changes every frame), autosave (it
fires mid-capture), and any effect with stateful or temporal content.
"""
import os
import traceback

import FreeCAD
import FreeCADGui

# FC_RENDER_TEST_BG=0 draws the model over a FLAT background instead of
# the viewer's gradient. Two cases, not one, because they fail
# differently: with a background most of the frame is not the model at
# all, so a change to the model moves few pixels while anything that
# moves the camera moves nearly all of them; without one, the frame is
# the model and the diff is about what is under test. Neither case
# subsumes the other -- the background is drawn by the engine too.
BACKGROUND = os.environ.get("FC_RENDER_TEST_BG", "1") != "0"

try:
    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)                 # the renderer (bgfx) path
    # Chrome and counters are pixels in a capture that have nothing to do
    # with the renderer under test.
    view.SetBool("ShowNaviCube", False)
    view.SetBool("CornerCoordSystem", False)
    view.SetBool("ShowFPS", False)
    # The floor slab is the ground; the draw style's automatic ground
    # plane would only enlarge the view-fit bounds.
    view.SetBool("ShadowShowGround", False)
    view.SetInt("ShadowSmoothBorder", 40)
    if not BACKGROUND:
        # Flat, and a stated colour: the gradient is three colours and a
        # radial flag, all of which are background pixels that have
        # nothing to do with the geometry under test.
        view.SetBool("Gradient", False)
        view.SetBool("RadialGradient", False)
        view.SetBool("UseBackgroundColorMid", False)
        view.SetUnsigned("BackgroundColor", 858993663)

    # Autosave fires on a timer and would land in the middle of a capture
    # run (docs/Testing.md).
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document") \
        .SetInt("AutoSaveTimeout", 0)

    render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    render.SetString("Type", "bgfx - OpenGL")
    render.SetFloat("LightIntensity", 0.55)
    render.SetBool("PBR", True)
    # The environment is what actually fills the background here: the PBR
    # env is drawn by default, over the viewer's gradient, so turning the
    # gradient off alone left the flat leg pixel-for-pixel the same frame
    # as the lit one (max channel delta 1). Off, the flat colour set
    # above is what the model sits on, which is the point of the leg.
    render.SetBool("PBREnvBackground", BACKGROUND)
    render.SetBool("AO", True)
    render.SetFloat("AOIntensity", 1.0)
    render.SetBool("Bloom", False)          # a halo is not what is under test
    render.SetBool("Volumetric", False)
    render.SetBool("WaterSurface", False)
    render.SetBool("SunDisc", False)
    render.SetBool("GroundReflection", False)
    render.SetBool("Caustics", False)

    doc = FreeCAD.newDocument("RenderTestScene")

    floor = doc.addObject("Part::Box", "Floor")
    floor.Length, floor.Width, floor.Height = 60, 60, 2
    floor.Placement.Base = FreeCAD.Vector(-30, -30, 0)

    box = doc.addObject("Part::Box", "RoughBox")
    box.Length, box.Width, box.Height = 10, 10, 10
    box.Placement.Base = FreeCAD.Vector(-16, -5, 2)

    ball = doc.addObject("Part::Sphere", "MetalBall")
    ball.Radius = 5
    ball.Placement.Base = FreeCAD.Vector(0, 0, 7)

    glass = doc.addObject("Part::Cylinder", "GlassRod")
    glass.Radius, glass.Height = 3.5, 14
    glass.Placement.Base = FreeCAD.Vector(15, 0, 2)

    bulb = doc.addObject("Part::Sphere", "Bulb")
    bulb.Radius = 1.2
    bulb.Placement.Base = FreeCAD.Vector(-12, -14, 22)

    doc.recompute()

    fvo = floor.ViewObject
    fvo.ShapeColor = (0.45, 0.45, 0.47)
    fvo.addProperty("App::PropertyFloat", "Render_Metallic").Render_Metallic = 0.0
    fvo.addProperty("App::PropertyFloat", "Render_Roughness").Render_Roughness = 0.9

    bvo = box.ViewObject
    bvo.ShapeColor = (0.72, 0.55, 0.35)
    bvo.addProperty("App::PropertyFloat", "Render_Metallic").Render_Metallic = 0.0
    bvo.addProperty("App::PropertyFloat", "Render_Roughness").Render_Roughness = 0.8

    svo = ball.ViewObject
    svo.ShapeColor = (0.85, 0.85, 0.88)
    svo.addProperty("App::PropertyFloat", "Render_Metallic").Render_Metallic = 1.0
    svo.addProperty("App::PropertyFloat", "Render_Roughness").Render_Roughness = 0.2

    # Glass: screen-space refraction on the raster leg, a real refractive
    # BSDF on the Cycles one. A stated density keeps Beer-Lambert out of
    # its automatic mode, which is sized from the body's bounds.
    gvo = glass.ViewObject
    gvo.ShapeColor = (0.60, 0.80, 0.75)
    gvo.addProperty("App::PropertyBool", "Render_Glass").Render_Glass = True
    gvo.addProperty("App::PropertyFloat", "Render_GlassIOR").Render_GlassIOR = 1.5
    gvo.addProperty("App::PropertyFloat", "Render_GlassRoughness").Render_GlassRoughness = 0.05
    gvo.addProperty("App::PropertyFloat", "Render_GlassDensity").Render_GlassDensity = 0.04

    lvo = bulb.ViewObject
    lvo.ShapeColor = (1.0, 0.93, 0.80)
    lvo.addProperty("App::PropertyBool", "Render_Light").Render_Light = True
    lvo.addProperty("App::PropertyFloat", "Render_LightIntensity").Render_LightIntensity = 3.0
    lvo.addProperty("App::PropertyFloat", "Render_LightRange").Render_LightRange = 0.0
    lvo.addProperty("App::PropertyBool", "Render_LightShadow").Render_LightShadow = True
    lvo.addProperty("App::PropertyBool",
                    "Render_LightShadowExtended").Render_LightShadowExtended = True

    doc.recompute()

    from PySide import QtCore

    def setup_view():
        # Re-create the backend so it picks up the parameters set above;
        # render_verify.py stages the camera itself.
        try:
            FreeCADGui.activateWorkbench("PartWorkbench")
            view.SetInt("RenderCache", 0)
            view.SetInt("RenderCache", 3)
            FreeCADGui.activeDocument().activeView().Render_Light = True
        except Exception:
            FreeCAD.Console.PrintError(traceback.format_exc())

    QtCore.QTimer.singleShot(2000, setup_view)
except Exception:
    FreeCAD.Console.PrintError(traceback.format_exc())
