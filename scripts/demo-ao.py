"""Demo scene isolating the SSAO (ambient occlusion) effect for the bgfx
render-cache backend: ALL other effects off (no shadow, volumetric, water,
reflection, PBR), only SSAO on. Matte light-grey objects in mutual contact and
tight concave corners on a ground plane, where ambient occlusion reads
strongest (contact darkening, crevices, inside corners).

Run via scripts/renderer-serve.sh or scripts/renderer-desktop.sh; serves
persistently for the WASM viewer to stream.
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
    view.SetInt("RenderCache", 3)
    view.SetBool("ShowNaviCube", True)
    view.SetBool("CornerCoordSystem", True)
    view.SetBool("ShowFPS", False)

    render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    render.SetString("Type", "bgfx - OpenGL")
    # --- ONLY ambient occlusion on -------------------------------------
    _ao = os.environ.get("AO", "1") == "1"
    render.SetBool("SSAO", _ao)
    render.SetFloat("SSAOIntensity", 1.6)   # a touch stronger so it reads clearly
    render.SetFloat("SSAORadius", float(os.environ.get("AORADIUS", "0.0")))  # 0=auto
    # AO algorithm: 0 = classic hemisphere SSAO, 1 = GTAO (horizon-based).
    render.SetInt("SSAOMethod", int(os.environ.get("AOMETHOD", "0")))
    # Dedicated AO resolution control (independent of EffectResolution).
    render.SetFloat("SSAOResolution", float(os.environ.get("AORES", "1.0")))
    # --- everything else off -------------------------------------------
    render.SetBool("Shadow", False)         # default is True -> must disable
    render.SetBool("Volumetric", False)
    render.SetBool("Caustics", False)
    render.SetBool("WaterSurface", False)
    render.SetBool("WaterRefraction", False)
    render.SetBool("WaterReflection", False)
    render.SetBool("WaterPlanarReflection", False)
    render.SetBool("WaterShadow", False)
    render.SetBool("GroundReflection", False)
    render.SetBool("PBR", False)

    nav = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/NaviCube")
    nav.SetBool("AutoHideButton", False)
    nav.SetBool("AutoHideCube", False)

    doc = FreeCAD.newDocument("AODemo")

    matte = (0.80, 0.80, 0.82)

    def add_box(name, l, w, h, x, y, z, col=matte):
        b = doc.addObject("Part::Box", name)
        b.Length, b.Width, b.Height = l, w, h
        b.Placement.Base = FreeCAD.Vector(x, y, z)
        b.ViewObject.ShapeColor = col
        return b

    # Ground plane.
    add_box("Ground", 120, 120, 2, -60, -60, -2, (0.85, 0.85, 0.86))

    # An inside corner (two tall walls meeting) -- strong AO in the seam.
    add_box("WallA", 4, 40, 30, -30, -20, 0)
    add_box("WallB", 40, 4, 30, -30, -20, 0)

    # A sphere nestled into that corner (contact AO all around the base + seam).
    sph = doc.addObject("Part::Sphere", "Sphere")
    sph.Radius = 9
    sph.Placement.Base = FreeCAD.Vector(-16, -6, 9)
    sph.ViewObject.ShapeColor = matte

    # A cluster of closely spaced pillars -- AO builds up in the narrow gaps.
    for i in range(4):
        for j in range(3):
            add_box("Pillar_%d_%d" % (i, j), 6, 6, 12 + 3 * ((i + j) % 3),
                    5 + i * 9, -18 + j * 9, 0)

    # A box resting on the ground with a smaller box stacked off-centre,
    # forming a step/crevice.
    add_box("BlockA", 22, 22, 10, 8, 18, 0)
    add_box("BlockB", 12, 12, 12, 8, 18, 10)

    # A cylinder and a cone standing next to a wall (contact + side occlusion).
    cyl = doc.addObject("Part::Cylinder", "Cylinder")
    cyl.Radius, cyl.Height = 7, 24
    cyl.Placement.Base = FreeCAD.Vector(-40, 20, 0)
    cyl.ViewObject.ShapeColor = matte

    cone = doc.addObject("Part::Cone", "Cone")
    cone.Radius1, cone.Radius2, cone.Height = 9, 0, 26
    cone.Placement.Base = FreeCAD.Vector(-40, 40, 0)
    cone.ViewObject.ShapeColor = matte

    # A torus lying flat -- AO in the inner ring and under the body.
    tor = doc.addObject("Part::Torus", "Torus")
    tor.Radius1, tor.Radius2 = 12, 4
    tor.Placement.Base = FreeCAD.Vector(34, -24, 4)
    tor.ViewObject.ShapeColor = matte

    doc.recompute()
    note("SCENE BUILT")

    from PySide import QtCore

    def setup_view():
        try:
            FreeCADGui.activateWorkbench("PartWorkbench")
            view.SetInt("RenderCache", 0)   # re-create the backend
            view.SetInt("RenderCache", 3)
            v = FreeCADGui.activeDocument().activeView()
            v.viewIsometric()
            FreeCADGui.SendMsgToActiveView("ViewFit")
            note("FIT")
        except Exception:
            note(traceback.format_exc())

    QtCore.QTimer.singleShot(2500, setup_view)
    note("SETUP OK - serving")
except Exception:
    note(traceback.format_exc())
