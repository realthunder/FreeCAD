"""Cavity shading under idle temporal accumulation (docs/RenderEngine.md
sec 3.5).

Runs inside a FreeCAD GUI session:

    FreeCAD scripts/cavity_accum_probe.py

Cavity is not a stochastic estimator the way the two AO passes are, so
it does not have their defect -- there is no noise to decorrelate.  Its
problem is the other one accumulation exists for: it is a screen-space
CREASE DETECTOR, a two-tap normal difference over a one-texel baseline,
run once per pixel after the MSAA resolve.  Its response is a line one
or two pixels wide, and a line drawn at one sample per pixel stairsteps
-- which is what "jagged cavity shading" is.  Multisampling cannot
touch it at any sample count, because every sample inside a triangle
shares one shaded value and this pass runs after the resolve entirely.

So the question is not whether it is noisy.  It is whether the pass
re-runs under the jittered camera on each accumulation sample, and
whether its edges converge to what a supersampled render would give.

The probe isolates the cavity term rather than judging the shaded
frame.  The pass is a MULTIPLY over the finished opaque scene, so
rendering the same camera with it on and off and dividing IN LINEAR
LIGHT recovers the multiplier map exactly -- the crease lines alone,
with the shading, the material and the environment all divided out.
That is the picture the jaggedness lives in.

Edges are off (Shaded display mode, not Flat Lines): the wireframe
overlay draws its own hard lines along exactly the creases being
measured, and would dominate every number here.
"""
import os
import sys
import time

import numpy as np
from PIL import Image

import FreeCAD
import FreeCADGui
from PySide6 import QtCore

OUT = os.environ.get("CV_OUT", "/tmp/cavity-probe")
SAMPLES = int(os.environ.get("CV_SAMPLES", "32"))
SS = int(os.environ.get("CV_SS", "3"))
RESULT = os.path.join(OUT, "result.txt")

_lines = []
_cam = {}


def say(msg):
    _lines.append(msg)
    FreeCAD.Console.PrintMessage("[cv] %s\n" % msg)


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
    try:
        return view.getRenderStats().get("temporalSamples", -1)
    except Exception:
        return -1


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float64)


def stabilise(view):
    """Wait until the captured frame stops changing SIZE.

    ! The window is still laying itself out for a while after the first
    document opens, and every number below differences two captures, so
    a size that moves midway invalidates the lot.  Measured on the
    desktop (real-GPU) window it went 703 rows then 677 a few frames
    later, which aborted this probe on a shape mismatch rather than on
    anything about the renderer.  Under xvfb it is rarer but not
    absent.  Wait for two consecutive captures to agree.
    """
    last = None
    for i in range(40):
        settle(4)
        shape = load(grab(view, "size-probe")).shape
        if shape == last:
            say("size     : frame stable at %dx%d after %d checks"
                % (shape[1], shape[0], i + 1))
            return shape
        last = shape
    raise RuntimeError("frame size never settled (last %s)" % (last,))


def rms(a, b):
    return float(np.sqrt(np.mean((a - b) ** 2)))


def decode(x):
    """0-255 sRGB to linear 0-1."""
    c = x / 255.0
    return np.where(c <= 0.04045, c / 12.92,
                    ((c + 0.055) / 1.055) ** 2.4)


def encode(v):
    c = np.where(v <= 0.0031308, v * 12.92,
                 1.055 * np.maximum(v, 0.0) ** (1.0 / 2.4) - 0.055)
    return np.clip(c * 255.0, 0.0, 255.0)


def downsample(big, h, w):
    """Box-average a supersampled frame IN LINEAR LIGHT.

    The engine averages linear radiance and encodes after, so averaging
    the encoded PNG would compare "mean of encoded" against "encode of
    mean" -- and the encode is concave, so they differ worst at exactly
    the high-contrast edges this measurement is about.
    """
    lin = decode(big).reshape(h, SS, w, SS, 3).mean(axis=(1, 3))
    return encode(lin)


def box3(a):
    p = np.pad(a, ((1, 1), (1, 1), (0, 0)), mode="edge")
    out = np.zeros_like(a)
    for dy in range(3):
        for dx in range(3):
            out += p[dy:dy + a.shape[0], dx:dx + a.shape[1]]
    return out / 9.0


def cavity_term(with_c, without_c):
    """The multiplier the cavity pass applied, recovered by division.

    A multiply in linear light, so linear division inverts it exactly.
    Clamped: where the denominator is near black the quotient is
    numerically meaningless (both sides are quantization noise), and
    those pixels carry no crease information anyway.
    """
    a = decode(with_c)
    b = decode(without_c)
    return np.where(b > 0.004, np.clip(a / np.maximum(b, 1e-6), 0.0, 1.0),
                    1.0)


def build_scene(doc):
    """A drilled plate -- pocket rims and inside corners are creases.

    The same scene as the AO probe, for the same reason: every feature
    in it is an edge between two surfaces, which is what a curvature
    estimator responds to.
    """
    plate = doc.addObject("Part::Box", "Plate")
    plate.Length, plate.Width, plate.Height = 120.0, 120.0, 12.0
    tools = []
    for ix in range(4):
        for iy in range(4):
            c = doc.addObject("Part::Cylinder", "Pocket%d%d" % (ix, iy))
            c.Radius = 9.0
            c.Height = 9.0
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
        # ! Edges OFF. The default Flat Lines draws a hard wireframe
        # along exactly the creases the cavity pass shades, and its
        # aliasing would be most of every number below.
        try:
            vo.DisplayMode = "Shaded"
        except Exception:
            say("!! could not set Shaded display mode -- edges are IN "
                "the measurement")
        vo.ShapeColor = (0.82, 0.82, 0.84)
        for prop, val in (("Render_Metallic", 0.0),
                          ("Render_Roughness", 0.55)):
            if not hasattr(vo, prop):
                vo.addProperty("App::PropertyFloat", prop)
            setattr(vo, prop, val)


def state_prefs():
    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)
    view.SetBool("ShowNaviCube", False)
    view.SetInt("AntiAliasing", 3)         # 4x MSAA: this refines it
    r = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    r.SetString("Type", "bgfx - OpenGL")
    r.SetBool("TemporalAccum", False)
    r.SetInt("TemporalAccumSamples", SAMPLES)
    r.SetBool("Cavity", True)              # THE point of this probe
    r.SetFloat("CavityValley", 1.0)
    r.SetFloat("CavityRidge", 0.5)
    r.SetFloat("CavityRadius", 1.0)
    r.SetBool("AO", False)                 # a second crease darkener would confound
    r.SetBool("Shadow", False)
    r.SetBool("Bloom", False)
    r.SetBool("Volumetric", False)
    r.SetBool("Matcap", False)
    r.SetInt("OutputTransform", 1)
    r.SetInt("DebugViewMode", 0)           # global RenderParam, not a view property
    say("prefs    : cache 3, MSAA 4x, cavity valley 1.0 ridge 0.5 "
        "radius 1.0; AO/shadow/bloom/matcap off; edges off; samples "
        "%d, reference %dx" % (SAMPLES, SS))
    return r


def vset(view, prefs, name, value):
    """Set a render knob where it actually takes.

    ! Every Render_* knob here is a per-view property seeded from the
    preference when the view is created; from then on the PROPERTY is
    what the frame reads and writing only the preference is silent and
    inert.  Measured 2026-08-21 on Render_AOMethod: two legs switched
    by preference alone came out rms 0.0000 apart.
    """
    prefs.SetBool(name, value) if isinstance(value, bool) \
        else prefs.SetFloat(name, float(value))
    prop = "Render_" + name
    if hasattr(view, prop):
        setattr(view, prop, value)
    else:
        say("!! the view has no %s -- the preference alone may not take"
            % prop)
    pump(4)


def pin(view):
    cam = view.getCameraNode()
    cam.position.setValue(*_cam["pos"])
    cam.focalDistance.setValue(_cam["focal"])


def converged(view, prefs, tag):
    """Single-sample frame and the converged frame, at the same camera."""
    view.Render_TemporalAccum = False
    prefs.SetBool("TemporalAccum", False)
    pin(view)
    settle()
    single = load(grab(view, tag + "-single"))
    view.Render_TemporalAccum = True
    prefs.SetBool("TemporalAccum", True)
    pump(SAMPLES * 6)
    accum = load(grab(view, tag + "-accum"))
    n = samples(view)
    view.Render_TemporalAccum = False
    prefs.SetBool("TemporalAccum", False)
    pump(4)
    return single, accum, n


def reference(view, prefs, tag, h, w, cavity_on):
    """Supersampled render of the same camera, box-downsampled.

    ! Do NOT rescale CavityRadius for this, however much the units
    invite it.  The baseline is stated in texels of the RENDERER's
    target (u_cavityParams.zw = radius / view width), and saveImage
    does not resize that target -- the log shows no second view init,
    and the 3x image comes out of the same 1010x653 renderer.  So a
    radius of 1 already spans one full 1x pixel in the 3x image, which
    is exactly the estimator the on-screen frame runs and therefore the
    one the accumulation converges toward.  Measured 2026-08-21:
    "correcting" the radius to SS thickened the reference's crease
    lines to twice the coverage and 1.4x the depth, and scored the
    accumulation as diverging from a pass it was converging to.
    """
    pin(view)
    pump(2)
    path = os.path.join(OUT, tag + "-ref.png")
    view.saveImage(path, w * SS, h * SS, "Current")
    return downsample(load(path)[:h * SS, :w * SS], h, w)


def main():
    os.makedirs(OUT, exist_ok=True)
    prefs = state_prefs()
    doc = FreeCAD.newDocument("CavityAccum")
    build_scene(doc)

    view = FreeCADGui.ActiveDocument.ActiveView
    for prop, val in (("Render_PBR", True),
                      ("Render_PBREnvBackground", True)):
        if not hasattr(view, prop):
            view.addProperty("App::PropertyBool", prop)
        setattr(view, prop, val)
    for prop, kind in (("Render_TemporalAccum", "App::PropertyBool"),
                       ("Render_TemporalAccumSamples",
                        "App::PropertyInteger")):
        if not hasattr(view, prop):
            view.addProperty(kind, prop)
    view.Render_TemporalAccumSamples = SAMPLES

    view.setCameraType("Perspective")
    view.viewAxonometric()
    view.fitAll()
    settle()
    stabilise(view)
    cam = view.getCameraNode()
    _cam["pos"] = tuple(cam.position.getValue().getValue())
    _cam["focal"] = cam.focalDistance.getValue()
    say("camera   : framed once by fitAll at (%.1f, %.1f, %.1f) focal "
        "%.1f, then pinned" % (_cam["pos"] + (_cam["focal"],)))

    # ---- cavity ON ---------------------------------------------------
    vset(view, prefs, "Cavity", True)
    pin(view)
    settle()
    still_a = load(grab(view, "still-a"))
    pump(SAMPLES + 8)
    ctrl = rms(still_a, load(grab(view, "still-b")))
    say("control  : accum off, %d frames apart, rms %.4f (must be 0)"
        % (SAMPLES + 8, ctrl))

    on_single, on_accum, n_on = converged(view, prefs, "cav-on")
    h, w = int(on_single.shape[0]), int(on_single.shape[1])
    on_ref = reference(view, prefs, "cav-on", h, w, True)
    d_on_s, d_on_a = rms(on_single, on_ref), rms(on_accum, on_ref)
    say("cavity on: shaded frame vs %dx reference, single %.4f, "
        "accumulated %.4f, at %d samples" % (SS, d_on_s, d_on_a, n_on))

    # ---- cavity OFF, the control -------------------------------------
    # The accumulation improves ANY frame, so "it got closer to truth"
    # says nothing on its own about cavity.  What does is whether the
    # improvement is BIGGER with the pass on -- i.e. whether the frame
    # was carrying error that only this pass puts there.
    vset(view, prefs, "Cavity", False)
    off_single, off_accum, n_off = converged(view, prefs, "cav-off")
    off_ref = reference(view, prefs, "cav-off", h, w, False)
    d_off_s, d_off_a = rms(off_single, off_ref), rms(off_accum, off_ref)
    say("cavity off: shaded frame vs %dx reference, single %.4f, "
        "accumulated %.4f, at %d samples"
        % (SS, d_off_s, d_off_a, n_off))
    gain_on = d_on_s - d_on_a
    gain_off = d_off_s - d_off_a
    say("gain     : the accumulation closes %.4f with cavity on vs "
        "%.4f with it off -- the difference, %.4f, is error only this "
        "pass contributes" % (gain_on, gain_off, gain_on - gain_off))

    # ---- the cavity term alone ---------------------------------------
    # Divide the pass out.  Everything below is the multiplier map: the
    # crease lines with the shading and the material removed.
    t_single = cavity_term(on_single, off_single)
    t_accum = cavity_term(on_accum, off_accum)
    t_ref = cavity_term(on_ref, off_ref)
    for name, t in (("single", t_single), ("accum", t_accum),
                    ("ref", t_ref)):
        Image.fromarray(
            np.clip(t * 255.0, 0, 255).astype(np.uint8)).save(
                os.path.join(OUT, "term-%s.png" % name))
    # Where the pass does anything at all. Everywhere else it is a flat
    # 1.0 that averages to itself and would only dilute the reading.
    mask = np.repeat((t_ref.mean(axis=2) < 0.99)[:, :, None], 3, axis=2)
    say("term     : the pass darkens %.2f%% of the frame"
        % (100.0 * mask[:, :, 0].mean()))
    hf = lambda a: float(np.sqrt(np.mean(((a - box3(a))[mask]) ** 2)))
    say("jagged   : high-frequency energy of the cavity term over "
        "those pixels, single %.5f -> accumulated %.5f (reference "
        "%.5f)" % (hf(t_single), hf(t_accum), hf(t_ref)))
    td_s = float(np.sqrt(np.mean(((t_single - t_ref)[mask]) ** 2)))
    td_a = float(np.sqrt(np.mean(((t_accum - t_ref)[mask]) ** 2)))
    say("term-truth: cavity term vs the supersampled term, single "
        "%.5f, accumulated %.5f (lower is closer)" % (td_s, td_a))

    ok = (ctrl == 0.0
          and n_on >= SAMPLES
          and d_on_a < d_on_s          # the frame improves with it on
          and td_a < td_s              # and the crease lines themselves do
          and gain_on > gain_off)      # by more than the frame improves anyway
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


# ! Deferred, not inline: work run straight off the startup script
# shares the frame the GUI is still assembling, and its redraws do not
# really redraw.
QtCore.QTimer.singleShot(1500, run)
