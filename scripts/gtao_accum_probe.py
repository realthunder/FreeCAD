"""GTAO under idle temporal accumulation (docs/RenderEngine.md sec 3.5).

Runs inside a FreeCAD GUI session:

    FreeCAD scripts/gtao_accum_probe.py

The sibling probe, scripts/temporal_accum_probe.py, deliberately states
AO OFF: its Siemens star has no concave contact anywhere, so it could
not have shown AO even if AO had worked, and at the time AO was the one
part of the frame the accumulation could not converge.  This probe is
that missing measurement.

Both AO passes are stochastic estimators -- GTAO marches horizon
slices along directions from a low-discrepancy sequence, the classic
pass rotates a fixed hemisphere kernel by a tiled noise texture -- so
both carry per-pixel noise by construction.  Averaging frames is
exactly how that noise is meant to disappear, but only if successive
frames DIFFER, and two things stopped them:

  - the AO chain is cached against the camera, and the accumulation
    jitters the camera AFTER that hash is taken, so every sample of a
    refinement scored a cache hit and the chain was skipped outright
    (this half is shared by both passes);
  - the noise itself is pinned to screen position -- GTAO at XeGTAO
    temporal index 0, the classic pass at a 4x4 texture tiled by
    fragment coordinate -- so even a re-render laid down the same
    pattern.

Both are fixed for both methods, and the probe measures the AO buffer
itself -- render debug view mode 3, the AO term alone -- because that
is where the defect lives.  Measurement 2 is the discriminator: on the
pre-fix engine it reads EXACTLY zero by construction, since a buffer
that is never re-rendered is averaged with itself.  Measurement 1
checks the other side of the same gate -- an idle view WITHOUT the
feature must still hold its cache, which is the same reading of zero,
wanted.

Everything from measurement 1 to 4 runs once per AO method.  The
scene is a drilled plate: concave occlusion and nothing else.
"""
import os
import sys
import time

import numpy as np
from PIL import Image

import FreeCAD
import FreeCADGui
from PySide6 import QtCore

OUT = os.environ.get("GA_OUT", "/tmp/gtao-probe")
SAMPLES = int(os.environ.get("GA_SAMPLES", "32"))
SS = int(os.environ.get("GA_SS", "3"))          # reference supersample factor
RESULT = os.path.join(OUT, "result.txt")

_lines = []
_cam = {}


def say(msg):
    _lines.append(msg)
    FreeCAD.Console.PrintMessage("[ga] %s\n" % msg)


def pump(n=8):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()


def settle(rounds=12):
    for _ in range(rounds):
        time.sleep(0.05)
        pump(3)


def grab(view, name):
    path = os.path.join(OUT, name + ".png")
    view.saveRenderDump(path, metadata=False)
    return path


def samples(view):
    """Jittered samples in the history as of the last frame.

    Without this, "converged" and "never engaged" are the same
    observation -- an image that has stopped changing.
    """
    try:
        return view.getRenderStats().get("temporalSamples", -1)
    except Exception:
        return -1


def load(path):
    img = Image.open(path).convert("RGB")
    return np.asarray(img).astype(np.float64)


def rms(a, b):
    return float(np.sqrt(np.mean((a - b) ** 2)))


def box3(a):
    """3x3 box blur, edges replicated."""
    p = np.pad(a, ((1, 1), (1, 1), (0, 0)), mode="edge")
    out = np.zeros_like(a)
    for dy in range(3):
        for dx in range(3):
            out += p[dy:dy + a.shape[0], dx:dx + a.shape[1]]
    return out / 9.0


def hf(a, mask):
    """High-frequency energy over the masked pixels.

    GTAO's error is white-ish noise riding on a term that is smooth
    almost everywhere -- occlusion follows geometry, and geometry does
    not change per pixel.  So the residual against a local mean IS the
    noise, near enough, and it is what the accumulation is meant to
    drive down.
    """
    r = a - box3(a)
    return float(np.sqrt(np.mean(r[mask] ** 2)))


def build_scene(doc):
    """A drilled plate: blind pockets, which are pure concave occlusion.

    Pocket floors and walls sit in their own shadow and the corner
    where a wall meets a floor is the strongest occlusion in the
    frame -- the thing AO exists to draw.  Blind, not through: a
    through hole would show the background at the bottom of every
    pocket and replace occlusion with a silhouette.
    """
    plate = doc.addObject("Part::Box", "Plate")
    plate.Length, plate.Width, plate.Height = 120.0, 120.0, 12.0
    tools = []
    for ix in range(4):
        for iy in range(4):
            c = doc.addObject("Part::Cylinder", "Pocket%d%d" % (ix, iy))
            c.Radius = 9.0
            c.Height = 9.0            # base at z=4 in a 12-thick plate
            c.Placement.Base = FreeCAD.Vector(18.0 + ix * 28.0,
                                              18.0 + iy * 28.0, 4.0)
            tools.append(c)
    fuse = doc.addObject("Part::MultiFuse", "Tools")
    fuse.Shapes = tools
    cut = doc.addObject("Part::Cut", "Pocketed")
    cut.Base = plate
    cut.Tool = fuse
    doc.recompute()
    for obj in doc.Objects:
        vo = getattr(obj, "ViewObject", None)
        if vo is None or not vo.Visibility:
            continue
        # A plain bright dielectric: AO is an ambient term, so a dark or
        # metallic surface would hide the signal being measured.
        vo.ShapeColor = (0.82, 0.82, 0.84)
        for prop, val in (("Render_Metallic", 0.0),
                          ("Render_Roughness", 0.55)):
            if not hasattr(vo, prop):
                vo.addProperty("App::PropertyFloat", prop)
            setattr(vo, prop, val)


def state_prefs():
    """State every scene-wide render preference this probe depends on.

    Inheriting them is how a probe ends up measuring somebody else's
    matcap.  AO in particular defaults OFF, so a probe that does not
    say so measures a frame with no AO in it and reports a clean PASS
    for a chain that never ran.
    """
    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)          # the engine only runs in mode 3
    view.SetBool("ShowNaviCube", False)    # cube pixels have faked a PASS before
    view.SetInt("AntiAliasing", 3)         # 4x MSAA underneath, as in normal use
    r = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    r.SetString("Type", "bgfx - OpenGL")
    r.SetBool("TemporalAccum", False)      # the view property drives it below
    r.SetInt("TemporalAccumSamples", SAMPLES)
    r.SetBool("AO", True)                  # THE point of this probe
    r.SetInt("AOMethod", 1)                # both are measured; 1 = GTAO, 0 = classic
    # ! This preference alone decides nothing once a view exists -- see
    # set_method() below.
    r.SetFloat("AOIntensity", 1.0)
    r.SetFloat("AORadius", 0.0)            # 0 = automatic, a fraction of scene size
    r.SetFloat("AOResolution", 1.0)        # full res: no upscale blur to hide noise
    r.SetBool("Matcap", False)             # else one flat material for everything
    r.SetBool("Bloom", False)
    r.SetBool("Volumetric", False)
    r.SetBool("Shadow", False)             # a second stochastic term would confound
    r.SetBool("Cavity", False)             # composes into the same darkening
    r.SetInt("OutputTransform", 1)         # sRGB on the way out
    r.SetInt("DebugViewMode", 0)
    say("prefs    : cache 3, MSAA 4x, GTAO on at full res, intensity 1.0, "
        "auto radius; shadow/cavity/bloom/matcap off; samples %d, "
        "reference %dx" % (SAMPLES, SS))
    return r


def debug_mode(prefs, mode):
    """Render debug view mode.

    ! A global RenderParam, NOT a view property.  The switches were
    deliberately kept off the view (only the custom shader parameters
    live there), so a probe that adds a RenderDebug_ViewMode property
    and sets it gets no error and no effect -- it measures the beauty
    frame while believing it is looking at the AO term.
    """
    prefs.SetInt("DebugViewMode", mode)
    pump(4)


def set_method(view, prefs, method):
    """Select the AO pass -- on the VIEW PROPERTY, not just the preference.

    ! `Render_AOMethod` is a per-view property seeded from the
    preference when the view is created, and from then on the property
    is what the frame reads.  Writing only the preference mid-run is
    silent and inert: measured 2026-08-21, two legs set that way came
    out rms 0.0000 apart, on screen and through saveImage alike, so a
    two-method probe measured ONE method twice and reported it as a
    comparison.  Write both, and let the property be the one that
    matters.
    """
    prefs.SetInt("AOMethod", method)
    if hasattr(view, "Render_AOMethod"):
        view.Render_AOMethod = method
    else:
        say("!! the view has no Render_AOMethod -- the preference alone "
            "will NOT switch the pass")
    pump(4)


def pin(view):
    """Re-pin the camera fitAll placed at setup.

    Never fitAll for an A/B: it restages under the capture and
    reframes what is being compared (render A/B harness).  Framing
    once and then re-pinning the numbers is the safe half of it.
    """
    cam = view.getCameraNode()
    cam.position.setValue(*_cam["pos"])
    cam.focalDistance.setValue(_cam["focal"])


def main():
    os.makedirs(OUT, exist_ok=True)
    prefs = state_prefs()
    doc = FreeCAD.newDocument("GtaoAccum")
    build_scene(doc)

    view = FreeCADGui.ActiveDocument.ActiveView
    for prop, val in (("Render_PBR", True),
                      ("Render_PBREnvBackground", True)):
        if not hasattr(view, prop):
            view.addProperty("App::PropertyBool", prop)
        setattr(view, prop, val)
    for prop, kind in (("Render_TemporalAccum", "App::PropertyBool"),
                       ("Render_TemporalAccumSamples",
                        "App::PropertyInteger"),
                       ("Render_AO", "App::PropertyBool")):
        if not hasattr(view, prop):
            view.addProperty(kind, prop)
    view.Render_TemporalAccumSamples = SAMPLES
    view.Render_AO = True

    view.setCameraType("Perspective")
    view.viewAxonometric()
    view.fitAll()
    settle()
    cam = view.getCameraNode()
    _cam["pos"] = tuple(cam.position.getValue().getValue())
    _cam["focal"] = cam.focalDistance.getValue()
    say("camera   : framed once by fitAll at (%.1f, %.1f, %.1f) focal "
        "%.1f, then pinned" % (_cam["pos"] + (_cam["focal"],)))

    # Geometry mask, from debug view mode 1 (linearized depth): in the
    # AO term the empty background reads 0, which is also what a deep
    # pocket corner approaches, so thresholding the AO term itself
    # cannot separate them.  A depth image can: the background is one
    # constant, and the frame corner is background by construction
    # under a fitAll framing.
    debug_mode(prefs, 1)
    pin(view)
    settle()
    depth = load(grab(view, "depth"))
    bg = depth[0, 0, :]
    mask = np.repeat((np.abs(depth - bg).max(axis=2) > 1.5)[:, :, None],
                     3, axis=2)
    say("mask     : geometry is %.1f%% of the frame (depth mode 1, "
        "background = %s)" % (100.0 * mask[:, :, 0].mean(),
                              tuple(int(x) for x in bg)))

    # Mode 3: the AO term alone.  Written raw -- the output colour
    # transform stands down for every debug mode -- which is why
    # everything below averages these images directly rather than
    # sRGB-decoding first.  The shaded frame is measured at the end,
    # where the decode does apply.
    debug_mode(prefs, 3)
    pin(view)
    settle()

    # ---- 1-4, once per AO method -----------------------------------
    # The classic pass and GTAO are different estimators with different
    # noise, and the fix has a shared half (the cache) and a
    # per-shader half (the pattern).  Measuring only the default would
    # leave the other shader's half unmeasured.
    h, w = 0, 0
    verdicts = []
    for method, mname in ((1, "GTAO"), (0, "classic SSAO")):
        set_method(view, prefs, method)
        pin(view)
        settle()

        # ---- 1. control: an idle view WITHOUT the feature holds its
        # cache.  Same reading as the pre-fix defect -- zero -- but
        # here it is the wanted answer: nobody is refining, so nothing
        # should pay to re-render.
        view.Render_TemporalAccum = False
        prefs.SetBool("TemporalAccum", False)
        pin(view)
        settle()
        single_p = grab(view, "%d-ao-single" % method)
        pump(SAMPLES + 8)
        single_b = grab(view, "%d-ao-single-b" % method)
        single = load(single_p)
        ctrl = rms(single, load(single_b))
        h, w = int(single.shape[0]), int(single.shape[1])
        say("[%s] method   : view property reads %r"
            % (mname, getattr(view, "Render_AOMethod", "<absent>")))
        say("[%s] control  : accum off, %d frames apart, AO term rms "
            "%.4f (must be 0 -- the cache still holds when nobody is "
            "refining)" % (mname, SAMPLES + 8, ctrl))

        # ---- 2. THE discriminator: does the AO buffer move at all?
        # Pre-fix this is 0.0000 by construction: the chain is skipped
        # every sample, so the accumulation averages one buffer with
        # itself.  Nonzero means it now re-renders per sample AND lays
        # down different noise when it does.
        view.Render_TemporalAccum = True
        prefs.SetBool("TemporalAccum", True)
        steps = []
        prev = single
        for k in range(3):
            pump(60)
            cur = load(grab(view, "%d-ao-accum-%d" % (method, k)))
            steps.append((rms(prev, cur), samples(view)))
            prev = cur
        accum = prev
        d_move = rms(single, accum)
        say("[%s] moved    : accumulated AO term vs single-sample rms "
            "%.4f (0 = the chain never re-rendered)" % (mname, d_move))
        say("[%s] converge : per-round step rms/samples %s"
            % (mname, " -> ".join("%.4f@%d" % (d, n) for d, n in steps)))

        # ---- 3. noise: the thing that is supposed to go down
        n_single = hf(single, mask)
        n_accum = hf(accum, mask)
        say("[%s] noise    : high-frequency energy over geometry, "
            "single %.4f -> accumulated %.4f (%.0f%% of it left)"
            % (mname, n_single, n_accum,
               100.0 * n_accum / max(n_single, 1e-9)))

        # ---- 4. right, not merely smoother
        # A blur also lowers measurement 3.  What separates converging
        # from blurring is the direction relative to a genuinely
        # supersampled render of the same camera.
        ref_path = os.path.join(OUT, "%d-ao-ref-%dx.png" % (method, SS))
        pin(view)
        pump(2)
        view.saveImage(ref_path, w * SS, h * SS, "Current")
        big = load(ref_path)[:h * SS, :w * SS]
        # Box-downsample.  No sRGB decode: mode 3 writes the AO term
        # raw, so these values are already linear and averaging them
        # directly is the definition of a supersampled AO pixel.
        ref = big.reshape(h, SS, w, SS, 3).mean(axis=(1, 3))
        Image.fromarray(np.clip(ref, 0, 255).astype(np.uint8)).save(
            os.path.join(OUT, "%d-ao-ref-down.png" % method))
        d_single = rms(single, ref)
        d_accum = rms(accum, ref)
        say("[%s] truth    : AO term vs %dx reference, single %.4f, "
            "accumulated %.4f (lower is closer)"
            % (mname, SS, d_single, d_accum))

        verdicts.append(ctrl == 0.0
                        and steps[-1][1] >= SAMPLES
                        and d_move > 0.5
                        and n_accum < n_single
                        and d_accum < d_single)
    set_method(view, prefs, 1)    # the rest runs at the default method

    # ---- 5. the frame a user actually looks at ----------------------
    # Everything above is the AO buffer in isolation.  Whether it
    # reaches the screen is a separate question -- AO is applied into
    # the ambient term only, so a brightly lit frame can swallow it.
    # Back to the shaded frame, at the default method.
    debug_mode(prefs, 0)
    view.Render_TemporalAccum = False
    prefs.SetBool("TemporalAccum", False)
    pin(view)
    settle()
    b_single = load(grab(view, "beauty-single"))
    view.Render_TemporalAccum = True
    prefs.SetBool("TemporalAccum", True)
    pump(SAMPLES * 6)
    b_accum = load(grab(view, "beauty-accum"))
    n_beauty = samples(view)
    b_ref_path = os.path.join(OUT, "beauty-ref-%dx.png" % SS)
    pin(view)
    pump(2)
    view.saveImage(b_ref_path, w * SS, h * SS, "Current")
    b_big = load(b_ref_path)[:h * SS, :w * SS]
    # ! Average in LINEAR LIGHT here.  The beauty path IS colour
    # managed: the accumulation runs on linear radiance and the sRGB
    # encode happens after it, so box-averaging the encoded PNG would
    # compare "mean of encoded" against "encode of mean" -- and the
    # encode is concave, so they differ worst at exactly the
    # high-contrast edges being measured.
    c = b_big / 255.0
    lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    m = lin.reshape(h, SS, w, SS, 3).mean(axis=(1, 3))
    enc = np.where(m <= 0.0031308, m * 12.92,
                   1.055 * np.maximum(m, 0.0) ** (1.0 / 2.4) - 0.055)
    b_ref = np.clip(enc * 255.0, 0.0, 255.0)
    bd_single = rms(b_single, b_ref)
    bd_accum = rms(b_accum, b_ref)
    say("beauty   : shaded frame vs %dx reference, single %.4f, "
        "accumulated %.4f, at %d samples"
        % (SS, bd_single, bd_accum, n_beauty))

    # ---- 6. reproducible: the index is a SAMPLE number --------------
    # Advancing the noise per sample is only safe because a sample
    # number is reproducible.  A frame counter would have made the
    # converged image depend on how many times the view happened to
    # redraw -- which the render-verify goldens would catch as drift.
    cam.position.setValue(_cam["pos"][0] * 0.6, _cam["pos"][1] * 1.6,
                          _cam["pos"][2] * 0.7)
    pump(SAMPLES * 4)
    pin(view)
    pump(SAMPLES * 6)
    b_again = load(grab(view, "beauty-again"))
    repeat = rms(b_accum, b_again)
    say("repeat   : converged, left, returned and re-converged, rms "
        "%.4f from the first converged frame" % repeat)

    ok = (all(verdicts)                    # both AO methods, 1-4
          and bd_accum < bd_single          # the shaded frame improved
          and repeat < 0.5 * bd_single)     # reproducible, not frame-count-bound
    say("VERDICT  : %s" % ("PASS" if ok else "FAIL"))
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
        # An in-process quit is always defeatable; the launcher's
        # timeout is the real backstop.
        sys.stdout.flush()
        os._exit(0)


# ! Deferred, not inline.  Work run straight off the startup script
# shares the frame the GUI is still assembling: redraws do not really
# redraw, and a baseline taken there reads as though nothing changed.
QtCore.QTimer.singleShot(1500, run)
