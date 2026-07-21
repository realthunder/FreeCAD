"""Demo scene for the bgfx render-cache backend: a shallow WATER pool
(Render_Water body), a metallic cylinder rising through it, and a FIRE plume
(Render_Fire) on top, with the volumetric / SSAO / shadow / water-surface /
caustics effects enabled. Run it via scripts/renderer-desktop.sh or
scripts/renderer-serve.sh.

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
    view.SetBool("ShowFPS", True)

    nav = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/NaviCube")
    nav.SetBool("AutoHideButton", False)
    nav.SetBool("AutoHideCube", False)

    render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    render.SetString("Type", "bgfx - OpenGL")     # backend factory string
    render.SetBool("SSAO", True)
    render.SetBool("Volumetric", True)            # water tint + fire + shafts
    render.SetBool("WaterSurface", True)          # refraction + reflection
    render.SetFloat("WaterWaveStrength", 0.3)
    render.SetBool("GroundReflection", False)
    render.SetBool("Caustics", True)
    render.SetFloat("CausticsIntensity", 1.0)

    doc = FreeCAD.newDocument("WaterFire")

    box = doc.addObject("Part::Box", "Water")
    box.Length, box.Width, box.Height = 44, 44, 7
    box.Placement.Base = FreeCAD.Vector(-22, -22, 0)

    cyl = doc.addObject("Part::Cylinder", "Cylinder")
    cyl.Radius, cyl.Height = 3, 22

    fire = doc.addObject("Part::Cone", "Fire")
    fire.Radius1, fire.Radius2, fire.Height = 3.2, 0.0, 10
    fire.Placement.Base = FreeCAD.Vector(0, 0, 22)

    doc.recompute()

    bvo = box.ViewObject
    bvo.ShapeColor = (0.15, 0.45, 0.75)
    bvo.Transparency = 0
    bvo.addProperty("App::PropertyBool", "Render_Water").Render_Water = True
    bvo.addProperty("App::PropertyFloat", "Render_WaterDensity").Render_WaterDensity = 0.0

    cvo = cyl.ViewObject
    cvo.ShapeColor = (0.75, 0.75, 0.78)
    cvo.addProperty("App::PropertyFloat", "Render_Metallic").Render_Metallic = 0.9
    cvo.addProperty("App::PropertyFloat", "Render_Roughness").Render_Roughness = 0.35

    fvo = fire.ViewObject
    fvo.ShapeColor = (1.0, 0.55, 0.1)
    fvo.addProperty("App::PropertyBool", "Render_Fire").Render_Fire = True
    fvo.addProperty("App::PropertyFloat", "Render_FireIntensity").Render_FireIntensity = 0.0
    fvo.addProperty("App::PropertyFloat", "Render_FireDetail").Render_FireDetail = 0.0
    fvo.addProperty("App::PropertyFloat", "Render_FireSpeed").Render_FireSpeed = 1.0

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
