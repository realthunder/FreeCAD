"""Inspection-shading scene: geometry chosen to show what matcap and
cavity (docs/ShaderDesign.md §3.11, §3.12) actually do.

Both are inspection shading -- they make *form* read rather than making
the scene look lit -- so the scene is deliberately monochrome and full of
the features they respond to:

  Corrugation   round grooves and ridges of shrinking radius, side by
                side. Cavity's valley term darkens the grooves, its ridge
                term the crests, and both bands narrow with the radius --
                the clearest read of what the two strengths do.
  Staircase     square steps: the sharpest concave corners and convex
                edges in the scene, where cavity is at its strongest.
  Golf ball     a sphere pitted with spherical dimples: smooth curvature
                everywhere, so matcap carries the form and cavity rings
                every dimple.
  Shaft         a hex head and a run of circular grooves -- curvature that
                changes along one axis, which matcap bands very visibly.
  Plate         a machined part: pockets at three depths, through holes,
                chamfered rim. The realistic case for both effects.
  Torus/blob    smooth, crease-free curvature: what matcap does *alone*,
                and where cavity correctly finds almost nothing.

Everything is tessellated fine (small Deviation / AngularDeflection) on
purpose: cavity reads the prepass normals, so a coarse mesh would wear
its own triangle grid.

Starts with Matcap + Cavity on, which is the pairing the two are for.
Flip them in the Display style tool button's Shading section, or from the
console: `Gui.activeView().Render_Matcap = False`.

Env knobs: MATCAP / CAVITY (0 disables), PRESET (0 Studio, 1 Clay,
2 Metal, 3 Pearl), VALLEY / RIDGE (cavity strengths), AO.

Runs persistently (no auto-close) so it can be viewed / streamed.
"""
import math
import os
import traceback

import FreeCAD
import FreeCADGui
import Part

_out = os.environ.get("SMOKE_RESULT")


def note(msg):
    if _out:
        with open(_out, "a") as f:
            f.write(str(msg) + "\n")


def V(x, y, z):
    return FreeCAD.Vector(x, y, z)


def corrugation():
    """A slab whose top face alternates cut grooves and fused ridges, the
    radius halving across it. Cavity should darken every groove and crest
    with a band that narrows as the radius does."""
    body = Part.makeBox(76, 26, 8, V(-38, -13, 0))
    cutters, adders = [], []
    x = -32
    radius = 5.0
    while radius > 0.7:
        # groove: a cylinder along Y sunk into the top face
        cutters.append(Part.makeCylinder(radius, 30, V(x, -15, 8), V(0, 1, 0)))
        x += radius * 2.6
        # ridge: the same radius standing proud of it
        adders.append(Part.makeCylinder(radius, 30, V(x, -15, 8), V(0, 1, 0)))
        x += radius * 2.6
        radius *= 0.68
    shape = body.cut(Part.makeCompound(cutters))
    for a in adders:
        shape = shape.fuse(a)
    return shape.removeSplitter()


def staircase():
    """Square steps: the sharpest creases in the scene."""
    steps = []
    for i in range(6):
        steps.append(Part.makeBox(30 - i * 4, 26, 4, V(-15 + i * 2, -13, i * 4)))
    shape = steps[0]
    for s in steps[1:]:
        shape = shape.fuse(s)
    return shape.removeSplitter()


def golfball():
    """Sphere pitted with spherical dimples, spiralled so the rows do not
    line up into bands that could be mistaken for shading artifacts."""
    r = 14.0
    ball = Part.makeSphere(r)
    dimples = []
    count = 42
    # Fibonacci sphere: even coverage without pole clustering.
    golden = math.pi * (3.0 - math.sqrt(5.0))
    for i in range(count):
        z = 1.0 - 2.0 * i / float(count - 1)
        rho = math.sqrt(max(0.0, 1.0 - z * z))
        a = golden * i
        p = V(math.cos(a) * rho * r, math.sin(a) * rho * r, z * r)
        dimples.append(Part.makeSphere(2.7, p))
    return ball.cut(Part.makeCompound(dimples))


def shaft():
    """Hex head over a run of circular grooves: curvature that varies
    along the axis, which matcap bands strongly."""
    hexpts = [V(9 * math.cos(math.radians(60 * i)),
                9 * math.sin(math.radians(60 * i)), 0) for i in range(6)]
    hexpts.append(hexpts[0])
    head = Part.Face(Part.makePolygon(hexpts)).extrude(V(0, 0, 10))
    body = Part.makeCylinder(6, 26, V(0, 0, 10))
    shape = head.fuse(body)
    grooves = [Part.makeTorus(6, 1.1, V(0, 0, 15 + i * 4)) for i in range(5)]
    shape = shape.cut(Part.makeCompound(grooves))
    tip = Part.makeCone(6, 2.5, 4, V(0, 0, 36))
    return shape.fuse(tip).removeSplitter()


def plate():
    """A machined part: pockets at three depths, through holes, a
    chamfered rim -- the realistic case for both effects."""
    body = Part.makeBox(52, 34, 10, V(-26, -17, 0))
    cutters = []
    for i, depth in enumerate((2.0, 4.0, 6.5)):
        x = -20 + i * 15
        cutters.append(Part.makeBox(11, 18, depth + 1,
                                    V(x, -9, 10 - depth)))
    for sx in (-1, 1):
        for sy in (-1, 1):
            cutters.append(Part.makeCylinder(2.4, 14,
                                             V(sx * 21, sy * 13, -2)))
    shape = body.cut(Part.makeCompound(cutters))
    try:
        # Chamfer the outer rim only: the vertical corner edges plus the
        # top border, found by their bounding box rather than by index.
        bb = shape.BoundBox
        rim = [e for e in shape.Edges
               if abs(e.BoundBox.ZMax - bb.ZMax) < 1e-6
               and (abs(e.BoundBox.XMin - bb.XMin) < 1e-6
                    or abs(e.BoundBox.XMax - bb.XMax) < 1e-6
                    or abs(e.BoundBox.YMin - bb.YMin) < 1e-6
                    or abs(e.BoundBox.YMax - bb.YMax) < 1e-6)]
        if rim:
            shape = shape.makeChamfer(1.2, rim)
    except Exception:
        note("plate chamfer skipped: " + traceback.format_exc(limit=1))
    return shape


def blob():
    """Smooth crease-free curvature: matcap alone, and cavity correctly
    finding almost nothing."""
    shape = Part.makeTorus(13, 5)
    for i in range(3):
        a = math.radians(120 * i)
        shape = shape.fuse(
            Part.makeSphere(7, V(math.cos(a) * 13, math.sin(a) * 13, 0)))
    return shape.removeSplitter()


try:
    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)                 # renderer (bgfx) path
    view.SetBool("ShowNaviCube", True)
    view.SetBool("CornerCoordSystem", True)
    view.SetBool("ShowFPS", False)
    view.SetBool("ShowPreSelectedFaceOutline", True)
    view.SetBool("ShowSelectedFaceOutline", True)
    # The parts sit on nothing; the draw style's auto ground plane would
    # only blow up the view-fit bounds.
    view.SetBool("ShadowShowGround", False)
    view.SetFloat("ShadowLightIntensity", float(os.environ.get("SUN", "0.9")))
    view.SetInt("ShadowSmoothBorder", 30)

    nav = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/NaviCube")
    nav.SetBool("AutoHideButton", False)
    nav.SetBool("AutoHideCube", False)

    # Set the globals BEFORE a backend is selected: initRenderProperties
    # materializes the per-view Render_* properties from them.
    render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    render.SetString("Type", "bgfx - OpenGL")
    render.SetBool("AO", os.environ.get("AO", "1") == "1")
    render.SetFloat("AOIntensity", 0.7)
    render.SetBool("Matcap", os.environ.get("MATCAP", "1") == "1")
    render.SetInt("MatcapPreset", int(os.environ.get("PRESET", "1")))  # Clay
    render.SetFloat("MatcapTint", 0.0)
    render.SetBool("Cavity", os.environ.get("CAVITY", "1") == "1")
    render.SetFloat("CavityValley", float(os.environ.get("VALLEY", "1.0")))
    render.SetFloat("CavityRidge", float(os.environ.get("RIDGE", "0.5")))
    render.SetBool("PBR", True)          # so 'Realistic' has somewhere to go
    render.SetBool("Bloom", False)
    render.SetBool("WaterSurface", False)
    render.SetBool("Caustics", False)
    render.SetBool("GroundReflection", False)
    render.SetBool("SunDisc", False)

    doc = FreeCAD.newDocument("Inspect")

    # A 3x2 grid rather than a row: a long thin scene fits to nothing.
    # Ordered "pure crease" to "pure curvature" along the reading order,
    # so one view walks the range.
    parts = (
        ("Corrugation", corrugation, V(-58, 32, 0), (0.72, 0.72, 0.74)),
        ("Staircase", staircase, V(28, 32, 0), (0.70, 0.71, 0.73)),
        ("Plate", plate, V(104, 32, 0), (0.74, 0.73, 0.70)),
        ("Shaft", shaft, V(-58, -34, 0), (0.76, 0.76, 0.78)),
        ("GolfBall", golfball, V(28, -34, 16), (0.75, 0.75, 0.77)),
        ("Blob", blob, V(104, -34, 8), (0.73, 0.74, 0.76)),
    )

    for name, build, where, color in parts:
        try:
            shape = build()
        except Exception:
            note("%s FAILED\n%s" % (name, traceback.format_exc()))
            continue
        obj = doc.addObject("Part::Feature", name)
        obj.Shape = shape
        obj.Placement.Base = where
        vo = obj.ViewObject
        vo.ShapeColor = color
        # Fine tessellation: cavity reads the prepass normals, and a
        # coarse mesh would make every facet boundary read as a crease.
        vo.Deviation = 0.02
        vo.AngularDeflection = 5.0
        vo.addProperty("App::PropertyFloat", "Render_Metallic").Render_Metallic = 0.1
        vo.addProperty("App::PropertyFloat", "Render_Roughness").Render_Roughness = 0.55
        note("%s built, %d faces" % (name, len(shape.Faces)))

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
        # The Shadow draw style is what puts a directional scene light in
        # the graph at all -- the renderer picks its light out of the Coin
        # state, and Render_Shadow only drops the shadow map. Matcap does
        # not care, but 'Realistic' and the AO/shadow toggles do.
        try:
            FreeCADGui.runCommand("Std_DrawStyleShadow", 0)
            note("SHADOW ON")
        except Exception:
            note(traceback.format_exc())

    def report(tries=[0]):
        try:
            v = FreeCADGui.activeDocument().activeView()
            note("matcap=%s preset=%s cavity=%s valley=%s ridge=%s" % (
                v.Render_Matcap, v.Render_MatcapPreset, v.Render_Cavity,
                v.Render_CavityValley, v.Render_CavityRidge))
            FreeCADGui.SendMsgToActiveView("ViewFit")
        except Exception:
            tries[0] += 1
            if tries[0] < 20:
                QtCore.QTimer.singleShot(500, report)
            else:
                note(traceback.format_exc())

    QtCore.QTimer.singleShot(2500, setup_view)
    QtCore.QTimer.singleShot(4500, enable_shadow)
    QtCore.QTimer.singleShot(6000, report)
    note("SETUP OK - serving")
except Exception:
    note(traceback.format_exc())
