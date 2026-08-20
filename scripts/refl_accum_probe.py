"""Planar reflection and media intervals under idle temporal
accumulation (docs/RenderEngine.md sec 3.5).

Runs inside a FreeCAD GUI session:

    FreeCAD scripts/refl_accum_probe.py

The third of the audit probes, after the AO one and the cavity one.
These two targets are cached on `staticFrame` -- the mirrored scene
re-render and the media interval depths -- and `staticFrame` is exactly
the predicate that says an accumulation may run.  So during a
refinement the cache answers "hit" for every sample and both targets
hold the result they were rendered with at the UNJITTERED camera, while
everything sampling them moves underneath.  That is the same shape as
the AO defect, in two more places.

It cannot be measured off the reflection debug view (mode 9): that
shows the reflection as APPLIED, sampled with the current jittered
camera, so a frozen target still yields a moving picture and the mode
reads as though all is well.  Measured instead the way the cavity probe
measures: what the pass CONTRIBUTES.  Render the same camera with the
pass on and off, difference them in linear light, and compare that
contribution against a supersampled render of it.  A frozen target
cannot converge, so its contribution stays as aliased as the frame it
was rendered from, however many samples the view accumulates.

! The supersampled reference is taken BEFORE the accumulation ever
runs.  Taken after, it would render through the same stale cache and
inherit the very defect being measured -- which is not hypothetical:
the first GTAO measurement in this series was wrong for exactly that
reason.
"""
import os
import sys
import time

import numpy as np
from PIL import Image

import FreeCAD
import FreeCADGui
from PySide6 import QtCore

OUT = os.environ.get("RF_OUT", "/tmp/refl-probe")
SAMPLES = int(os.environ.get("RF_SAMPLES", "32"))
SS = int(os.environ.get("RF_SS", "3"))
RESULT = os.path.join(OUT, "result.txt")

_lines = []
_cam = {}


def say(msg):
    _lines.append(msg)
    FreeCAD.Console.PrintMessage("[rf] %s\n" % msg)


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


def build_scene(doc):
    """A drilled plate floating over the ground plane.

    Lifted clear of the ground so the mirrored copy is a whole object
    with its own silhouette, rather than something touching its own
    reflection -- the aliasing being measured lives on those mirrored
    edges.
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
    cut.Placement.Base = FreeCAD.Vector(0.0, 0.0, 34.0)
    doc.recompute()
    for obj in doc.Objects:
        vo = getattr(obj, "ViewObject", None)
        if vo is None or not vo.Visibility:
            continue
        try:
            vo.DisplayMode = "Shaded"     # edges would dominate every number
        except Exception:
            pass
        vo.ShapeColor = (0.90, 0.58, 0.22)
        for prop, val in (("Render_Metallic", 0.0),
                          ("Render_Roughness", 0.45)):
            if not hasattr(vo, prop):
                vo.addProperty("App::PropertyFloat", prop)
            setattr(vo, prop, val)


def state_prefs():
    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)
    view.SetBool("ShowNaviCube", False)
    view.SetInt("AntiAliasing", 3)
    r = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    r.SetString("Type", "bgfx - OpenGL")
    r.SetBool("TemporalAccum", False)
    r.SetInt("TemporalAccumSamples", SAMPLES)
    r.SetBool("GroundReflection", True)         # THE point of this probe
    r.SetFloat("GroundReflectionIntensity", 0.9)
    r.SetBool("AO", False)                      # keep the frame to one effect
    r.SetBool("Cavity", False)
    r.SetBool("Shadow", False)
    r.SetBool("Bloom", False)
    r.SetBool("Volumetric", False)
    r.SetBool("Matcap", False)
    r.SetInt("OutputTransform", 1)
    r.SetInt("DebugViewMode", 0)
    say("prefs    : cache 3, MSAA 4x, ground reflection at 0.9; "
        "AO/cavity/shadow/bloom off; edges off; samples %d, reference "
        "%dx" % (SAMPLES, SS))
    return r


def vset(view, prefs, name, value, kind="bool"):
    """Set a render knob where it actually takes -- the VIEW PROPERTY.

    ! The preference is only the seed for that property when the view
    is created; from then on the property is what the frame reads, and
    writing the preference alone is silent and inert.
    """
    if kind == "bool":
        prefs.SetBool(name, bool(value))
    else:
        prefs.SetFloat(name, float(value))
    prop = "Render_" + name
    if hasattr(view, prop):
        setattr(view, prop, value)
    else:
        say("!! the view has no %s" % prop)
    pump(4)


def pin(view):
    cam = view.getCameraNode()
    cam.position.setValue(*_cam["pos"])
    cam.focalDistance.setValue(_cam["focal"])


def reference(view, tag, h, w):
    """Supersampled render of the same camera, box-downsampled.

    ! The mirror will NOT be supersampled in it, and cannot be: the
    reflection target is sized from the VIEW (effW/effH in
    BGFXView::updateEffect), and saveImage does not resize the view --
    no second "bgfx: view init" appears in the log.  So this reference
    holds a 1x mirror that the 3x3 downsample merely smooths, and the
    frame-level distance to it is NOT ground truth for this pass.  It
    is still reported below, because the cavity probe's on/off gain
    comparison is worth having for the rest of the frame, but the
    verdict keys on the contribution's own high-frequency energy.

    ! Nudge the camera and bring it back first.  Held perfectly still,
    staticFrame is true and the mirror is not re-rendered at all, so
    the reference would composite whatever the cache happened to hold
    -- and after a refinement that is a JITTERED sample.  The nudge
    makes the last full render redraw it at the pinned camera.  Three
    separate measurements in this series were wrong because a
    reference inherited a cache it should have refreshed.
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
    single = load(grab(view, tag + "-single"))
    if h == 0:
        h, w = int(single.shape[0]), int(single.shape[1])
    ref = reference(view, tag, h, w)
    view.Render_TemporalAccum = True
    prefs.SetBool("TemporalAccum", True)
    pump(SAMPLES * 6)
    accum = load(grab(view, tag + "-accum"))
    n = samples(view)
    view.Render_TemporalAccum = False
    prefs.SetBool("TemporalAccum", False)
    pump(4)
    return single, accum, ref, n, h, w


def main():
    os.makedirs(OUT, exist_ok=True)
    prefs = state_prefs()
    doc = FreeCAD.newDocument("ReflAccum")
    build_scene(doc)

    view = FreeCADGui.ActiveDocument.ActiveView
    # ! Render_Light is what makes the scene light config VALID, and the
    # ground quad -- and so the ground reflection -- exists only when it
    # is. Without it the pass silently never runs and every number here
    # would be about a frame with no reflection in it.
    for prop, val in (("Render_PBR", True),
                      ("Render_PBREnvBackground", True),
                      ("Render_Light", True),
                      ("Render_GroundReflection", True)):
        if not hasattr(view, prop):
            view.addProperty("App::PropertyBool", prop)
        setattr(view, prop, val)
    for prop, kind, val in (
            ("Render_GroundReflectionIntensity", "App::PropertyFloat", 0.9),
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
    cam = view.getCameraNode()
    _cam["pos"] = tuple(cam.position.getValue().getValue())
    _cam["focal"] = cam.focalDistance.getValue()
    say("camera   : framed once by fitAll at (%.1f, %.1f, %.1f) focal "
        "%.1f, then pinned" % (_cam["pos"] + (_cam["focal"],)))

    vset(view, prefs, "GroundReflection", True)
    pin(view)
    settle()
    still_a = load(grab(view, "still-a"))
    pump(SAMPLES + 8)
    ctrl = rms(still_a, load(grab(view, "still-b")))
    say("control  : accum off, %d frames apart, rms %.4f (must be 0)"
        % (SAMPLES + 8, ctrl))

    on_s, on_a, on_r, n_on, h, w = leg(view, prefs, "refl-on", 0, 0)
    d_on_s, d_on_a = rms(on_s, on_r), rms(on_a, on_r)
    say("refl on  : shaded frame vs %dx reference, single %.4f, "
        "accumulated %.4f, at %d samples" % (SS, d_on_s, d_on_a, n_on))

    vset(view, prefs, "GroundReflection", False)
    off_s, off_a, off_r, n_off, h, w = leg(view, prefs, "refl-off", h, w)
    d_off_s, d_off_a = rms(off_s, off_r), rms(off_a, off_r)
    say("refl off : shaded frame vs %dx reference, single %.4f, "
        "accumulated %.4f, at %d samples" % (SS, d_off_s, d_off_a, n_off))

    gain_on = d_on_s - d_on_a
    gain_off = d_off_s - d_off_a
    say("gain     : the accumulation closes %.4f with the reflection "
        "on vs %.4f with it off (difference %+.4f)"
        % (gain_on, gain_off, gain_on - gain_off))

    # ---- what the pass contributes, on its own --------------------
    # A difference rather than the cavity probe's division: the ground
    # reflection composites, it does not multiply.
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
    say("contrib  : the reflection touches %.2f%% of the frame"
        % (100.0 * mask[:, :, 0].mean()))
    hf = lambda a: float(np.sqrt(np.mean(((a - box3(a))[mask]) ** 2)))
    say("jagged   : high-frequency energy of the contribution, single "
        "%.5f -> accumulated %.5f (reference %.5f)"
        % (hf(c_single), hf(c_accum), hf(c_ref)))
    td_s = float(np.sqrt(np.mean(((c_single - c_ref)[mask]) ** 2)))
    td_a = float(np.sqrt(np.mean(((c_accum - c_ref)[mask]) ** 2)))
    say("contrib-truth: vs the reference contribution, single %.5f, "
        "accumulated %.5f (lower is closer; the reference's own mirror "
        "is 1x, so this is a floor, not truth)" % (td_s, td_a))
    moved = float(np.sqrt(np.mean(((c_accum - c_single)[mask]) ** 2)))
    say("moved    : accumulated contribution vs single-sample %.5f "
        "(a frozen mirror can only move by the resampling blur)"
        % moved)

    # The verdict deliberately does NOT use the frame-level gain: with
    # a 1x mirror in the reference, converging the mirror moves the
    # frame AWAY from it, so that comparison scores a working pass as a
    # broken one.  The gate is the contribution's own high-frequency
    # energy instead.
    #
    # ! The threshold is CALIBRATED, not guessed, because a frozen
    # mirror does not sit at 1.0: its sampling still jitters, and that
    # resampling alone blurs the contribution.  Measured on this scene,
    # 32 samples, llvmpipe -- with the mirror frozen the ratio is
    # 0.886, with it re-rendered per sample 0.780.  0.85 separates
    # them with room on both sides.  A looser gate passes the defect,
    # which is exactly what the first version of this probe did.
    ratio = hf(c_accum) / max(hf(c_single), 1e-12)
    say("ratio    : contribution hf accumulated/single = %.3f "
        "(frozen mirror measured 0.886, re-rendered 0.780)" % ratio)
    ok = (ctrl == 0.0
          and n_on >= SAMPLES
          and ratio < 0.85
          and td_a < 0.95 * td_s)
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
