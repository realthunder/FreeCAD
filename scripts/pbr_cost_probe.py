"""Measure what the PBR shading path costs a frame, against Phong.

The question this answers is whether `Render_PBR` is cheap enough to be
the default shading model. It is one uber-shader with a runtime branch
(`u_pbrParams.x > 0.5`, fc_mesh_lighting.sh), so the cost is per
FRAGMENT, not per draw: a prefiltered environment lookup, a 9-term SH
irradiance, an analytic env BRDF and GGX in place of one pow() per
light -- plus `fcBaseFromSpecular`, which runs for every Phong-authored
material (nearly all of them) where nothing states a metalness.

Because the cost is per fragment, the scene is built for COVERAGE: a
backdrop filling the frame plus a grid of spheres in front of it.

!! A desktop frame does not expose that cost on its own. Measured, a
14x sweep of the covered-pixel count moved the frame by nothing at all
-- 11.3 ms whatever the camera and whatever the leg -- because the
frame is bound by the CPU (renderer plus Qt/Coin composite) and the
GPU finishes inside that with room to spare. So the probe AMPLIFIES:
it stacks N full-frame opaque layers, built far to near so submission
order defeats early-z and each one really shades, and sweeps N. The
slope of frame time against N is the cost of one full-screen shading
pass outright, and the difference between the legs' slopes is what PBR
costs per fragment. A Phong slope of zero says the overdraw never
reached the shader, and the report refuses to interpret the rest.

Legs, all three at every camera:
  phong     Render_PBR off
  pbr       Render_PBR on, Render_PBRFromSpecular on  (the realistic
            default: no material here authors a metalness, so the
            spec-gloss solve runs on every fragment)
  pbr-nofs  Render_PBR on, Render_PBRFromSpecular off (isolates what
            that solve costs)

The numbers come from `RenderDebug_Timing` (the global DebugTiming
parameter), whose once-a-second line carries the frame cost, and from
`getRenderStats()` for the covered-pixel count. Both are read out of
the log file, so the run needs `--log-file`, and scripts/
pbr_cost_report.py reduces the pair.

!! bgfx's GPU timer reports nothing here: Mesa d3d12 under WSLg
advertises ARB_timer_query and never resolves a timestamp, so the line
reads `gpu n/a`. That is why the frame's WALL time, amplified until
the GPU binds, is the instrument rather than the GPU timer.

Every effect switch is pinned OFF as a view property rather than left
to the preferences: a real user.cfg has Matcap on, which would shade
the whole scene with one material and erase the thing being measured.

!! Launch with **FC_SWAP_INTERVAL=0** or the run measures nothing: the
frame is otherwise vblank-locked at exactly 60 Hz and every leg costs
16.7 ms by definition, with the whole difference sitting in the swap
wait. It is not a preference by design (Gui/Application.cpp).

Usage: FC_SWAP_INTERVAL=0 FreeCAD --log-file <log> \
           scripts/pbr_cost_probe.py
Env:   PBR_COST_OUT     result JSON (default /tmp/pbr-cost.json)
       PBR_COST_SECS    seconds of frames per cell (default 8)
       PBR_COST_LAYERS  overdraw sweep (default "0,6,12,18,24")
       PBR_COST_EXIT    "1" = exit when done
"""
import json
import os
import time
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide.QtCore import QTimer

OUT = os.environ.get("PBR_COST_OUT", "/tmp/pbr-cost.json")
SECS = float(os.environ.get("PBR_COST_SECS", "8"))
EXIT = os.environ.get("PBR_COST_EXIT", "") == "1"
SYNC_FRAMES = int(os.environ.get("PBR_COST_SYNC", "60"))

# Must run before the first 3D view exists: the backend is only created
# for render cache mode 3, and the type string is exact -- a wrong one
# leaves the renderer null and every leg draws plain Coin.
_view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
_view.SetInt("RenderCache", 3)
_render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
_render.SetString("Type", "bgfx - OpenGL")
# The frame-cost instrument. Global (not a view property) since
# 2026-08-14; left on for the whole run rather than toggled per leg,
# because it switches BGFX_DEBUG_PROFILER for the whole context and a
# leg measured with it in a different state is not comparable.
_render.SetBool("DebugTiming", True)
# Autosave rewrites the document mid-run and lands inside a timing
# window; the navigation cube adds draws that belong to no leg.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
    "AutoSaveTimeout", 0)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
    "AutoSaveEnabled", False)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/NaviCube").SetBool(
    "ShowNaviCube", False)

# Scene: a backdrop wide enough to fill the frame at the closest
# camera, and a grid of spheres in front of it for curvature (a flat
# wall alone shades every fragment with the same normal, which is not
# what a model looks like).
BACK_W = 420.0
BACK_H = 170.0
SPHERE_R = 22.0
GRID = (-70.0, 0.0, 70.0)
# Orthographic heights: ~full frame, ~1/4, ~1/16 of the covered pixels.
HEIGHTS = [BACK_H, BACK_H * 2.0, BACK_H * 4.0]

# Overdraw sweep. A desktop frame is bound by the CPU long before this
# shader's fragment cost shows up in it -- a 14x change in covered
# pixels moved a measured frame by nothing -- so the per-fragment cost
# has to be amplified until the GPU is what the frame waits for.
#
# Each layer is a full-frame opaque quad in front of the last, built
# FAR TO NEAR so submission order defeats early-z rejection and every
# layer actually shades. The slope of frame time against layer count is
# then the cost of one full-screen shading pass outright -- a better
# number than the coverage fit, because it needs no assumption about
# what else the frame is doing. A slope of zero is the honest report
# that the overdraw did not happen.
LAYERS = [int(v) for v in
          os.environ.get("PBR_COST_LAYERS", "0,200,400,600,800").split(",")]
LAYER_GAP = 0.5
LAYER_THICK = 0.2
LAYER_NEAR = 50.0

# Phong appearances with real specular colours -- fcBaseFromSpecular
# only has something to solve where the specular says something the
# diffuse does not.
COLORS = [
    ((0.80, 0.80, 0.82), (0.90, 0.90, 0.90)),   # steel-ish
    ((0.75, 0.60, 0.20), (0.95, 0.80, 0.35)),   # brass-ish
    ((0.20, 0.35, 0.70), (0.60, 0.60, 0.60)),   # blue plastic
    ((0.60, 0.20, 0.20), (0.80, 0.75, 0.70)),   # red
    ((0.30, 0.55, 0.30), (0.50, 0.55, 0.50)),   # green
]

# Every effect that would add passes to the frame, pinned off so the
# frame is the mesh pass and nothing else.
OFF = ["AO", "Shadow", "Cavity", "Matcap", "Parallax", "Volumetric",
       "Bloom", "Light", "LightSpot", "SunDisc", "Caustics",
       "WaterSurface", "WaterRefraction", "WaterReflection",
       "WaterPlanarReflection", "WaterShadow", "GroundReflection",
       "PBREnvBackground"]

LEGS = [
    ("phong",    dict(PBR=False, PBRFromSpecular=True)),
    ("pbr",      dict(PBR=True,  PBRFromSpecular=True)),
    ("pbr-nofs", dict(PBR=True,  PBRFromSpecular=False)),
]


def pump(n=3):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()


def mark(text):
    """A marker in the same log the render lines go to, so the parse
    can attribute each timing line to the cell that produced it."""
    FreeCAD.Console.PrintMessage("PBRCOST %s\n" % text)


def setprop(container, name, kind, value):
    """Render_* view properties are pre-created by the viewer when the
    backend starts; addProperty is idempotent, and doing it anyway
    keeps the probe working on a view that never selected a renderer."""
    if not hasattr(container, name):
        container.addProperty(kind, name)
    setattr(container, name, value)


def build_scene():
    doc = FreeCAD.newDocument("PBRCost")
    back = doc.addObject("Part::Box", "Backdrop")
    back.Length = BACK_W
    back.Width = 4.0
    back.Height = BACK_H
    back.Placement.Base = FreeCAD.Vector(-BACK_W / 2.0, 60.0, -BACK_H / 2.0)
    back.ViewObject.ShapeColor = (0.55, 0.55, 0.58)

    n = 0
    for z in GRID:
        for x in GRID:
            sph = doc.addObject("Part::Sphere", "Sphere%d" % n)
            sph.Radius = SPHERE_R
            sph.Placement.Base = FreeCAD.Vector(x, 0.0, z)
            diffuse, specular = COLORS[n % len(COLORS)]
            vo = sph.ViewObject
            vo.ShapeColor = diffuse
            # Coin's 0..1 shininess; 0.2 is FreeCAD's default and the
            # roughness the PBR path derives from it when nothing
            # authors one.
            mat = vo.ShapeAppearance[0]
            mat.SpecularColor = specular
            mat.Shininess = 0.2
            vo.ShapeAppearance = [mat]
            n += 1

    # The overdraw stack, all of it, hidden. Built once and toggled per
    # cell: adding and removing objects would rebuild the render cache
    # inside the sweep, and the cost of that rebuild is not the cost
    # being measured.
    # !! All the layers of a cell live in ONE compound, so N layers
    # cost one draw. Built as N separate objects instead, each layer
    # carried a draw's worth of CPU (~17 us in this build) alongside
    # its fragments -- and since that CPU is the term the frame is
    # already bound by, the count could never be raised far enough for
    # the GPU to become what the frame waits for, which is the entire
    # point of raising it. One draw per cell decouples the two:
    # fragments climb, submission does not.
    stacks = {}
    for count in LAYERS:
        if count <= 0:
            stacks[count] = None
            continue
        quads = [Part.makeBox(BACK_W, LAYER_THICK, BACK_H,
                              FreeCAD.Vector(-BACK_W / 2.0,
                                             LAYER_NEAR - i * LAYER_GAP,
                                             -BACK_H / 2.0))
                 for i in range(count)]
        obj = doc.addObject("Part::Feature", "Stack%d" % count)
        obj.Shape = Part.makeCompound(quads)
        vo = obj.ViewObject
        vo.ShapeColor = (0.50, 0.52, 0.55)
        # Shaded, not Flat Lines: edge draws would add submission cost
        # that scales with the layer count and read as fragment cost.
        vo.DisplayMode = "Shaded"
        vo.Visibility = False
        stacks[count] = obj

    doc.recompute()
    return doc, stacks


def stage_view(view):
    view.setCameraType("Orthographic")
    view.viewFront()
    for name in OFF:
        setprop(view, "Render_" + name, "App::PropertyBool", False)
    # Roughness 0 means "derive from the material shininess" -- the
    # path a Phong-authored appearance actually takes.
    setprop(view, "Render_PBRMetallic", "App::PropertyFloat", 0.0)
    setprop(view, "Render_PBRRoughness", "App::PropertyFloat", 0.0)
    setprop(view, "Render_PBREnvIntensity", "App::PropertyFloat", 1.0)


def pin_camera(view, height):
    """Pin the camera outright rather than fitting: a delayed fitAll
    lands at a different moment run to run and the legs stop being
    comparable ([[render-ab-harness]])."""
    cam = view.getCameraNode()
    cam.position.setValue(0.0, -400.0, 0.0)
    cam.focalDistance.setValue(400.0)
    cam.nearDistance.setValue(1.0)
    cam.farDistance.setValue(1000.0)
    cam.height.setValue(height)


def run_cell(view, leg, height, nlayers, secs):
    mark("BEGIN leg=%s height=%.1f layers=%d" % (leg, height, nlayers))
    end = time.time() + secs
    frames = 0
    while time.time() < end:
        view.redraw()
        FreeCADGui.updateGui()
        frames += 1
    mark("END leg=%s height=%.1f layers=%d frames=%d"
         % (leg, height, nlayers, frames))
    return frames


def show_stack(stacks, count):
    for size, obj in stacks.items():
        if obj is not None:
            obj.ViewObject.Visibility = (size == count)


def run_sync(view, count):
    """Mean wall time of readback-forced frames.

    `getRenderStats()` reads the finished colour target back, and a
    readback cannot complete until the GPU has actually drawn the
    frame. That SERIALIZES the two halves, which is the whole point:
    free-running, this frame's CPU (renderer plus Qt/Coin composite)
    outruns its GPU by an order of magnitude, so fragment cost is
    invisible in frame time no matter how much of it there is. The
    readback itself costs the same in every leg, so it cancels out of a
    difference even though it dominates the absolute number.
    """
    view.getRenderStats()
    t0 = time.perf_counter()
    for _ in range(count):
        view.getRenderStats()
    return (time.perf_counter() - t0) * 1000.0 / count


def probe():
    result = {"cells": []}
    try:
        # A fixed, generous viewport: the per-fragment cost is what is
        # being measured, so the number of fragments has to be stated
        # rather than inherited from whatever size the window opened at.
        mw = FreeCADGui.getMainWindow()
        mw.resize(1700, 1100)
        FreeCADGui.updateGui()

        doc, stacks = build_scene()
        view = FreeCADGui.ActiveDocument.ActiveView
        stage_view(view)
        pump(5)

        # !! Burn a throwaway cell first. The first measured cell of
        # every run so far came back 2-3x slow (shader compiles, the
        # environment prefilter, the backend's first real frames) and
        # it is not detectable as stale -- its coverage is correct, its
        # timing is not. Paying for it once, unrecorded, is what stops
        # it landing on whichever leg happens to run first.
        show_stack(stacks, max(LAYERS))
        for leg, props in LEGS:
            for name, value in props.items():
                setprop(view, "Render_" + name, "App::PropertyBool", value)
            pin_camera(view, HEIGHTS[0])
            pump(30)
            run_sync(view, 10)
        mark("WARMUP done")

        for leg, props in LEGS:
            for name, value in props.items():
                setprop(view, "Render_" + name, "App::PropertyBool", value)
            for nlayers in LAYERS:
                show_stack(stacks, nlayers)
                height = HEIGHTS[0]
                pin_camera(view, height)
                # Settle: the first frames after a switch pay for the
                # environment prefilter and the backend's one-frame
                # lag, and would otherwise land inside the window.
                pump(30)
                time.sleep(1.0)
                pump(10)
                before = view.getRenderStats()
                pin_camera(view, height)
                frames = run_cell(view, leg, height, nlayers, SECS)
                sync_ms = run_sync(view, SYNC_FRAMES)
                # !! Read the coverage on BOTH sides. The backend draws
                # one frame behind a scene change, so a stats call too
                # soon after the camera moves reports the previous
                # framing -- the first cell of the first run came back
                # 4550 px where its siblings measured 809300. Two reads
                # that agree is the only evidence the cell was staged.
                after = view.getRenderStats()
                result["cells"].append({
                    "leg": leg,
                    "height": height,
                    "layers": nlayers,
                    "frames": frames,
                    "syncMs": sync_ms,
                    "width": after.get("width"),
                    "height_px": after.get("height"),
                    "geometryPixels": after.get("geometryPixels"),
                    "geometryPixelsBefore": before.get("geometryPixels"),
                    "staged": before.get("geometryPixels")
                              == after.get("geometryPixels"),
                })
                FreeCAD.Console.PrintMessage(
                    "PBRCOST cell done leg=%s h=%.1f layers=%d px=%s "
                    "(before %s) frames=%d sync=%.3fms\n"
                    % (leg, height, nlayers, after.get("geometryPixels"),
                       before.get("geometryPixels"), frames, sync_ms))

        with open(OUT, "w") as fp:
            json.dump(result, fp, indent=1)
        FreeCAD.Console.PrintMessage("PBRCOST RESULT written %s\n" % OUT)
    except Exception:
        traceback.print_exc()
        FreeCAD.Console.PrintError("PBRCOST FAILED\n")
    finally:
        # Never leave the process to a modal save prompt: a modified
        # document turns mw.close() into a question nobody answers.
        try:
            FreeCAD.closeDocument(doc.Name)
        except Exception:
            pass
        if EXIT:
            os._exit(0)


QTimer.singleShot(1500, probe)
