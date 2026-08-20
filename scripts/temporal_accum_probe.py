"""Idle temporal accumulation probe (docs/RenderEngine.md sec 3.5).

Runs inside a FreeCAD GUI session:

    FreeCAD scripts/temporal_accum_probe.py

Answers four questions, each against a control, and prints a verdict:

1. Does it converge?  Successive accumulated frames must stop differing.
2. Is it RIGHT?  The converged frame is compared against a genuinely
   supersampled reference (the same camera rendered at 3x linear
   resolution and box-downsampled).  Accumulation must move the frame
   TOWARD that reference -- a blur would move it away.
3. Does it stay off when off?  With the feature off, pumping the same
   number of frames must leave the image bit-identical.  Without this
   control a converging measurement could just be frame-to-frame noise.
4. Does it reset?  After converging, a camera move and a move back must
   land on the plain single-sample frame again -- no trace of the old
   accumulation, which is the no-ghosting claim.

The scene is built for what MSAA cannot fix: a smooth low-roughness
metal (specular aliasing, shaded once per pixel per triangle whatever
the sample count) plus thin near-diagonal edges.
"""
import math
import os
import sys
import time

import numpy as np
from PIL import Image

import FreeCAD
import FreeCADGui
import Part

OUT = os.environ.get("TA_OUT", "/tmp/ta-probe")
SAMPLES = int(os.environ.get("TA_SAMPLES", "32"))
SS = int(os.environ.get("TA_SS", "3"))          # reference supersample factor
RESULT = os.path.join(OUT, "result.txt")

_lines = []


def say(msg):
    _lines.append(msg)
    FreeCAD.Console.PrintMessage("[ta] %s\n" % msg)


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
    """RGB float array, alpha dropped -- the readback's alpha is not the
    picture."""
    img = Image.open(path).convert("RGB")
    return np.asarray(img).astype(np.float64)


def rms(a, b):
    return float(np.sqrt(np.mean((a - b) ** 2)))


def srgb_decode(x):
    """0-255 sRGB to linear 0-1."""
    c = x / 255.0
    return np.where(c <= 0.04045, c / 12.92,
                    ((c + 0.055) / 1.055) ** 2.4)


def srgb_encode(v):
    """Linear 0-1 back to 0-255 sRGB."""
    c = np.where(v <= 0.0031308, v * 12.92,
                 1.055 * np.maximum(v, 0.0) ** (1.0 / 2.4) - 0.055)
    return np.clip(c * 255.0, 0.0, 255.0)


def build_scene(doc):
    """A Siemens star -- the standard antialiasing target.

    Thin wedges radiating from a point: their angular pitch is fixed, so
    the spatial frequency climbs without bound toward the centre and
    every ring of the image sits at a different point either side of
    Nyquist.  That is content 4x MSAA demonstrably cannot resolve (five
    coverage levels), and it is what the earlier scene lacked -- nine
    mirror spheres reflecting a near-uniform procedural environment
    produced a frame with 386 gradient pixels in 1.07M, which no
    antialiasing of any kind could have improved.

    Bright blades on the dark background give the hard silhouettes the
    measurement needs; a low-roughness dielectric sphere rides along for
    the specular case, which is aliasing MSAA cannot touch at any sample
    count.
    """
    blades = 48
    for i in range(blades):
        b = doc.addObject("Part::Box", "Blade%d" % i)
        b.Length, b.Width, b.Height = 40.0, 0.9, 0.4
        # Radiating from the centre, so the gaps close toward it.
        b.Placement = FreeCAD.Placement(
            FreeCAD.Vector(0, 0, 0),
            FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1),
                             i * (360.0 / blades)))
    s = doc.addObject("Part::Sphere", "Gloss")
    s.Radius = 7.0
    s.Placement.Base = FreeCAD.Vector(0, 0, 8.0)
    doc.recompute()
    for obj in doc.Objects:
        vo = obj.ViewObject
        gloss = obj.Name == "Gloss"
        vo.ShapeColor = (0.93, 0.93, 0.95)
        for prop, val in (("Render_Metallic", 1.0 if gloss else 0.0),
                          ("Render_Roughness", 0.10 if gloss else 0.45)):
            if not hasattr(vo, prop):
                vo.addProperty("App::PropertyFloat", prop)
            setattr(vo, prop, val)


def state_prefs():
    """State every scene-wide render preference this probe depends on.

    Inheriting them from whatever cfg the launcher happened to copy is
    how a probe ends up measuring somebody else's matcap: the frame is
    the product of the whole preference set, so the ones that decide
    what is on screen belong here, in the record, not in a file.
    """
    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)          # the engine only runs in mode 3
    view.SetBool("ShowNaviCube", False)    # cube pixels have faked a PASS before
    view.SetInt("AntiAliasing", 3)         # 4x MSAA: this refines it, not replaces it
    r = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    r.SetString("Type", "bgfx - OpenGL")
    r.SetBool("TemporalAccum", False)      # the view property drives it below
    r.SetInt("TemporalAccumSamples", SAMPLES)
    r.SetBool("Matcap", False)             # one flat material for the whole scene
    r.SetBool("AO", False)                 # its map is not re-rendered while accumulating
    r.SetBool("Bloom", False)
    r.SetBool("Volumetric", False)
    r.SetInt("OutputTransform", 1)   # sRGB: the frame is encoded on the way out
    say("prefs    : cache 3, MSAA 4x, matcap/AO/bloom/volumetric off, "
        "samples %d, reference %dx" % (SAMPLES, SS))


def main():
    os.makedirs(OUT, exist_ok=True)
    state_prefs()
    doc = FreeCAD.newDocument("TemporalAccum")
    build_scene(doc)

    view = FreeCADGui.ActiveDocument.ActiveView
    for prop, val in (("Render_PBR", True),
                      ("Render_PBREnvBackground", True)):
        if not hasattr(view, prop):
            view.addProperty("App::PropertyBool", prop)
        setattr(view, prop, val)
    if not hasattr(view, "Render_TemporalAccum"):
        view.addProperty("App::PropertyBool", "Render_TemporalAccum")
    if not hasattr(view, "Render_TemporalAccumSamples"):
        view.addProperty("App::PropertyInteger", "Render_TemporalAccumSamples")
    view.Render_TemporalAccumSamples = SAMPLES

    # Pinned camera -- never fitAll for an A/B, it restages under the
    # capture (see the render A/B harness notes).
    view.setCameraType("Perspective")
    # Straight down onto the star so its whole frequency range is in
    # frame; pinned, never fitAll, which restages under the capture.
    view.viewTop()
    cam = view.getCameraNode()
    cam.position.setValue(0.0, 0.0, 100.0)
    cam.focalDistance.setValue(100.0)
    settle()

    # ---- 1. control: feature OFF, and OFF must mean STILL -----------
    view.Render_TemporalAccum = False
    settle()
    off_a = grab(view, "off-a")
    pump(SAMPLES + 8)
    off_b = grab(view, "off-b")
    a, b = load(off_a), load(off_b)
    ctrl = rms(a, b)
    say("control  : off, %d frames apart, rms %.4f (must be 0)"
        % (SAMPLES + 8, ctrl))

    # ---- 2. accumulate and watch it converge ------------------------
    # Both gates, reported: the view property is what should carry it,
    # and the preference is the fallback the bridge reads when no
    # property exists. Turning on only one of them and measuring
    # nothing would not say WHICH one failed.
    view.Render_TemporalAccum = True
    prefs = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    say("gates    : view prop %r, preference %r"
        % (view.Render_TemporalAccum, prefs.GetBool("TemporalAccum", False)))
    prefs.SetBool("TemporalAccum", True)
    pump(2)
    first = load(grab(view, "accum-first"))
    steps = []
    prev = first
    # Generously more frames than the sample budget: the accumulation
    # only advances on frames that actually render, and the engine's
    # own "on but never engages" report needs a stretch of them before
    # it will speak.
    for k in range(4):
        pump(80)
        cur = load(grab(view, "accum-%d" % k))
        steps.append((rms(prev, cur), samples(view)))
        prev = cur
    accum = prev
    say("converge : per-quarter step rms/samples %s"
        % " -> ".join("%.4f@%d" % (d, n) for d, n in steps))
    say("moved    : accumulated vs single-sample rms %.4f" % rms(a, accum))

    # ---- 3. is it right?  compare both against real supersampling ---
    w, h = int(a.shape[1]), int(a.shape[0])
    ref_path = os.path.join(OUT, "ref-%dx.png" % SS)
    view.saveImage(ref_path, w * SS, h * SS, "Current")
    big = load(ref_path)
    # Box-downsample: the definition of a supersampled pixel.
    big = big[:h * SS, :w * SS]
    # ! Average in LINEAR LIGHT, not in the encoded values.
    #
    # The engine is colour managed: the accumulation runs on the scene
    # target, which holds linear radiance, and the sRGB encode happens
    # after it. Box-averaging the encoded PNG instead would compare
    # "mean of encoded" against "encode of mean", and because the
    # encode is concave those differ systematically -- brightest
    # exactly at the high-contrast edges the whole measurement is
    # about. Doing it wrong made the accumulated frame look 0.9 levels
    # too bright and scored it FARTHER from truth than the aliased
    # frame it plainly improves on.
    ref = srgb_encode(srgb_decode(big).reshape(
        h, SS, w, SS, 3).mean(axis=(1, 3)))
    Image.fromarray(ref.astype(np.uint8)).save(
        os.path.join(OUT, "ref-down.png"))
    d_single = rms(a, ref)
    d_accum = rms(accum, ref)
    say("truth    : single %.4f vs reference, accumulated %.4f (lower "
        "is closer)" % (d_single, d_accum))

    # ---- 4. reset: move away and back, no trace left ----------------
    # Converge somewhere else entirely, then jump back and look at the
    # very next frames. Ghosting would drag that other view's content
    # along; a history replaced outright cannot.
    cam.position.setValue(25.0, -15.0, 90.0)
    pump(SAMPLES + 40)
    elsewhere = load(grab(view, "elsewhere"))
    d_else = rms(a, elsewhere)
    cam.position.setValue(0.0, 0.0, 100.0)
    pump(3)
    back = load(grab(view, "after-move"))
    n_back = samples(view)
    d_back = rms(a, back)
    # Not zero: the accumulation legitimately restarts the instant the
    # camera settles, so a few samples are already in. What must be
    # true is that the counter went back to the start and that none of
    # the other view survived -- which d_else scales.
    say("reset    : returned at %d samples, %.4f from the plain frame; "
        "the view it came from was %.4f away" % (n_back, d_back, d_else))

    ok = (ctrl == 0.0                       # off means still
          and steps[-1][1] >= SAMPLES        # it really accumulated
          and steps[-1][0] < steps[0][0]     # and then converged
          and d_accum < d_single             # toward the truth, not away
          and n_back <= 4                    # the count restarted
          and d_back < 0.25 * d_else)        # with none of the old view in it
    say("VERDICT  : %s" % ("PASS" if ok else "FAIL"))
    with open(RESULT, "w") as fh:
        fh.write("\n".join(_lines) + "\n")
        fh.write("DONE\n")


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
    # An in-process quit is always defeatable; the launcher's timeout is
    # the real backstop, but leave nothing running when we can help it.
    sys.stdout.flush()
    os._exit(0)
