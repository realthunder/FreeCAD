"""What an idle refinement COSTS (docs/RenderEngine.md sec 3.5).

Runs inside a FreeCAD GUI session, and only means anything on a REAL
GPU and in an OPTIMIZED build:

    FC_SWAP_INTERVAL=0 FreeCAD scripts/accum_cost_probe.py

Every other probe in this series asks whether the refinement is
CORRECT. None asks what it costs, and it is not free: while a
refinement runs the view keeps ASKING for frames. `submitTemporalAccum`
sets `animatedFrame` until the sample budget is reached, so the engine
schedules the next sample itself -- nothing else would, the scene being
unchanged is the whole premise -- and at the budget it stops and the
view goes quiet holding the converged image. So the feature turns a
parked view into N more rendered frames, and the audit fixes made each
one dearer still: the AO chain, the mirrored scene and the media
intervals used to be reused across a refinement and now re-render per
sample. That is the price of the correctness the other probes measured.

! What a parked frame is NOT is free. There is no whole-frame idle skip
here: a redraw of a static view still runs the frame, it just hits
every cache (shadow map, AO chain, mirror, media intervals) instead of
re-rendering them. So the "static" leg below is a floor made of cached
frames, not of nothing, and the difference between it and a refinement
is the marginal cost of re-rendering what a refinement invalidates.

Three legs per configuration, all at the same pumped frame count:

  static      a parked view, feature off: full frames, every cache hit
  refine      the same view carried to N samples
  interactive nudging the camera every frame, which is what an
              ordinary moving frame costs

and three numbers out of them. The absolute per-sample frame time; the
marginal cost over a cached static frame; and the ratio of a sample to
an interactive frame -- that last is the one that generalises off this
machine and off this build type, because a refinement sample and an
interactive frame are both full frames and any per-frame overhead the
build adds lands on both.

! FC_SWAP_INTERVAL=0 is required, not optional. Vsync makes every leg
cost 16.67 ms and puts the entire difference in the swap wait; ~59.5
fps in the per-frame numbers is the tell that it was not set.

! Two clocks, because either alone is unconvincing. Free-running wall
time is the primary. The serialized leg calls getRenderStats() after
each frame -- its readback cannot complete before the GPU has drawn, so
it serializes the two halves; it is noisy in absolute terms and useless
as one, but it cancels from a difference and must agree on the ratio.

! The first measured configuration is contaminated even after a warm-up
frame or two (shader compiles land wherever they land), so a whole
warm-up REFINEMENT is burned before any leg is timed.

MEASURED 2026-08-21 -- RTX 3070 Ti under Mesa d3d12, OPTIMIZED build
(conda-relwithdebinfo-801), 1498x703, 1.05M geometry pixels, 32 samples,
best of 3.

    config      static/frame  refine/SAMPLE  interactive/frame  ratio
    plain          10.92         11.85            11.99          0.99
    AO             11.61         13.25            12.58          1.05
    mirror         15.49         11.85            12.27          0.97
    AO+mirror      13.86         12.15            12.40          0.98

**A refinement sample costs the same as an ordinary moving frame** --
every ratio is 1.0 within noise. So a 32-sample refinement is about
380-420 ms of work after the camera stops, roughly 31 interactive
frames. That is the number the sample-count dial should be read
against: 32 samples is about four tenths of a second on this class of
GPU, 16 would be two, 64 still under a second.

What is NOT resolvable here: what AO or the mirror add PER SAMPLE.
Free-running says +0.30 ms, serialized says -0.93 ms, and both sit
under a ~1 ms noise floor on a ~12 ms frame. Two zeroes, not a
disagreement -- the frame is bound by Qt/Coin composite and per-frame
CPU, so these passes hide underneath it. That BOUNDS their cost (under
about a millisecond each); it does not measure it.

The serialized clock (readback after every frame, forcing the GPU to
finish) puts every leg at 29-43 ms and agrees on the conclusion: a
sample is not dearer than a frame. Its ratios run lower (0.80-0.88 vs
0.98) for a reason worth knowing -- serialization exposes GPU work that
the CPU-bound free-running frame hides, and the INTERACTIVE leg is the
one doing extra GPU work, because moving the camera invalidates
light-space caches a parked refinement keeps. Under the readback an
interactive frame costs 40-43 ms against a refinement sample's 34-35.

! Traps this probe was built around, all of them instrument faults that
produced confident wrong numbers first:

- A GPU READBACK MUST NOT SIT INSIDE THE TIMING. getRenderStats() costs
  ~15-37 ms here. The first version used it as the loop's own
  while-condition, so the convergence check cost more than the frames it
  was waiting for -- about 37% of the total it reported.
- A PUMP IS NOT A FRAME, and the ratio is not even constant. Unpolled,
  one redraw()+updateGui() drains exactly one engine frame; with a
  stall in the loop, 7 pumps drained all 32 samples, because the stall
  lets the event loop service the redraws the accumulation schedules for
  itself. Dividing by pumps once reported a sample at 5x an interactive
  frame; calibrating the pump count WITH polling and replaying it
  without stopped the run at 7 samples of 32. Pump exactly SAMPLES
  times.
- TWO NEAR-ZERO NUMBERS ARE NOT A DISAGREEMENT. Comparing the clocks'
  sub-millisecond deltas as a ratio printed DISAGREE for what was
  simply "neither can see it". A null result reported as a conflict is
  worse than no result.
"""
import os
import sys
import time

import FreeCAD
import FreeCADGui
from PySide6 import QtCore

OUT = os.environ.get("AC_OUT", "/tmp/accum-cost")
SAMPLES = int(os.environ.get("AC_SAMPLES", "32"))
REPEATS = int(os.environ.get("AC_REPEATS", "3"))
RESULT = os.path.join(OUT, "result.txt")

_lines = []
_cam = {}


def say(msg):
    _lines.append(msg)
    FreeCAD.Console.PrintMessage("[ac] %s\n" % msg)


def pump(n=8):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()


def settle(rounds=10):
    for _ in range(rounds):
        time.sleep(0.05)
        pump(3)


def stats(view):
    try:
        return view.getRenderStats()
    except Exception:
        return {}


def samples(view):
    return stats(view).get("temporalSamples", -1)


def pin(view, dx=0.0, dy=0.0, dz=0.0):
    cam = view.getCameraNode()
    cam.position.setValue(_cam["pos"][0] + dx, _cam["pos"][1] + dy,
                          _cam["pos"][2] + dz)
    cam.focalDistance.setValue(_cam["focal"])


def restart(view):
    """Break the parked state so a fresh refinement begins.

    Nudge away and back: any camera move makes staticFrame false, which
    drops the accumulated history, and returning to the pinned camera
    means the samples that follow are all of the same picture.
    """
    pin(view, dx=9.0, dy=-7.0)
    pump(2)
    pin(view)
    pump(1)


def prop(view, name, value, kind="App::PropertyBool"):
    """Set a render knob on the VIEW, which is what the frame reads.

    ! Never by preference: once a view exists the property decides, and
    writing the preference alone is silent and inert.
    """
    full = "Render_" + name
    if not hasattr(view, full):
        view.addProperty(kind, full)
    setattr(view, full, value)


def time_frames(view, n, nudge=False, serialize=False):
    """Wall-clock for n pumped frames. Returns seconds."""
    t0 = time.perf_counter()
    for i in range(n):
        if nudge:
            # A sub-degree wobble: enough that staticFrame is false and
            # the frame is fully drawn, small enough that it is the same
            # picture and the same visibility.
            pin(view, dx=0.35 * ((i % 2) * 2 - 1))
        view.redraw()
        FreeCADGui.updateGui()
        if serialize:
            stats(view)
    return time.perf_counter() - t0


def time_refine(view, serialize=False):
    """Wall-clock for a refinement, with NO readback inside the timing.

    Pump exactly SAMPLES times. Without a stall in the loop one pump
    drains exactly one engine frame, so that is exactly one sample.

    ! It is only 1:1 because nothing in the loop blocks. Measured: with
    a getRenderStats() readback in the loop, 7 pumps drained all 32
    samples -- the stall gives the event loop time to service the
    redraws the accumulation schedules for itself. So the pump-to-frame
    ratio is a property of what else the loop does, which is why an
    earlier version that calibrated the count with polling and then
    replayed it without polling stopped at 7 samples of 32.

    ! And no polling inside the timing, ever: getRenderStats() is a GPU
    readback costing ~15-37 ms here, more than the frame it would be
    waiting on. Read the sample count once, afterwards.
    """
    restart(view)
    t0 = time.perf_counter()
    for _ in range(SAMPLES):
        view.redraw()
        FreeCADGui.updateGui()
        if serialize:
            stats(view)
    dt = time.perf_counter() - t0
    return dt, SAMPLES, samples(view)


def leg(view, name, serialize=False):
    """One configuration: idle floor, a refinement, interactive frames."""
    best = {}
    for _ in range(REPEATS):
        prop(view, "TemporalAccum", False)
        pin(view)
        settle(6)
        t_static = time_frames(view, SAMPLES, serialize=serialize)
        t_inter = time_frames(view, SAMPLES, nudge=True, serialize=serialize)
        pin(view)
        settle(4)
        prop(view, "TemporalAccum", True)
        t_ref, frames, got = time_refine(view, serialize=serialize)
        prop(view, "TemporalAccum", False)
        pump(2)
        # Minimum over repeats: frame time has a hard floor and a long
        # tail (compositor hiccups, other processes), so the minimum is
        # the stable statistic here, not the mean.
        if not best or t_ref < best["refine"]:
            best = {"static": t_static, "inter": t_inter,
                    "refine": t_ref, "frames": frames, "got": got}
    n = float(SAMPLES)
    # Absolute: what one SAMPLE costs.
    #
    # ! Divide by samples reached, NOT by pumped frames. The refinement
    # schedules its own redraws (animatedFrame), so a single
    # redraw()+updateGui() drains several engine frames -- measured
    # here, 32 samples arrive in about 9 pumps. Dividing by pumps gives
    # ms-per-pump, which looked like a sample costing 5x an interactive
    # frame when it costs about 1.5x. The static and interactive legs
    # do not self-schedule, so for those a pump IS a frame.
    abs_sample = best["refine"] / max(1.0, float(best["got"]))
    abs_static = best["static"] / n
    abs_inter = best["inter"] / n
    # Marginal: what the refinement adds over redrawing a parked view,
    # which is what the cache invalidation per sample actually buys.
    marginal = abs_sample - abs_static
    say("%-9s: ms  static/frame %6.2f  refine/SAMPLE %6.2f  "
        "interactive/frame %6.2f  (%d pumps -> %d samples)%s"
        % (name, abs_static * 1e3, abs_sample * 1e3, abs_inter * 1e3,
           best["frames"], best["got"],
           " [serialized]" if serialize else ""))
    say("%-9s: marginal over a cached static frame %+6.2f ms, "
        "sample/interactive ratio %.2f"
        % (name, marginal * 1e3,
           abs_sample / abs_inter if abs_inter > 0 else float("nan")))
    return {"name": name, "abs_sample": abs_sample,
            "abs_static": abs_static, "abs_inter": abs_inter,
            "marginal": marginal, "refine": best["refine"],
            "got": best["got"]}


def build_scene(doc):
    """Ordinary CAD-looking geometry: enough triangles that a frame is
    not measuring an empty screen, no medium, nothing exotic."""
    for ix in range(3):
        for iy in range(3):
            plate = doc.addObject("Part::Box", "P%d%d" % (ix, iy))
            plate.Length, plate.Width, plate.Height = 60.0, 60.0, 10.0
            plate.Placement.Base = FreeCAD.Vector(ix * 70.0, iy * 70.0, 0.0)
            tools = []
            for k in range(4):
                c = doc.addObject("Part::Cylinder", "C%d%d%d" % (ix, iy, k))
                c.Radius = 6.0
                c.Height = 30.0
                c.Placement.Base = FreeCAD.Vector(
                    ix * 70.0 + 15.0 + (k % 2) * 30.0,
                    iy * 70.0 + 15.0 + (k // 2) * 30.0, -5.0)
                tools.append(c)
            fuse = doc.addObject("Part::MultiFuse", "F%d%d" % (ix, iy))
            fuse.Shapes = tools
            cut = doc.addObject("Part::Cut", "Cut%d%d" % (ix, iy))
            cut.Base = plate
            cut.Tool = fuse
    doc.recompute()
    for obj in doc.Objects:
        vo = getattr(obj, "ViewObject", None)
        if vo is None or not vo.Visibility:
            continue
        try:
            vo.DisplayMode = "Shaded"
        except Exception:
            pass
        vo.ShapeColor = (0.72, 0.74, 0.78)


def main():
    os.makedirs(OUT, exist_ok=True)
    if os.environ.get("FC_SWAP_INTERVAL") != "0":
        say("!! FC_SWAP_INTERVAL is not 0 -- every leg will read about "
            "16.7 ms per frame and the numbers below are vsync, not cost")
    v = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    v.SetInt("RenderCache", 3)
    v.SetBool("ShowNaviCube", False)
    v.SetInt("AntiAliasing", 3)
    r = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    r.SetString("Type", "bgfx - OpenGL")
    r.SetInt("OutputTransform", 1)
    r.SetInt("DebugViewMode", 0)

    doc = FreeCAD.newDocument("AccumCost")
    build_scene(doc)
    view = FreeCADGui.ActiveDocument.ActiveView
    for name, val in (("PBR", True), ("PBREnvBackground", True),
                      ("Light", True), ("Matcap", False),
                      ("AO", False), ("Cavity", False), ("Shadow", False),
                      ("Bloom", False), ("Volumetric", False),
                      ("GroundReflection", False),
                      ("TemporalAccum", False)):
        prop(view, name, val)
    prop(view, "TemporalAccumSamples", SAMPLES, "App::PropertyInteger")

    view.setCameraType("Perspective")
    view.viewAxonometric()
    view.fitAll()
    settle()
    cam = view.getCameraNode()
    _cam["pos"] = tuple(cam.position.getValue().getValue())
    _cam["focal"] = cam.focalDistance.getValue()
    st = stats(view)
    say("scene    : %dx%d, %d geometry pixels, %d samples per refinement, "
        "best of %d" % (st.get("width", -1), st.get("height", -1),
                        st.get("geometryPixels", -1), SAMPLES, REPEATS))

    # Burn a whole refinement before timing anything: a warm-up frame or
    # two is not enough, shader compiles land wherever they land and
    # they bend whichever configuration runs first.
    prop(view, "TemporalAccum", True)
    time_refine(view)
    prop(view, "TemporalAccum", False)
    pump(4)
    say("warm-up  : one full refinement burned before timing")

    configs = (
        ("plain", {}),
        ("AO", {"AO": True}),
        ("mirror", {"GroundReflection": True}),
        ("AO+mirror", {"AO": True, "GroundReflection": True}),
    )
    rows = []
    for name, cfg in configs:
        for k in ("AO", "GroundReflection"):
            prop(view, k, bool(cfg.get(k, False)))
        pump(4)
        rows.append(leg(view, name))
    # The cross-check clock, on the two configurations that bracket the
    # range.
    #
    # ! It must agree on the DELTA between two configurations, NOT on
    # the ratio. The readback attaches per pump, and a refinement pump
    # drains several frames while a static or interactive pump is one,
    # so the tax lands unevenly across the legs and no ratio taken
    # through it means anything. It does cancel from a difference
    # between two configurations measured the same way, which is
    # exactly what a cross-check needs: both clocks should say the same
    # thing about what AO and the mirror ADD to a sample.
    say("-- serialized cross-check (readback forces the GPU to finish) --")
    ser = []
    for name, cfg in (configs[0], configs[3]):
        for k in ("AO", "GroundReflection"):
            prop(view, k, bool(cfg.get(k, False)))
        pump(4)
        ser.append(leg(view, name, serialize=True))

    say("")
    say("SUMMARY (a refinement is %d samples)" % SAMPLES)
    for row in rows:
        # Normalised to a FULL refinement rather than quoting the timed
        # run directly: the pump count is calibrated in a pass that
        # does poll, so a timed run occasionally lands a sample or two
        # short, and per-sample x SAMPLES is the honest full-refinement
        # figure either way. `got` is reported per leg above.
        say("  %-9s one sample %6.2f ms, full %d-sample refinement "
            "%6.0f ms, = %.1f interactive frames"
            % (row["name"], row["abs_sample"] * 1e3, SAMPLES,
               row["abs_sample"] * SAMPLES * 1e3,
               row["abs_sample"] / row["abs_inter"] * SAMPLES
               if row["abs_inter"] > 0 else float("nan")))
    # What AO+mirror ADDS to one sample, in each clock. The readback
    # cancels from this difference, so the two must agree.
    free_delta = rows[3]["abs_sample"] - rows[0]["abs_sample"]
    ser_delta = ser[1]["abs_sample"] - ser[0]["abs_sample"]
    # A noise floor, below which "they disagree" is meaningless: two
    # sub-millisecond numbers on a ~12 ms frame are both saying zero,
    # and the ratio of two noise values says nothing at all. Comparing
    # them as if it did is how a null result gets read as a conflict.
    floor = 0.001 * max(rows[3]["abs_sample"], 1e-9) * 1000.0
    floor = max(1.0e-3, 0.08 * rows[3]["abs_sample"])
    both_null = abs(free_delta) < floor and abs(ser_delta) < floor
    scale = max(abs(free_delta), abs(ser_delta), 1e-9)
    agree = abs(free_delta - ser_delta) / scale < 0.35
    if both_null:
        say("clocks   : what AO+mirror adds to one sample -- free-running "
            "%+.2f ms, serialized %+.2f ms; both under the %.2f ms noise "
            "floor, so the passes' own cost is NOT RESOLVABLE here (not a "
            "disagreement -- two zeroes)"
            % (free_delta * 1e3, ser_delta * 1e3, floor * 1e3))
    else:
        say("clocks   : what AO+mirror adds to one sample -- free-running "
            "%+.2f ms vs serialized %+.2f ms: %s"
            % (free_delta * 1e3, ser_delta * 1e3,
               "AGREE" if agree else "DISAGREE, treat the absolutes as "
               "provisional"))
    say("ratio    : one sample / one interactive frame = %.2f "
        "(free-running; the transferable number)"
        % (rows[3]["abs_sample"] / rows[3]["abs_inter"]
           if rows[3]["abs_inter"] > 0 else float("nan")))
    # A CPU-bound frame is the other way these numbers go quiet, and it
    # announces itself: the GPU work of AO plus a second full scene
    # render cannot really be free.
    if both_null:
        say("note     : the frame is bound by something other than these "
            "passes (Qt/Coin composite and per-frame CPU), so their cost "
            "sits under the floor. That bounds them -- it does not "
            "measure them.")
    with open(RESULT, "w") as fh:
        fh.write("\n".join(_lines) + "\n")
        fh.write("DONE\n")


def run():
    try:
        main()
    except Exception:
        import traceback
        with open(RESULT, "w") as fh:
            fh.write("\n".join(_lines) + "\n")
            traceback.print_exc(file=fh)
            fh.write("\nABORT\n")
        traceback.print_exc()
    finally:
        sys.stdout.flush()
        os._exit(0)


QtCore.QTimer.singleShot(1500, run)
