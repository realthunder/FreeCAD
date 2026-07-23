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
    # Global default for the Shadow draw style's SmoothBorder: the style
    # materializes its Shadow_SmoothBorder view property from this, so it
    # is soft from the first shadow frame (tune_shadow below then drives
    # the view property directly).
    view.SetInt("ShadowSmoothBorder", int(os.environ.get("SHADOWSMOOTH", "40")))

    nav = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/NaviCube")
    nav.SetBool("AutoHideButton", False)
    nav.SetBool("AutoHideCube", False)

    render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    render.SetString("Type", "bgfx - OpenGL")     # backend factory string
    render.SetBool("AO", os.environ.get("AO", "1") == "1")
    render.SetFloat("AOIntensity", float(os.environ.get("AOINT", "1.0")))
    render.SetBool("Volumetric", os.environ.get("VOL", "1") == "1")  # water body + fire + shafts
    render.SetBool("WaterSurface", True)          # refraction + reflection
    render.SetFloat("WaterWaveStrength", 0.25)
    render.SetBool("WaterRefraction", True)
    render.SetBool("WaterReflection", True)
    render.SetBool("WaterPlanarReflection", True)
    render.SetBool("WaterShadow", True)           # beam shadow on the water
    # Shadow wobble with the wave field (0 = straight band, 1 = physical)
    render.SetFloat("WaterShadowWobble", float(os.environ.get("WOBBLE", "1.0")))
    render.SetBool("GroundReflection", False)
    render.SetBool("Caustics", True)
    render.SetBool("PBR", os.environ.get("PBR", "1") == "1")  # image-based lighting
    render.SetFloat("BumpScale", float(os.environ.get("BUMP", "3.0")))  # normal-map strength

    doc = FreeCAD.newDocument("WaterFire")

    # Shallow pool, top surface at z = 7.
    box = doc.addObject("Part::Box", "Water")
    box.Length, box.Width, box.Height = 48, 48, 7
    box.Placement.Base = FreeCAD.Vector(-24, -24, 0)

    # A metallic cylinder rising through the pool, set near PostA so the two
    # form a crevice where AO accumulates -- far enough (y = 8.5) that the fire
    # plume's base clears the gantry beam (which reaches y = 4).
    cyl = doc.addObject("Part::Cylinder", "Cylinder")
    cyl.Radius, cyl.Height = 3, 20
    cyl.Placement.Base = FreeCAD.Vector(-20, 8.5, 0)

    # A fire plume on top of the cylinder (base at the cylinder's top z = 20).
    fire = doc.addObject("Part::Cone", "Fire")
    fire.Radius1, fire.Radius2, fire.Height = 3.2, 0.0, 10
    fire.Placement.Base = FreeCAD.Vector(-20, 8.5, 20)

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

    # Fuse the beam and both posts (three separate boxes -- effectively a
    # compound) into a single solid gantry; the cylinder stays separate. AO
    # then darkens the concave post/beam junctions and the cylinder-to-gantry
    # gap on one continuous solid.
    frame = doc.addObject("Part::MultiFuse", "Gantry")
    frame.Shapes = [beam, postA, postB]
    doc.recompute()
    # The base-color texture modulates the material colour, so keep the
    # gantry white for the wood texture to show its true tones. (A boolean
    # result carries a per-face DiffuseColor that overrides ShapeColor.)
    frame.ViewObject.ShapeColor = (1.0, 1.0, 1.0)
    frame.ViewObject.DiffuseColor = [(1.0, 1.0, 1.0, 0.0)]
    gvo = frame.ViewObject
    # Wood-like PBR material: dielectric (no metalness), fairly matte.
    gvo.addProperty("App::PropertyFloat", "Render_Metallic").Render_Metallic = 0.0
    gvo.addProperty("App::PropertyFloat", "Render_Roughness").Render_Roughness = 0.8
    # Wood base-color + normal (bump) maps -- ambientCG Wood058 (CC0). The
    # PropertyFileIncluded copies each image into the document.
    wood = os.environ.get(
        "WOOD_DIR",
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "textures"))
    gvo.addProperty("App::PropertyFileIncluded", "Render_BaseColorTexture"
                    ).Render_BaseColorTexture = wood + "/Bark012_2K-JPG_Color.jpg"
    gvo.addProperty("App::PropertyFileIncluded", "Render_NormalMap"
                    ).Render_NormalMap = wood + "/Bark012_2K-JPG_NormalGL.jpg"
    # Rough bark tiles fairly small on the gantry.
    gvo.addProperty("App::PropertyVector", "Render_TextureScale"
                    ).Render_TextureScale = (0.35, 0.35, 0.0)

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

    # Soft shadow border via the Shadow draw style's per-view property
    # (0..100 gaussian over the shadow moments; SHADOWSMOOTH=0 keeps hard
    # borders). The Shadow_* properties materialize on the view lazily at
    # the first shadow render, so retry until the assignment sticks.
    def tune_shadow(tries=[0]):
        try:
            v = FreeCADGui.activeDocument().activeView()
            v.Shadow_SmoothBorder = int(os.environ.get("SHADOWSMOOTH", "40"))
            note("SHADOW SMOOTH %s" % v.Shadow_SmoothBorder)
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
