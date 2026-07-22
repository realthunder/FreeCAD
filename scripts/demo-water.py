"""Demo scene for the bgfx render-cache backend: a shallow WATER pool with a
beam suspended on two posts casting a shadow onto the water surface
(Render_WaterShadow), a metallic cylinder rising through the pool, and a FIRE
plume (Render_Fire) on top of the cylinder. Volumetric / SSAO / shadow /
water-surface (refraction + planar reflection + water shadow) effects on.
Run it via scripts/renderer-desktop.sh or scripts/renderer-serve.sh.

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

    # Preselection / selection highlight as OUTLINE ONLY (no filled face).
    # Preselect is already outline-only by default (NoPreSel...=True); the
    # non-default one is selection (NoSel...=False), set here too. The viewer
    # streams these (preselconf) for its local hover highlight; selection is
    # backend-driven and honours them directly.
    view.SetBool("ShowPreSelectedFaceOutline", True)
    view.SetBool("ShowSelectedFaceOutline", True)
    view.SetBool("NoPreSelFaceHighlightWithOutline", True)
    view.SetBool("NoSelFaceHighlightWithOutline", True)

    nav = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/NaviCube")
    nav.SetBool("AutoHideButton", False)
    nav.SetBool("AutoHideCube", False)

    render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    render.SetString("Type", "bgfx - OpenGL")     # backend factory string
    render.SetBool("SSAO", True)
    render.SetBool("Volumetric", True)            # water body + fire + shafts
    render.SetBool("WaterSurface", True)          # refraction + reflection
    render.SetFloat("WaterWaveStrength", 0.25)
    render.SetBool("WaterRefraction", True)
    render.SetBool("WaterReflection", True)
    render.SetBool("WaterPlanarReflection", True)
    render.SetBool("WaterShadow", True)           # beam shadow on the water
    render.SetBool("GroundReflection", False)
    render.SetBool("Caustics", False)

    doc = FreeCAD.newDocument("WaterFire")

    # Shallow pool, top surface at z = 7.
    box = doc.addObject("Part::Box", "Water")
    box.Length, box.Width, box.Height = 48, 48, 7
    box.Placement.Base = FreeCAD.Vector(-24, -24, 0)

    # A metallic cylinder rising through the pool.
    cyl = doc.addObject("Part::Cylinder", "Cylinder")
    cyl.Radius, cyl.Height = 3, 20
    cyl.Placement.Base = FreeCAD.Vector(-12, 10, 0)

    # A fire plume on top of the cylinder (base at the cylinder's top z = 20).
    fire = doc.addObject("Part::Cone", "Fire")
    fire.Radius1, fire.Radius2, fire.Height = 3.2, 0.0, 10
    fire.Placement.Base = FreeCAD.Vector(-12, 10, 20)

    # A beam suspended over the water on two posts -- the shadow caster.
    beam = doc.addObject("Part::Box", "Beam")
    beam.Length, beam.Width, beam.Height = 44, 7, 4
    beam.Placement.Base = FreeCAD.Vector(-22, -3, 20)

    postA = doc.addObject("Part::Box", "PostA")
    postA.Length, postA.Width, postA.Height = 4, 4, 20
    postA.Placement.Base = FreeCAD.Vector(-22, -1.5, 4)

    postB = doc.addObject("Part::Box", "PostB")
    postB.Length, postB.Width, postB.Height = 4, 4, 20
    postB.Placement.Base = FreeCAD.Vector(18, -1.5, 4)

    doc.recompute()

    bvo = box.ViewObject
    bvo.ShapeColor = (0.13, 0.42, 0.72)
    bvo.Transparency = 0
    bvo.addProperty("App::PropertyBool", "Render_Water").Render_Water = True
    bvo.addProperty("App::PropertyFloat", "Render_WaterDensity").Render_WaterDensity = 0.0

    cvo = cyl.ViewObject
    cvo.ShapeColor = (0.78, 0.78, 0.80)
    cvo.addProperty("App::PropertyFloat", "Render_Metallic").Render_Metallic = 0.9
    cvo.addProperty("App::PropertyFloat", "Render_Roughness").Render_Roughness = 0.35

    fvo = fire.ViewObject
    fvo.ShapeColor = (1.0, 0.55, 0.1)
    fvo.addProperty("App::PropertyBool", "Render_Fire").Render_Fire = True
    fvo.addProperty("App::PropertyFloat", "Render_FireIntensity").Render_FireIntensity = 0.0
    fvo.addProperty("App::PropertyFloat", "Render_FireDetail").Render_FireDetail = 0.0
    fvo.addProperty("App::PropertyFloat", "Render_FireSpeed").Render_FireSpeed = 1.0

    for o, col in ((beam, (0.72, 0.55, 0.35)),
                   (postA, (0.60, 0.46, 0.30)),
                   (postB, (0.60, 0.46, 0.30))):
        o.ViewObject.ShapeColor = col

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

    QtCore.QTimer.singleShot(2500, setup_view)
    QtCore.QTimer.singleShot(4500, enable_shadow)
    note("SETUP OK - serving")
except Exception:
    note(traceback.format_exc())
