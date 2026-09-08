# 3D engine backend benchmark on the chess scene.
#
# The vg bench measures the 2D path and a Part::Box grid measures little
# but transform throughput. This one loads what the golden render tests
# load -- the MaterialX chess set, a real asset with real materials, an
# HDR environment and the texture path -- because that is the workload
# the backend question is about. docs/Testing.md calls the chess leg the
# only test exercising MaterialX, map binding and textures.
#
# Run it in the GUI binary, not FreeCADCmd: the engine needs a real 3D
# view. FreeCADCmd with showMainWindow() gives a 1x1 warm-up surface and
# the frames come out "Framebuffer incomplete, missing attachment".
#
# And the work runs from a QTimer, not at import: a script argument is
# executed BEFORE the event loop, so redraw()/waitFrameComplete() there
# waits for frames nothing is pumping. Same reason and same shape as
# scripts/render-test-chess.py.
#
# Two things it must do that a naive loop does not:
#   - force completion per frame. redraw() only schedules, and
#     bgfx::frame() returns after SUBMISSION, so timing around it ranks
#     how much a driver postpones rather than what it finishes.
#     waitFrameComplete() blocks until completeFrames() advances.
#   - move the camera every frame, so nothing is an unchanged replay.
import os
import sys
import time
import traceback

import FreeCAD
import FreeCADGui

BACKEND = os.environ.get("FC_RENDER_BACKEND", "bgfx - OpenGL")
FRAMES = int(os.environ.get("FC_BENCH_FRAMES", "100"))
WARMUP = int(os.environ.get("FC_BENCH_WARMUP", "30"))
OUT = os.environ.get("FC_BENCH_OUT", "")

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(REPO, "src/3rdParty/MaterialX/resources")
GLB = os.path.join(RES, "Geometry/chess_set.glb")
MTLX = os.path.join(
    RES, "Materials/Examples/StandardSurface/standard_surface_chess_set.mtlx")
HDR = os.path.join(RES, "Lights/san_giuseppe_bridge.hdr")
RENDER = "User parameter:BaseApp/Preferences/View/Render"


def say(line):
    print(line)
    sys.stdout.flush()
    if OUT:
        with open(OUT, "a") as fh:
            fh.write(line + "\n")


FreeCAD.ParamGet(RENDER).SetString("Type", BACKEND)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
    "RenderCache", 3)

# Same configuration as the chess golden, so the frame being timed is the
# frame that test blesses: PBR lit by the HDR, every optional stage off,
# so this measures the material and texture path rather than whichever
# effects happened to be enabled.
p = FreeCAD.ParamGet(RENDER)
p.SetBool("PBR", True)
p.SetBool("PBRFromSpecular", False)
p.SetBool("PBREnvBackground", True)
p.SetFloat("PBREnvBlur", 0.0)
p.SetFloat("PBREnvIntensity", 1.0)
p.SetString("PBREnvImage", HDR)
for off in ("AO", "Cavity", "Matcap", "Bloom", "Volumetric",
            "GroundReflection", "Shadow", "Light"):
    p.SetBool(off, False)
# Temporal accumulation keeps refining a parked frame, so a run would
# measure convergence rather than draw cost.
p.SetBool("TemporalAccum", False)
p.SetInt("OutputTransform", 1)
# The engine's own per-stage accounting, which reports the frame's
# CPU cost against its GPU cost. Wall clock around redraw() cannot
# tell a GPU-bound frame from a scheduling artefact; this can.
p.SetBool("DebugTiming", True)
p.SetFloat("Exposure", 1.0)

view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
for off in ("ShowAxisCross", "ShowNaviCube", "CornerCoordSystem", "ShowFPS"):
    view.SetBool(off, False)


def bench():
    import ImportGui

    doc = FreeCAD.newDocument("BenchChess")
    FreeCADGui.ActiveDocument = FreeCADGui.getDocument(doc.Name)
    v = FreeCADGui.ActiveDocument.ActiveView
    # Before any fit: an animated fit is a nested event loop lasting as
    # long as ten frames, and a camera staged under one is overwritten by
    # the animation still in flight (render-test-chess.py says the same).
    v.setAnimationEnabled(False)

    t_load = time.perf_counter()
    ImportGui.insert(GLB, doc.Name)
    ImportGui.insert(MTLX, doc.Name)
    doc.recompute()
    load_s = time.perf_counter() - t_load

    pieces = [o for o in doc.Objects if o.isDerivedFrom("Part::Feature")]
    worn = sum(1 for o in pieces
               if o.ShapeMaterial
               and o.ShapeMaterial.getAppearanceValue("MaterialXSurface"))

    v.setCameraOrientation((0.4247, 0.1759, 0.3389, 0.8226))
    v.fitAll()
    # Let the progressive load and deferred shapes settle, or the first
    # timed frames measure geometry ARRIVING rather than drawing.
    v.waitFrameComplete()

    # setCameraOrientation, not the Coin camera node: getCameraNode()
    # hands back a SWIG object and raises "No SWIG wrapped library
    # loaded" without pivy, which this env does not load into the GUI.
    # The view's own setter takes the same four floats.
    base = FreeCAD.Rotation(0.4247, 0.1759, 0.3389, 0.8226)

    def spin(n):
        for i in range(n):
            # A small orbit: enough that no frame repeats, small enough
            # that the set stays framed and cost stays comparable.
            r = FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), (i % 360) * 1.0)
            q = r.multiply(base).Q
            v.setCameraOrientation((q[0], q[1], q[2], q[3]))
            v.redraw()
            v.waitFrameComplete()

    spin(WARMUP)
    t0 = time.perf_counter()
    spin(FRAMES)
    elapsed = time.perf_counter() - t0

    ms = elapsed * 1000.0 / FRAMES
    say("%-22s %7.2f ms/frame %6.1f fps  shapes=%d mtlx=%d load=%.1fs n=%d"
        % (BACKEND, ms, 1000.0 / ms, len(pieces), worn, load_s, FRAMES))


def deferred():
    try:
        bench()
    except Exception:
        say("FAILED %s\n%s" % (BACKEND, traceback.format_exc()))
    from PySide import QtCore
    QtCore.QCoreApplication.quit()


from PySide import QtCore  # noqa: E402  (after the parameter setup above)

QtCore.QTimer.singleShot(1500, deferred)
