# 3D engine backend benchmark.
#
# What it ranks backends by is the ENGINE'S OWN accounting, not wall
# clock. docs/RenderEngine.md 7.10 records why: wall clock around
# redraw() put OpenGL, Vulkan and Direct3D 11 inside 1.4% of each other
# on this box, because the renderer owned 7.5 ms of a 20 ms frame and
# the backend's own share of that was 1.6 ms -- the Qt event loop, the
# paint scheduling and this script's own polling owned the rest. A
# harness that reports its own limit ranks nothing.
#
# So RenderParams::DebugTiming is turned on and the engine's per-frame
# report is read back through a console observer:
#
#   render frame: ... submit X ms gpu Y ms ... cpu ours Z ms
#                 (bgfx::frame W ms) outside V ms
#   render cpu phases (ms/frame): pre | cull | submitloop | post | ...
#
# `submit` (bgfx's render thread issuing draw calls), `gpu` and
# `submitloop` (our per-draw C++) are the terms a backend actually
# moves. `ours`, `outside` and `frame` are printed beside them so a
# reading that moves only the harness is visible as such.
#
# The scene is a document, because the question is about real
# assemblies: FC_BENCH_DOC=<file.FCStd>. With none given it falls back
# to the MaterialX chess set the golden render test uses -- 15 shapes,
# which 7.10 also records as too small to separate backends. Import a
# STEP assembly with scripts/render-bench-import.py and point at that.
#
# Progressive load and the fidelity ladder are turned OFF (see the
# setup below). Both are adaptive, and an adaptive scene is not the
# same scene twice, let alone across two backends.
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
#
# Set FC_BGFX_NO_VSYNC for every leg or each returns the refresh
# interval whatever the scene costs. Name the backend with
# FC_RENDER_BACKEND; the non-GL ones need their own opt-in env var
# (FC_BGFX_VULKAN / FC_BGFX_D3D11 / FC_BGFX_D3D12 / FC_BGFX_METAL),
# and none of them reaches the screen -- they render and capture, and
# Coin draws the viewport. That does not affect what is measured here.
#
# One leg, on Windows:
#
#   FC_BGFX_NO_VSYNC=1 FC_BGFX_D3D11=1 \
#   FC_RENDER_BACKEND="bgfx - Direct3D11" \
#   FC_BENCH_DOC=D:/works/sw/bench/MiSTer.FCStd \
#   FC_BENCH_OUT=bench.txt \
#       .conda/run.cmd build/win-relwithdebinfo-801/bin/FreeCAD.exe \
#       scripts/render-bench.py
#
# FC_BENCH_OUT is not optional there: FreeCAD.exe is a GUI-subsystem
# binary with no console attached, so nothing this prints reaches a
# pipe. The file is the only output.
import os
import re
import sys
import time
import traceback

import FreeCAD
import FreeCADGui

BACKEND = os.environ.get("FC_RENDER_BACKEND", "bgfx - OpenGL")
DOC = os.environ.get("FC_BENCH_DOC", "")
FRAMES = int(os.environ.get("FC_BENCH_FRAMES", "200"))
WARMUP = int(os.environ.get("FC_BENCH_WARMUP", "30"))
# The engine reports once a SECOND, so the timed run has to last several
# of those or there is nothing to average. The frame target is a floor,
# not the measure.
MINSEC = float(os.environ.get("FC_BENCH_MIN_SECONDS", "6"))
MAXSEC = float(os.environ.get("FC_BENCH_MAX_SECONDS", "60"))
# WxH the view is resized to before timing, because fill cost scales
# with the pixels. Empty leaves the window as it comes up, which is
# only comparable against another leg that came up the same.
SIZE = os.environ.get("FC_BENCH_SIZE", "1280x720")
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
# A view judged to be in the BACKGROUND gives its render targets back
# after this many ms, and the next frame rebuilds them. Under a
# benchmark that is a rebuild of every sized target between frames --
# measured at ~430 of a 450 ms frame here, which is the whole
# measurement -- because a bench window is not the maximized one and
# because a slow frame is longer than the delay. 0 lets a background
# view keep them.
p.SetInt("BackgroundReleaseDelay", 0)

# Everything adaptive OFF. Each of these decides, per frame and per
# object, how much geometry there is to draw, so with any of them on the
# two legs of an A/B do not draw the same scene -- and one of them
# decides it from a number only some backends report.
#
#   ProgressiveLoad    builds visuals after the load in bounded slices,
#                      so the first frames measure geometry ARRIVING.
#   CoarseTessellation -1 tessellates exact up front (pre-ladder), so no
#                      rung is standing in for a mesh mid-run.
#   CoarseDeferFaces   -1 keeps big shapes off the refine pool: no
#                      bounding-box stand-in swapped in later.
#   LevelTolerance     0 refines everything immediately rather than
#                      leaving distant objects coarse.
#   LevelScale         1 turns off the coarsen-to-fit pass.
#   GpuMemoryBudgetMB  is the one that would silently bias the answer:
#                      0 means "the API's own reported limit", and
#                      Direct3D and Vulkan report one while OpenGL
#                      reports nothing. Left at 0, the D3D and Vulkan
#                      legs downgrade meshes the GL leg keeps, and the
#                      comparison is between two different scenes. A
#                      large value applies no budget on any of them.
#
# FC_BENCH_ADAPTIVE=1 leaves all six at whatever the preferences hold,
# for the A/B that asks what the ladder itself is worth. It is not the
# comparable configuration and no backend table should mix the two.
if not os.environ.get("FC_BENCH_ADAPTIVE"):
    p.SetBool("ProgressiveLoad", False)
    p.SetInt("CoarseTessellation", -1)
    p.SetInt("CoarseDeferFaces", -1)
    p.SetFloat("LevelTolerance", 0.0)
    p.SetFloat("LevelScale", 1.0)
    p.SetInt("GpuMemoryBudgetMB", 1 << 20)
else:
    for name in ("ProgressiveLoad",):
        p.RemBool(name)
    for name in ("CoarseTessellation", "CoarseDeferFaces",
                 "GpuMemoryBudgetMB"):
        p.RemInt(name)
    for name in ("LevelTolerance", "LevelScale"):
        p.RemFloat(name)

view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
for off in ("ShowAxisCross", "ShowNaviCube", "CornerCoordSystem", "ShowFPS"):
    view.SetBool(off, False)


# The engine's report, as printed by reportFrameStats() and the cpu
# phase line beside it (BGFXRendererP.h / BGFXFrame.cpp). Both are
# Base::Console().Message, so a console observer catches them wherever
# the process sends its output -- which on Windows is nowhere a pipe can
# reach, the GUI binary having no console attached.
FRAME_RE = re.compile(
    r"render frame: frames:(?P<frames>\d+) (?P<w>\d+)x(?P<h>\d+) "
    r"frame (?P<frame>-?[\d.]+)ms submit (?P<submit>-?[\d.]+)ms "
    r"gpu (?:(?P<gpu>-?[\d.]+)ms\(n=(?P<gpun>\d+)\)|n/a) \| "
    r"draws (?P<draws>-?[\d.]+) prims (?P<prims>-?[\d.]+).*"
    r"cpu ours (?P<ours>-?[\d.]+)ms \(bgfx::frame (?P<bgfx>-?[\d.]+)ms\) "
    r"outside (?P<outside>-?[\d.]+)ms")
PHASE_RE = re.compile(
    r"render cpu phases \(ms/frame\): pre (?P<pre>-?[\d.]+) \| "
    r"cull (?P<cull>-?[\d.]+) \| submitloop (?P<submitloop>-?[\d.]+) \| "
    r"post (?P<post>-?[\d.]+) ")
# The OTHER half of the CPU frame, from Gui/RenderTiming.cpp: the Coin
# side that builds what the backend then draws. A backend cannot move
# any of it, so a leg whose cost sits here is not measuring a backend --
# which is exactly what a first run on a 17k-object assembly showed.
# Totals over the window in whole ms, so they are divided by the
# window's own frame count, not by the report's.
TIMING_RE = re.compile(
    r"RenderTiming (?P<window>\d+)ms frames=(?P<frames>\d+) "
    r"traverse=(?P<traverse>-?\d+)/\d+ delta=(?P<delta>-?\d+)/\d+ "
    r"flatten=(?P<flatten>-?\d+)/\d+ flattensub=-?\d+/\d+ "
    r"entries=(?P<entries>-?\d+)/\d+ translate=(?P<translate>-?\d+)/\d+ "
    r"backend=(?P<backend>-?\d+)/\d+ submit=(?P<submit2>-?\d+)/\d+"
    r".*? other=(?P<other>-?\d+)")


class Report(object):
    """Frame-weighted mean of the engine's own per-frame report.

    Weighted because the windows are not equal: the report fires once a
    second, so a window holds however many frames fitted in it, and a
    plain mean of the lines would weight a 3-frame window like a
    30-frame one -- which is backwards, since the slow window is the
    interesting one.
    """

    KEYS = ("frame", "submit", "gpu", "ours", "bgfx", "outside", "draws",
            "prims", "pre", "cull", "submitloop", "post")
    COIN = ("traverse", "delta", "flatten", "entries", "translate",
            "backend", "submit2")

    def __init__(self):
        self.on = False
        self.windows = 0
        self.frames = 0
        self.sums = dict((k, 0.0) for k in self.KEYS)
        self.gpuframes = 0
        self.pending = 0     # frames of the window whose phase line is next
        self.width = 0
        self.height = 0
        self.coin = dict((k, 0.0) for k in self.COIN)
        self.coinframes = 0

    def __call__(self, notifier, message, level):
        if not self.on:
            return
        for line in message.splitlines():
            m = FRAME_RE.search(line)
            if m:
                self._frame(m)
                continue
            m = PHASE_RE.search(line)
            if m:
                self._phase(m)
                continue
            m = TIMING_RE.search(line)
            if m:
                self._coin(m)

    def _frame(self, m):
        n = int(m.group("frames"))
        if not n:
            return
        # The first window of the timed run straddles the warm-up: the
        # accumulator resets only when it prints, so whatever was
        # standing in it when the window opened belongs to frames that
        # were not being measured. Drop it.
        self.windows += 1
        self.pending = 0
        if self.windows == 1:
            return
        self.frames += n
        self.pending = n
        self.width = int(m.group("w"))
        self.height = int(m.group("h"))
        for k in ("frame", "submit", "ours", "bgfx", "outside", "draws",
                  "prims"):
            self.sums[k] += float(m.group(k)) * n
        # bgfx's GPU timestamps lag the frame that produced them and
        # repeat, so the engine counts its own samples; weight by those,
        # not by frames, and say n/a rather than a diluted number when a
        # backend reports none.
        if m.group("gpu"):
            gn = int(m.group("gpun")) or n
            self.sums["gpu"] += float(m.group("gpu")) * gn
            self.gpuframes += gn

    def _phase(self, m):
        n = self.pending
        if not n:
            return
        for k in ("pre", "cull", "submitloop", "post"):
            self.sums[k] += float(m.group(k)) * n

    def _coin(self, m):
        # Its own window and its own frame count, published on its own
        # clock: pairing it with the backend report's window would
        # divide one window's cost by another's frames.
        n = int(m.group("frames"))
        if not n:
            return
        self.coinframes += n
        for k in self.COIN:
            self.coin[k] += float(m.group(k))

    def coinmean(self, key):
        return self.coin[key] / self.coinframes if self.coinframes else None

    def mean(self, key):
        if key == "gpu":
            return self.sums["gpu"] / self.gpuframes if self.gpuframes else None
        return self.sums[key] / self.frames if self.frames else None


def fmt(v, spec="%.2f"):
    return "n/a" if v is None else spec % v


def restage_viewport(v):
    """Give every leg the same viewport, or say what it got instead.

    Fill cost scales with the pixels, so two legs at two sizes are not
    a comparison. Condensed from scripts/render_verify.py
    restage_viewport(), including the two reasons it is shaped this
    way: while the subwindow is maximized the QMdiArea owns its
    geometry and resize() on it is simply overridden, and the
    subwindow carries frame and decoration the view does not, so the
    delta is applied and re-measured rather than the size assigned.
    """
    want = tuple(int(x) for x in SIZE.lower().split("x")) if SIZE else None
    if not want or len(want) != 2:
        return tuple(v.getSize())
    from PySide import QtWidgets
    sub = None
    area = FreeCADGui.getMainWindow().findChild(QtWidgets.QMdiArea)
    if area:
        sub = area.activeSubWindow()
    if sub is None:
        say("  !! viewport %dx%d wanted, no MDI subwindow found" % want)
        return tuple(v.getSize())
    if sub.isMaximized():
        sub.showNormal()
    for _ in range(8):
        got = tuple(v.getSize())
        if got == want:
            break
        sub.resize(sub.width() + (want[0] - got[0]),
                   sub.height() + (want[1] - got[1]))
        FreeCADGui.updateGui()
    got = tuple(v.getSize())
    if got != want:
        say("  !! viewport %dx%d wanted, got %dx%d -- legs at two sizes "
            "are not a comparison" % (want + got))
    return got


def open_scene():
    """The document to time, and a 3D view showing it."""
    if DOC.startswith("boxes:"):
        # A synthetic scene built IN SESSION, which is the control for
        # every measurement taken on a restored .FCStd: same drawables,
        # no document restore behind them. It is what showed the ~430ms
        # per-frame `pre` to be about the OPEN and not about the model.
        n = int(DOC.split(":", 1)[1])
        doc = FreeCAD.newDocument("BenchBoxes")
        FreeCADGui.ActiveDocument = FreeCADGui.getDocument(doc.Name)
        t0 = time.perf_counter()
        for i in range(n):
            b = doc.addObject("Part::Box", "Box")
            b.Length = b.Width = b.Height = 10
            b.Placement.Base = FreeCAD.Vector((i % 20) * 15, (i // 20) * 15, 0)
        doc.recompute()
        load_s = time.perf_counter() - t0
    elif DOC:
        t0 = time.perf_counter()
        doc = FreeCAD.openDocument(DOC)
        load_s = time.perf_counter() - t0
    else:
        import ImportGui
        doc = FreeCAD.newDocument("BenchChess")
        FreeCADGui.ActiveDocument = FreeCADGui.getDocument(doc.Name)
        t0 = time.perf_counter()
        ImportGui.insert(GLB, doc.Name)
        ImportGui.insert(MTLX, doc.Name)
        doc.recompute()
        load_s = time.perf_counter() - t0
    gdoc = FreeCADGui.getDocument(doc.Name)
    FreeCADGui.ActiveDocument = gdoc
    v = gdoc.ActiveView
    if v is None:
        v = gdoc.createView("Gui::View3DInventor")
    return doc, v, load_s


def bench():
    doc, v, load_s = open_scene()
    # Before any fit: an animated fit is a nested event loop lasting as
    # long as ten frames, and a camera staged under one is overwritten by
    # the animation still in flight (render-test-chess.py says the same).
    v.setAnimationEnabled(False)

    shapes = sum(1 for o in doc.Objects if o.isDerivedFrom("Part::Feature"))

    # Size first: the camera's aspect and everything screen-space is
    # resolved against the viewport, so a fit staged into a different
    # one frames the model differently.
    restage_viewport(v)
    v.setCameraOrientation((0.4247, 0.1759, 0.3389, 0.8226))
    v.fitAll()
    # Let the deferred shapes settle, or the first timed frames measure
    # geometry ARRIVING rather than drawing. With ProgressiveLoad off
    # there should be none left, which is the point of turning it off.
    v.waitFrameComplete()

    # setCameraOrientation, not the Coin camera node: getCameraNode()
    # hands back a SWIG object and raises "No SWIG wrapped library
    # loaded" without pivy, which this env does not load into the GUI.
    # The view's own setter takes the same four floats.
    base = FreeCAD.Rotation(0.4247, 0.1759, 0.3389, 0.8226)
    step = [0]

    def spin_frame():
        # A small orbit: enough that no frame repeats, small enough
        # that the model stays framed and cost stays comparable.
        r = FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), (step[0] % 360) * 1.0)
        step[0] += 1
        q = r.multiply(base).Q
        v.setCameraOrientation((q[0], q[1], q[2], q[3]))
        v.redraw()
        v.waitFrameComplete()

    for _ in range(WARMUP):
        spin_frame()

    rep = Report()
    FreeCAD.Console.AttachObserver(rep)
    rep.on = True
    t0 = time.perf_counter()
    n = 0
    while True:
        spin_frame()
        n += 1
        elapsed = time.perf_counter() - t0
        if elapsed >= MAXSEC:
            break
        if n >= FRAMES and elapsed >= MINSEC:
            break
    elapsed = time.perf_counter() - t0
    rep.on = False
    FreeCAD.Console.DetachObserver(rep)

    # Whether the frames just timed had anything in them. A benchmark
    # that measures an empty viewport is the same family of mistake as
    # one that measures its own polling, and it is not visible in any
    # timing number -- the first run of this on a 17k-object assembly
    # reported 2 primitives a frame and looked like a result.
    try:
        st = v.getRenderStats()
        pixels = st["geometryPixels"]
        cover = 100.0 * pixels / float(max(1, st["width"] * st["height"]))
    except Exception as exc:
        pixels, cover = -1, 0.0
        say("  !! getRenderStats failed: %s" % exc)

    wall = elapsed * 1000.0 / n
    say("%s" % BACKEND)
    say("  engine  submit %s | gpu %s | submitloop %s | bgfx::frame %s"
        % (fmt(rep.mean("submit")), fmt(rep.mean("gpu")),
           fmt(rep.mean("submitloop")), fmt(rep.mean("bgfx"))))
    say("  ours    pre %s | cull %s | post %s | ours %s"
        % (fmt(rep.mean("pre")), fmt(rep.mean("cull")),
           fmt(rep.mean("post")), fmt(rep.mean("ours"))))
    say("  coin    traverse %s | delta %s | flatten %s | entries %s | "
        "translate %s | backend %s"
        % (fmt(rep.coinmean("traverse")), fmt(rep.coinmean("delta")),
           fmt(rep.coinmean("flatten")), fmt(rep.coinmean("entries")),
           fmt(rep.coinmean("translate")), fmt(rep.coinmean("backend"))))
    say("  frame   outside %s | frame %s | wall %.2f  (all ms)"
        % (fmt(rep.mean("outside")), fmt(rep.mean("frame")), wall))
    say("  scene   %dx%d draws %s prims %s | geometry %d px (%.1f%%) | "
        "objects %d shapes %d"
        % (rep.width, rep.height, fmt(rep.mean("draws"), "%.0f"),
           fmt(rep.mean("prims"), "%.0f"), pixels, cover,
           len(doc.Objects), shapes))
    say("  run     frames %d windows %d load %.1fs %s"
        % (n, max(0, rep.windows - 1), load_s,
           os.path.basename(DOC) if DOC else "chess"))
    if rep.frames == 0:
        say("  !! no engine report was captured: is DebugTiming reaching "
            "the console, and did the run last more than a second?")
    if pixels == 0:
        say("  !! the timed frames drew NO geometry -- whatever these "
            "numbers rank, it is not this model")


def deferred():
    try:
        bench()
    except Exception:
        say("FAILED %s\n%s" % (BACKEND, traceback.format_exc()))
    # quit() on its own leaves the process up (measured: the window
    # stays open and the app keeps running), and a leftover leg holds
    # its GPU memory while the next one measures. Close the documents
    # first so no save prompt can appear, then the window, and quit
    # from the next turn of the loop.
    from PySide import QtCore
    for name in list(FreeCAD.listDocuments()):
        try:
            FreeCAD.closeDocument(name)
        except Exception:
            pass
    mw = FreeCADGui.getMainWindow()
    if mw:
        mw.close()
    QtCore.QTimer.singleShot(0, QtCore.QCoreApplication.quit)


from PySide import QtCore  # noqa: E402  (after the parameter setup above)

QtCore.QTimer.singleShot(1500, deferred)
