"""Simple demo scene for the bgfx render-cache backend: two colored point
lights (Render_Light bulbs, one warm / one cool) casting shadows
(Render_LightShadow) from a pillar and a cross-beam onto a matte floor.
The directional scene light is dimmed (SUN env, default 0.15) so the two
bulb shadows dominate. Run it via scripts/renderer-desktop.sh or
scripts/renderer-serve.sh.

Env knobs: SUN (scene-light intensity), BULB_INTENSITY, BULB_RANGE,
BULB_SHADOW (0 disables bulb shadows), BLOOM, AO, VOL (haze light halos,
default off), SHADOWSMOOTH.

Runs persistently (no auto-close) so it can be viewed / streamed.
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
    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)                 # renderer (bgfx) path
    view.SetBool("ShowNaviCube", True)
    view.SetBool("CornerCoordSystem", True)
    view.SetBool("ShowFPS", False)   # the browser HUD shows the real (browser) fps

    # Outline-only highlight (same as demo-water.py).
    view.SetBool("ShowPreSelectedFaceOutline", True)
    view.SetBool("ShowSelectedFaceOutline", True)
    view.SetBool("NoPreSelFaceHighlightWithOutline", True)
    view.SetBool("NoSelFaceHighlightWithOutline", True)
    view.SetInt("ShadowSmoothBorder", int(os.environ.get("SHADOWSMOOTH", "40")))
    # Dim the directional scene light so the two bulbs carry the scene.
    view.SetFloat("ShadowLightIntensity", float(os.environ.get("SUN", "0.15")))
    # The floor slab is the scene's ground; the draw style's auto ground
    # plane would only blow up the view-fit bounds.
    view.SetBool("ShadowShowGround", False)

    nav = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/NaviCube")
    nav.SetBool("AutoHideButton", False)
    nav.SetBool("AutoHideCube", False)

    render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    render.SetString("Type", "bgfx - OpenGL")     # backend factory string
    render.SetBool("AO", os.environ.get("AO", "1") == "1")
    render.SetFloat("AOIntensity", float(os.environ.get("AOINT", "1.0")))
    # Haze halos around the bulbs; off by default to keep the scene simple.
    render.SetBool("Volumetric", os.environ.get("VOL", "0") == "1")
    render.SetFloat("VolumetricDensity", float(os.environ.get("VOLDENSITY", "0.0")))
    render.SetBool("WaterSurface", False)
    render.SetBool("SunDisc", False)              # no visible sun in a dim scene
    # Bloom gives the bulb bodies their glow halo.
    render.SetBool("Bloom", os.environ.get("BLOOM", "1") == "1")
    render.SetFloat("BloomIntensity", float(os.environ.get("BLOOM_INTENSITY", "1.0")))
    render.SetFloat("BloomRadius", float(os.environ.get("BLOOM_RADIUS", "1.0")))
    render.SetBool("GroundReflection", False)
    render.SetBool("Caustics", False)
    render.SetBool("PBR", os.environ.get("PBR", "1") == "1")  # image-based lighting

    doc = FreeCAD.newDocument("TwoLights")

    # Matte floor slab the shadows land on.
    floor = doc.addObject("Part::Box", "Floor")
    floor.Length, floor.Width, floor.Height = 60, 60, 2
    floor.Placement.Base = FreeCAD.Vector(-30, -30, 0)

    # Casters between the two lights: a central pillar and a low cross-beam,
    # so each light throws its own differently-angled shadow of the same
    # geometry across the floor.
    pillar = doc.addObject("Part::Cylinder", "Pillar")
    pillar.Radius, pillar.Height = 2.5, 16
    pillar.Placement.Base = FreeCAD.Vector(0, 0, 2)

    beam = doc.addObject("Part::Box", "CrossBeam")
    beam.Length, beam.Width, beam.Height = 24, 2.5, 2.5
    beam.Placement.Base = FreeCAD.Vector(-12, -1.25, 10)

    ball = doc.addObject("Part::Sphere", "Ball")
    ball.Radius = 3
    ball.Placement.Base = FreeCAD.Vector(0, 11, 5)

    # The two shadow-casting point lights, on opposite sides of the pillar:
    # a warm one and a cool one, so the overlapping shadows tint each other.
    # The warm one sits LOW (below the cross-beam) -- with the extended
    # (cube-face) shadow it shadows sideways and upward too; with the
    # plain downward cone only the floor patch beneath it darkens.
    bulbA = doc.addObject("Part::Sphere", "WarmBulb")
    bulbA.Radius = 1.2
    bulbA.Placement.Base = FreeCAD.Vector(-14, -10, 7)

    bulbB = doc.addObject("Part::Sphere", "CoolBulb")
    bulbB.Radius = 1.2
    bulbB.Placement.Base = FreeCAD.Vector(14, -8, 12)

    doc.recompute()

    fvo = floor.ViewObject
    fvo.ShapeColor = (0.45, 0.45, 0.47)
    fvo.addProperty("App::PropertyFloat", "Render_Metallic").Render_Metallic = 0.0
    fvo.addProperty("App::PropertyFloat", "Render_Roughness").Render_Roughness = 0.9

    pvo = pillar.ViewObject
    pvo.ShapeColor = (0.80, 0.80, 0.82)
    pvo.addProperty("App::PropertyFloat", "Render_Metallic").Render_Metallic = 0.1
    pvo.addProperty("App::PropertyFloat", "Render_Roughness").Render_Roughness = 0.6

    beam.ViewObject.ShapeColor = (0.72, 0.55, 0.35)

    svo = ball.ViewObject
    svo.ShapeColor = (0.85, 0.85, 0.88)
    svo.addProperty("App::PropertyFloat", "Render_Metallic").Render_Metallic = 0.9
    svo.addProperty("App::PropertyFloat", "Render_Roughness").Render_Roughness = 0.3

    def make_light(obj, color):
        lvo = obj.ViewObject
        lvo.ShapeColor = color   # emitter body color = light color
        lvo.addProperty("App::PropertyBool", "Render_Light").Render_Light = True
        lvo.addProperty("App::PropertyFloat", "Render_LightIntensity").Render_LightIntensity = \
            float(os.environ.get("BULB_INTENSITY", "1.2"))
        lvo.addProperty("App::PropertyFloat", "Render_LightRange").Render_LightRange = \
            float(os.environ.get("BULB_RANGE", "0"))
        lvo.addProperty("App::PropertyBool", "Render_LightShadow").Render_LightShadow = (
            os.environ.get("BULB_SHADOW", "1") == "1")
        lvo.addProperty("App::PropertyBool", "Render_LightShadowExtended"
                        ).Render_LightShadowExtended = (
            os.environ.get("BULB_EXT", "1") == "1")

    make_light(bulbA, (1.0, 0.72, 0.35))   # warm
    make_light(bulbB, (0.45, 0.65, 1.0))   # cool

    doc.recompute()
    note("SCENE BUILT")

    from PySide import QtCore

    def setup_view():
        try:
            FreeCADGui.activateWorkbench("PartWorkbench")
            view.SetInt("RenderCache", 0)          # re-create the backend
            view.SetInt("RenderCache", 3)
            v = FreeCADGui.activeDocument().activeView()
            v.viewIsometric()
            FreeCADGui.SendMsgToActiveView("ViewFit")
            note("FIT")
        except Exception:
            note(traceback.format_exc())

    def enable_shadow():
        try:
            FreeCADGui.runCommand("Std_DrawStyleShadow", 0)
            note("SHADOW ON")
        except Exception:
            note(traceback.format_exc())

    # The RenderShadow_*/Render_* per-view properties are materialized
    # with the backend, but the view itself may not exist yet, so retry
    # until the assignment sticks.
    def tune_shadow(tries=[0]):
        try:
            v = FreeCADGui.activeDocument().activeView()
            v.RenderShadow_SmoothBorder = int(os.environ.get("SHADOWSMOOTH", "40"))
            v.Render_LightIntensity = float(os.environ.get("SUN", "0.15"))
            if "VOLDENSITY" in os.environ:
                v.Render_VolumetricDensity = float(os.environ["VOLDENSITY"])
            FreeCADGui.SendMsgToActiveView("ViewFit")
            note("SHADOW SMOOTH %s" % v.RenderShadow_SmoothBorder)
        except Exception:
            tries[0] += 1
            if tries[0] < 20:
                QtCore.QTimer.singleShot(500, tune_shadow)
            else:
                note(traceback.format_exc())

    QtCore.QTimer.singleShot(2500, setup_view)
    QtCore.QTimer.singleShot(4500, enable_shadow)
    QtCore.QTimer.singleShot(6000, tune_shadow)
    note("SETUP OK - serving")
except Exception:
    note(traceback.format_exc())
