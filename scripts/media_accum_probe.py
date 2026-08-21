"""Media interval depths under idle temporal accumulation
(docs/RenderEngine.md sec 3.5).

Runs inside a FreeCAD GUI session:

    FreeCAD scripts/media_accum_probe.py

The fourth of the audit probes, and the one that closes the loose end
the other three left.  The water/glass/cloud/fire passes render the
entry/exit depth interval of each medium body into its own pair of
targets, and those targets are cached on `staticFrame` -- which is the
very predicate that says an accumulation may run.  So across a
refinement the cache answers "reuse" for every sample and the interval
holds the depths rendered at the UNJITTERED camera, while the surface
shading that reads them moves underneath.  Same shape as the AO defect
and the mirror one; `e1caf7ae42` fixed all three by keying on the
sample index, but this third target was never measured, because no
probe scene in the series has media in it.  This one does.

Why the interval matters to the picture, from `fs_fc_glass.sc`: the
front/back pair gives `thick`, and `thick` drives BOTH the lateral
refraction displacement (`disp = (T.xy - d.xy) * thick`) and the
per-channel Beer-Lambert absorption (`exp(-sigma * thick)`).  So a
frozen interval pins a stale thickness at every pixel, and the places
that hurts are exactly the places thickness jumps: silhouettes, hole
walls, and the step where two glass bodies overlap.  The scene below is
built to have a great many of those.

! Measured the way the mirror was measured, and for the same reason.
The interval targets are sized from the VIEW (`width`/`height` in
BGFXView::updateEffect), and saveImage does not resize the view, so a
supersampled capture still contains a 1x interval and the frame-level
distance to it is NOT ground truth for this pass.  The verdict keys on
what the pass CONTRIBUTES -- glass on minus glass off, differenced in
linear light -- and on that contribution's own high-frequency energy.
A frozen interval cannot converge; it can only soften by the amount its
own resampling jitters.

! The supersampled reference is taken BEFORE the accumulation ever
runs.  Taken after, it would render through the same stale cache and
inherit the defect being measured.

MEASURED, 32 samples, llvmpipe under xvfb, on the scene below (the
glass pass covers 22.18% of the frame), against a build of this tree
with the fix backed out to `mediumRender = !staticFrame`:

                                     frozen    per sample
    contribution hf accum/single      0.862      0.807
    accumulated frame, glass on       2.0335     1.9450
    gain over the glass-off leg      +0.0115    +0.1000
    contribution moved from single    0.01638    0.01986

Two internal controls come out of that pair for free, and both hold:
the glass-OFF leg is identical between the two builds to every digit
(single 2.0001, accumulated 1.6312), and so is the glass-on SINGLE
frame (2.4139).  So the fix is an exact identity both where no medium
exists and at sample 0, which is what keeps it golden-safe.
"""
import os
import sys
import time

import numpy as np
from PIL import Image

import FreeCAD
import FreeCADGui
from PySide6 import QtCore

OUT = os.environ.get("MA_OUT", "/tmp/media-probe")
SAMPLES = int(os.environ.get("MA_SAMPLES", "32"))
SS = int(os.environ.get("MA_SS", "3"))
RESULT = os.path.join(OUT, "result.txt")

_lines = []
_cam = {}
_glass = []
_shape = []


def say(msg):
    _lines.append(msg)
    FreeCAD.Console.PrintMessage("[ma] %s\n" % msg)


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


def stabilise(view):
    """Wait until the captured frame stops changing SIZE.

    ! The window is still laying itself out for a while after the first
    document opens -- measured here, the capture went 608 rows then 582
    a few frames later, which aborted the first run of this probe on a
    shape mismatch rather than on anything about the renderer.  Every
    number below differences two captures, so a size that moves midway
    invalidates the lot.  Wait for two consecutive captures to agree
    before pinning anything.
    """
    last = None
    for i in range(40):
        settle(4)
        shape = load(grab(view, "size-probe")).shape
        if shape == last:
            say("size     : frame stable at %dx%d after %d checks"
                % (shape[1], shape[0], i + 1))
            _shape.append(shape)
            return shape
        last = shape
    raise RuntimeError("frame size never settled (last %s)" % (last,))


def expect(arr, what, shape=None):
    shape = shape or (_shape[0] if _shape else None)
    if shape is not None and arr.shape != shape:
        raise RuntimeError("%s changed size: %s, expected %s"
                           % (what, arr.shape, shape))
    return arr


def samples(view):
    try:
        return view.getRenderStats().get("temporalSamples", -1)
    except Exception:
        return -1


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float64)


def rms(a, b):
    return float(np.sqrt(np.mean((a - b) ** 2)))


def decode(x):
    c = x / 255.0
    return np.where(c <= 0.04045, c / 12.92,
                    ((c + 0.055) / 1.055) ** 2.4)


def encode(v):
    c = np.where(v <= 0.0031308, v * 12.92,
                 1.055 * np.maximum(v, 0.0) ** (1.0 / 2.4) - 0.055)
    return np.clip(c * 255.0, 0.0, 255.0)


def downsample(big, h, w):
    """Box-average IN LINEAR LIGHT -- the engine encodes after averaging."""
    return encode(decode(big).reshape(h, SS, w, SS, 3).mean(axis=(1, 3)))


def box3(a):
    p = np.pad(a, ((1, 1), (1, 1), (0, 0)), mode="edge")
    out = np.zeros_like(a)
    for dy in range(3):
        for dx in range(3):
            out += p[dy:dy + a.shape[0], dx:dx + a.shape[1]]
    return out / 9.0


def shade(vo, color, metallic=0.0, rough=0.5):
    try:
        vo.DisplayMode = "Shaded"     # edges would dominate every number
    except Exception:
        pass
    vo.ShapeColor = color
    for prop, val in (("Render_Metallic", metallic),
                      ("Render_Roughness", rough)):
        if not hasattr(vo, prop):
            vo.addProperty("App::PropertyFloat", prop)
        setattr(vo, prop, val)


def build_scene(doc):
    """Glass with as many thickness discontinuities as it can carry,
    over a backdrop with enough contrast to show the refraction.

    A drilled glass plate gives a curved silhouette at every hole wall,
    where thickness steps between the full plate and nothing.  A second
    glass slab laid across it adds an INTERIOR step -- thickness jumps
    from one body to two without the glass ever ending -- which the
    silhouettes alone would not exercise.  The backdrop blocks are
    saturated and close-packed so the refraction has something to
    displace; against a flat ground the `disp * thick` term would move
    nothing and only the absorption would be measurable.
    """
    for ix in range(4):
        for iy in range(4):
            b = doc.addObject("Part::Box", "Block%d%d" % (ix, iy))
            b.Length, b.Width, b.Height = 24.0, 24.0, 6.0 + 5.0 * ((ix + iy) % 4)
            b.Placement.Base = FreeCAD.Vector(6.0 + ix * 30.0,
                                              6.0 + iy * 30.0, 0.0)
            shade(b.ViewObject,
                  ((ix % 2) * 0.85 + 0.1, (iy % 2) * 0.8 + 0.12,
                   ((ix + iy) % 2) * 0.9 + 0.08),
                  0.0, 0.35)

    plate = doc.addObject("Part::Box", "Plate")
    plate.Length, plate.Width, plate.Height = 126.0, 126.0, 11.0
    holes = []
    for ix in range(5):
        for iy in range(5):
            c = doc.addObject("Part::Cylinder", "Hole%d%d" % (ix, iy))
            c.Radius = 4.0 + 1.4 * ((ix * 3 + iy) % 5)
            c.Height = 40.0
            c.Placement.Base = FreeCAD.Vector(13.0 + ix * 25.0,
                                              13.0 + iy * 25.0, -8.0)
            holes.append(c)
    fuse = doc.addObject("Part::MultiFuse", "Drill")
    fuse.Shapes = holes
    drilled = doc.addObject("Part::Cut", "GlassPlate")
    drilled.Base = plate
    drilled.Tool = fuse
    drilled.Placement.Base = FreeCAD.Vector(0.0, 0.0, 52.0)

    bar = doc.addObject("Part::Box", "GlassBar")
    bar.Length, bar.Width, bar.Height = 132.0, 38.0, 9.0
    bar.Placement.Base = FreeCAD.Vector(-3.0, 44.0, 58.0)

    doc.recompute()
    for obj in (drilled, bar):
        shade(obj.ViewObject, (0.36, 0.86, 0.76), 0.0, 0.08)
        for prop, kind, val in (
                ("Render_Glass", "App::PropertyBool", True),
                ("Render_GlassIOR", "App::PropertyFloat", 1.52),
                ("Render_GlassDensity", "App::PropertyFloat", 0.10),
                ("Render_GlassRoughness", "App::PropertyFloat", 0.0)):
            if not hasattr(obj.ViewObject, prop):
                obj.ViewObject.addProperty(kind, prop)
            setattr(obj.ViewObject, prop, val)
        _glass.append(obj.ViewObject)
    for o in holes + [fuse, plate]:
        pass


def set_glass(on):
    """The medium is a MATERIAL flag, so the pass is switched per object.

    ! Not a render preference and not a view property -- `hasGlassBody`
    scans the draw list for `mat.glass`, so nothing outside the objects
    themselves can turn this pass on or off.
    """
    for vo in _glass:
        vo.Render_Glass = bool(on)
    pump(6)


def state_prefs():
    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)
    view.SetBool("ShowNaviCube", False)
    view.SetInt("AntiAliasing", 3)
    r = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    r.SetString("Type", "bgfx - OpenGL")
    r.SetBool("TemporalAccum", False)
    r.SetInt("TemporalAccumSamples", SAMPLES)
    r.SetBool("AO", False)                      # keep the frame to one effect
    r.SetBool("Cavity", False)
    r.SetBool("Shadow", False)
    r.SetBool("Bloom", False)
    r.SetBool("Volumetric", False)
    r.SetBool("GroundReflection", False)
    r.SetBool("Matcap", False)
    r.SetInt("OutputTransform", 1)
    r.SetInt("DebugViewMode", 0)
    say("prefs    : cache 3, MSAA 4x; AO/cavity/shadow/bloom/reflection "
        "off; samples %d, reference %dx" % (SAMPLES, SS))
    return r


def pin(view):
    cam = view.getCameraNode()
    cam.position.setValue(*_cam["pos"])
    cam.focalDistance.setValue(_cam["focal"])


def reference(view, tag, h, w):
    """Supersampled render of the same camera, box-downsampled.

    ! The interval will NOT be supersampled in it and cannot be (it is
    view-sized, and saveImage does not resize the view), so this is a
    floor rather than truth for the glass pass.  Reported anyway for
    the rest of the frame; the verdict keys on the contribution.

    ! Nudge the camera and bring it back first.  Held perfectly still,
    staticFrame is true and the interval is not re-rendered at all, so
    the reference would composite whatever the cache happened to hold
    -- and after a refinement that is a JITTERED sample.
    """
    cam = view.getCameraNode()
    cam.position.setValue(_cam["pos"][0] + 12.0, _cam["pos"][1] - 9.0,
                          _cam["pos"][2] + 7.0)
    pump(3)
    pin(view)
    pump(6)
    path = os.path.join(OUT, tag + "-ref.png")
    view.saveImage(path, w * SS, h * SS, "Current")
    return downsample(load(path)[:h * SS, :w * SS], h, w)


def leg(view, prefs, tag, h, w):
    """Single frame, supersampled reference, then the converged frame.

    In that order on purpose: the reference is rendered while the
    caches still hold an unjittered result, before any accumulation has
    had a chance to leave a jittered one in them.
    """
    view.Render_TemporalAccum = False
    prefs.SetBool("TemporalAccum", False)
    pin(view)
    settle()
    single = expect(load(grab(view, tag + "-single")),
                    tag + " single")
    if h == 0:
        h, w = int(single.shape[0]), int(single.shape[1])
    ref = reference(view, tag, h, w)
    view.Render_TemporalAccum = True
    prefs.SetBool("TemporalAccum", True)
    pump(SAMPLES * 6)
    accum = expect(load(grab(view, tag + "-accum")),
                   tag + " accumulated")
    n = samples(view)
    view.Render_TemporalAccum = False
    prefs.SetBool("TemporalAccum", False)
    pump(4)
    return single, accum, ref, n, h, w


def main():
    os.makedirs(OUT, exist_ok=True)
    prefs = state_prefs()
    doc = FreeCAD.newDocument("MediaAccum")
    build_scene(doc)

    view = FreeCADGui.ActiveDocument.ActiveView
    # ! Render_Light is what makes the scene light config valid; the
    # environment is what the glass pass reflects, and without a valid
    # one glassActive falls back to the dummy cube.
    for prop, val in (("Render_PBR", True),
                      ("Render_PBREnvBackground", True),
                      ("Render_Light", True)):
        if not hasattr(view, prop):
            view.addProperty("App::PropertyBool", prop)
        setattr(view, prop, val)
    for prop, kind, val in (
            ("Render_TemporalAccum", "App::PropertyBool", False),
            ("Render_TemporalAccumSamples", "App::PropertyInteger",
             SAMPLES)):
        if not hasattr(view, prop):
            view.addProperty(kind, prop)
        setattr(view, prop, val)

    view.setCameraType("Perspective")
    view.viewAxonometric()
    view.fitAll()
    settle()
    shape = stabilise(view)
    cam = view.getCameraNode()
    _cam["pos"] = tuple(cam.position.getValue().getValue())
    _cam["focal"] = cam.focalDistance.getValue()
    say("camera   : framed once by fitAll at (%.1f, %.1f, %.1f) focal "
        "%.1f, then pinned" % (_cam["pos"] + (_cam["focal"],)))

    set_glass(True)
    pin(view)
    settle()
    still_a = expect(load(grab(view, "still-a")), "still-a")
    pump(SAMPLES + 8)
    ctrl = rms(still_a, expect(load(grab(view, "still-b")), "still-b"))
    say("control  : accum off, %d frames apart, rms %.4f (must be 0)"
        % (SAMPLES + 8, ctrl))

    on_s, on_a, on_r, n_on, h, w = leg(view, prefs, "glass-on", 0, 0)
    d_on_s, d_on_a = rms(on_s, on_r), rms(on_a, on_r)
    say("glass on : shaded frame vs %dx reference, single %.4f, "
        "accumulated %.4f, at %d samples" % (SS, d_on_s, d_on_a, n_on))

    set_glass(False)
    off_s, off_a, off_r, n_off, h, w = leg(view, prefs, "glass-off", h, w)
    d_off_s, d_off_a = rms(off_s, off_r), rms(off_a, off_r)
    say("glass off: shaded frame vs %dx reference, single %.4f, "
        "accumulated %.4f, at %d samples" % (SS, d_off_s, d_off_a, n_off))
    set_glass(True)

    gain_on = d_on_s - d_on_a
    gain_off = d_off_s - d_off_a
    say("gain     : the accumulation closes %.4f with the glass on vs "
        "%.4f with it off (difference %+.4f)"
        % (gain_on, gain_off, gain_on - gain_off))

    # ---- what the pass contributes, on its own --------------------
    # A difference rather than a division: the glass pass replaces the
    # body's shading, it does not modulate it.
    c_single = decode(on_s) - decode(off_s)
    c_accum = decode(on_a) - decode(off_a)
    c_ref = decode(on_r) - decode(off_r)
    for name, c in (("single", c_single), ("accum", c_accum),
                    ("ref", c_ref)):
        Image.fromarray(np.clip(np.abs(c) * 4.0 * 255.0, 0, 255)
                        .astype(np.uint8)).save(
                            os.path.join(OUT, "contrib-%s.png" % name))
    mask = np.repeat((np.abs(c_ref).mean(axis=2) > 0.002)[:, :, None],
                     3, axis=2)
    cover = 100.0 * mask[:, :, 0].mean()
    say("contrib  : the glass pass touches %.2f%% of the frame" % cover)
    hf = lambda a: float(np.sqrt(np.mean(((a - box3(a))[mask]) ** 2)))
    hf_s, hf_a = hf(c_single), hf(c_accum)
    say("jagged   : high-frequency energy of the contribution, single "
        "%.5f -> accumulated %.5f (reference %.5f)"
        % (hf_s, hf_a, hf(c_ref)))
    td_s = float(np.sqrt(np.mean(((c_single - c_ref)[mask]) ** 2)))
    td_a = float(np.sqrt(np.mean(((c_accum - c_ref)[mask]) ** 2)))
    say("contrib-truth: vs the reference contribution, single %.5f, "
        "accumulated %.5f (lower is closer; the reference's own "
        "interval is 1x, so this is a floor, not truth)" % (td_s, td_a))
    moved = float(np.sqrt(np.mean(((c_accum - c_single)[mask]) ** 2)))
    say("moved    : accumulated contribution vs single-sample %.5f "
        "(a frozen interval can only move by the resampling blur)"
        % moved)

    ratio = hf_a / max(hf_s, 1e-12)
    say("ratio    : contribution hf accumulated/single = %.3f "
        "(frozen interval measured 0.862, re-rendered 0.807)" % ratio)

    # Both thresholds are CALIBRATED from a build of this tree with the
    # fix backed out (mediumRender = !staticFrame), not guessed.  The
    # borrowed 0.85 from the reflection probe would have passed here,
    # but only by 0.012 -- too close to the broken value to mean
    # anything.
    #
    # The gain differential is the stronger of the two by a long way,
    # and it is the reverse of the reflection case: there the frame's
    # distance to a supersampled reference was useless, because a
    # reduced-resolution mirror in the reference moved the wrong way.
    # Here the glass contribution is dominated by the refracted scene,
    # which resamples honestly, so the frame-level number does track
    # the fix -- frozen, the glass leg gains only 0.0115 more than the
    # same scene with the glass switched off; re-rendered per sample it
    # gains 0.1000, a factor of nine.
    gate_ratio = float(os.environ.get("MA_GATE", "0.835"))
    gate_gain = float(os.environ.get("MA_GATE_GAIN", "0.05"))
    # NOT in the verdict, deliberately: `td_a < 0.95 * td_s` is the
    # reflection probe's third check, and measured here it passes in
    # the BROKEN build too (0.01945 against a 0.02147 bar).  A check
    # that cannot fail on the defect it is meant to catch is reported,
    # not gated on -- keeping it would have manufactured confidence.
    #
    # Coverage is a guard rather than a measurement: if the glass pass
    # never ran, every number above compares two identical frames and
    # the ratio sits at a meaningless 1.0.
    ok = (ctrl == 0.0
          and n_on >= SAMPLES
          and cover > 5.0
          and ratio < gate_ratio
          and (gain_on - gain_off) > gate_gain)
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
        sys.stdout.flush()
        os._exit(0)


QtCore.QTimer.singleShot(1500, run)
