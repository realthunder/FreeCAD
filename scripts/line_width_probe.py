"""Perceived line thickness against screen angle, width and glass.

Runs inside a FreeCAD GUI session:

    FreeCAD scripts/line_width_probe.py

Three questions, one scene each.

A. ANGLE.  Straight edges radiating from a point, viewed down the
   orthographic axis they lie in, so one frame carries every screen
   angle at once.  The measurement is the INK each line lays down: the
   integral of its coverage across a perpendicular cut, in pixels.  A
   line of width W that holds its weight at every orientation
   integrates to W at every orientation -- that is the whole claim, and
   it is absolute rather than a comparison against a previous build.

B. WIDTH.  The same star at fractional widths.  Ink must track the
   requested width continuously; integer rounding shows up as a
   staircase (1.5 and 2.0 measuring the same).

C. GLASS.  A wireframe part behind a glass slab.  The screen-space
   refraction used to resample the lines out of the scene copy and
   magnify them, so the test is that the ink of an edge seen through
   the glass matches the ink of the same edge beside it.

Every number is computed in LINEAR light: coverage is what the blend
wrote, and the PNG is encoded.
"""
import os
import time

import numpy as np
from PIL import Image

import FreeCAD
import FreeCADGui
import Part

OUT = os.environ.get("LW_OUT", os.path.join(os.path.expanduser("~"),
                                            "lw-probe"))
RESULT = os.path.join(OUT, "result.txt")

_lines = []


class Prefs(object):
    """Set preferences and put them back.

    These probes run against the user's real configuration, not a
    throwaway one, so anything they switch on has to come off again --
    DebugTiming in particular, which otherwise leaves the Report view
    printing a frame line every second forever.
    """

    def __init__(self):
        self._saved = []

    def set(self, group, kind, name, value):
        grp = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/" + group)
        getter = getattr(grp, "Get" + kind)
        setter = getattr(grp, "Set" + kind)
        # ! The plural accessor is GetInts/GetBools/..., not
        # GetIntMap -- and it lists the names present in the group,
        # which is the only way to tell "set to the default value"
        # from "not set", and so whether to restore or remove.
        had = name in list(getattr(grp, "Get" + kind + "s")())
        self._saved.append((grp, kind, name, getter(name), had))
        setter(name, value)

    def restore(self):
        for grp, kind, name, old, had in reversed(self._saved):
            if had:
                getattr(grp, "Set" + kind)(name, old)
            else:
                getattr(grp, "Rem" + kind)(name)
        self._saved = []


def say(msg):
    _lines.append(msg)
    FreeCAD.Console.PrintMessage("[lw] %s\n" % msg)


def pump(n=6):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()


def settle(rounds=10):
    for _ in range(rounds):
        time.sleep(0.05)
        pump(3)


def grab(view, name):
    path = os.path.join(OUT, name + ".png")
    view.saveRenderDump(path, metadata=False)
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float64)


def decode(x):
    c = x / 255.0
    return np.where(c <= 0.04045, c / 12.92,
                    ((c + 0.055) / 1.055) ** 2.4)


def bilinear(lum, x, y):
    """Sample a 2D linear-light luma plane at fractional coordinates."""
    h, w = lum.shape[:2]
    x = np.clip(x, 0.0, w - 1.001)
    y = np.clip(y, 0.0, h - 1.001)
    x0 = np.floor(x).astype(int)
    y0 = np.floor(y).astype(int)
    fx = x - x0
    fy = y - y0
    a = lum[y0, x0] * (1 - fx) * (1 - fy) + lum[y0, x0 + 1] * fx * (1 - fy)
    b = lum[y0 + 1, x0] * (1 - fx) * fy + lum[y0 + 1, x0 + 1] * fx * fy
    return a + b


def luma(lin):
    return lin.mean(axis=2)


def ink(lum, cx, cy, ang, radii, half=6.0, step=0.125):
    """Integrated coverage across the line at `ang`, in pixels.

    Sampled at several radii and averaged, so a stray pixel or the
    star's crowded centre cannot carry the number.  Background is taken
    from the far ends of the cut rather than assumed.
    """
    dx, dy = np.cos(ang), np.sin(ang)
    px, py = -dy, dx           # perpendicular
    offs = np.arange(-half, half + 1e-9, step)
    vals = []
    for r in radii:
        sx = cx + dx * r
        sy = cy + dy * r
        prof = bilinear(lum, sx + px * offs, sy + py * offs)
        bg = 0.5 * (np.mean(prof[:6]) + np.mean(prof[-6:]))
        if bg <= 1e-6:
            continue
        cov = np.clip((bg - prof) / bg, 0.0, 1.0)
        vals.append(float(np.sum(cov) * step))
    return float(np.mean(vals)) if vals else float("nan")


def star(doc, name, n=12, radius=60.0):
    """n straight edges through the origin, evenly spread in angle."""
    edges = []
    for i in range(n):
        a = np.pi * i / n
        v = FreeCAD.Vector(float(np.cos(a)), float(np.sin(a)), 0.0)
        edges.append(Part.makeLine(v * (0.12 * radius), v * radius))
        edges.append(Part.makeLine(v * (-0.12 * radius), v * (-radius)))
    obj = doc.addObject("Part::Feature", name)
    obj.Shape = Part.makeCompound(edges)
    return obj


def prefs(pf, msaa):
    pf.set("View", "Int", "RenderCache", 3)
    pf.set("View", "Bool", "ShowNaviCube", False)
    pf.set("View", "Bool", "ShowAxisCross", False)
    pf.set("View", "Int", "AntiAliasing", msaa)
    pf.set("Document", "Int", "AutoSaveTimeout", 0)
    pf.set("Document", "Bool", "AutoSaveEnabled", False)
    pf.set("View/Render", "String", "Type", "bgfx - OpenGL")
    for off in ("AO", "Shadow", "Bloom", "Volumetric", "Matcap", "Cavity",
                "TemporalAccum", "GroundReflection"):
        pf.set("View/Render", "Bool", off, False)
    pf.set("View/Render", "Int", "OutputTransform", 1)
    pf.set("View/Render", "Int", "DebugViewMode", 0)
    # A flat light background: every number below is ink against it.
    pf.set("View", "Bool", "Simple", True)
    pf.set("View", "Unsigned", "BackgroundColor", 0xffffffff)


def main():
    if not os.path.isdir(OUT):
        os.makedirs(OUT)
    pf = Prefs()
    prefs(pf, 3)

    doc = FreeCAD.newDocument("lwprobe")
    s = star(doc, "Star")
    doc.recompute()
    vo = s.ViewObject
    vo.LineColor = (0.0, 0.0, 0.0)
    vo.PointSize = 1.0
    view = FreeCADGui.ActiveDocument.ActiveView
    view.viewTop()
    view.setCameraType("Orthographic")
    view.fitAll()
    settle(14)

    h, w = grab(view, "warm").shape[:2]
    cx, cy = w * 0.5, h * 0.5
    say("frame    : %dx%d, MSAA 4x, cache 3, bgfx" % (w, h))

    # Where the star's centre actually landed.
    #
    # ! The MEDIAN of the ink, not the mean.  The corner axis cross
    # survives ShowAxisCross=False (it is an overlay feed, not a scene
    # object) and its dark pixels sit 700px off to one side, which drags
    # a centroid far enough that every cut below lands on empty
    # background and reads zero ink.  A median over a symmetric star is
    # the centre and does not care about the corner at all.
    lum0 = luma(decode(grab(view, "centre")))
    bg = float(np.median(lum0))
    dark = (lum0 < bg * 0.5)
    ys, xs = np.nonzero(dark)
    say("background: linear luma %.4f, %d px below half of it"
        % (bg, len(xs)))
    if len(xs) > 200:
        cx, cy = float(np.median(xs)), float(np.median(ys))
    else:
        say("   !! ink mask unusable; falling back to the frame centre")
    say("centre   : %.1f, %.1f" % (cx, cy))
    # Radii off the SHORT axis: fitAll fits the star to it, so a radius
    # scaled from the width overshoots the spokes on a wide viewport and
    # samples background.
    radii = [h * 0.16, h * 0.22, h * 0.28]
    say("radii    : %s (spoke tip near %.0f)"
        % ([round(r) for r in radii], h * 0.36))

    # ---- A. angle -------------------------------------------------
    say("")
    say("A. ink vs screen angle (width 2.0; ideal = 2.00 at every angle)")
    vo.LineWidth = 2.0
    settle(8)
    lum = luma(decode(grab(view, "angle-w2")))
    vals = []
    for i in range(12):
        a = np.pi * i / 12.0
        # Screen y grows downward, so the screen angle is -a.
        k = ink(lum, cx, cy, -a, radii)
        vals.append(k)
        say("   %5.1f deg : %.3f px" % (np.degrees(a), k))
    vals = np.array(vals)
    say("   mean %.3f  min %.3f  max %.3f  spread %.1f%% of mean"
        % (vals.mean(), vals.min(), vals.max(),
           100.0 * (vals.max() - vals.min()) / vals.mean()))

    # ---- B. width -------------------------------------------------
    say("")
    say("B. ink vs requested width (mean over the 12 angles)")
    for wid in (1.0, 1.5, 2.0, 2.5, 3.0, 4.0):
        vo.LineWidth = wid
        settle(6)
        lum = luma(decode(grab(view, "width-%.1f" % wid)))
        vs = np.array([ink(lum, cx, cy, -np.pi * i / 12.0, radii)
                       for i in range(12)])
        say("   width %.1f : ink %.3f px  (spread %.1f%%)"
            % (wid, vs.mean(),
               100.0 * (vs.max() - vs.min()) / max(vs.mean(), 1e-6)))

    # ---- C. glass -------------------------------------------------
    say("")
    say("C. lines behind glass")
    vo.LineWidth = 2.0
    slab = doc.addObject("Part::Box", "Glass")
    slab.Length, slab.Width, slab.Height = 200.0, 34.0, 12.0
    slab.Placement.Base = FreeCAD.Vector(-100.0, -17.0, 30.0)
    doc.recompute()
    gvo = slab.ViewObject
    gvo.Transparency = 60
    for prop, val in (("Render_Glass", True), ("Render_GlassIOR", 1.5),
                      ("Render_GlassRoughness", 0.0)):
        try:
            if not hasattr(gvo, prop):
                gvo.addProperty(
                    "App::PropertyBool" if isinstance(val, bool)
                    else "App::PropertyFloat", prop)
            setattr(gvo, prop, val)
        except Exception as exc:
            say("   !! %s: %s" % (prop, exc))
    settle(16)
    lum = luma(decode(grab(view, "glass")))

    # The slab spans a horizontal band across the middle of the star, so
    # the vertical spoke crosses it: its ink INSIDE the band is the
    # through-glass measurement, outside it the control.
    ang = -np.pi * 0.5
    inb = ink(lum, cx, cy, ang, [h * 0.05, h * 0.08, h * 0.11])
    outb = ink(lum, cx, cy, ang, [h * 0.24, h * 0.28])
    say("   spoke ink through glass : %.3f px" % inb)
    say("   same spoke beside glass : %.3f px" % outb)
    say("   ratio %.2f (1.00 = same width; >1.3 = magnified)"
        % (inb / max(outb, 1e-6)))

    with open(RESULT, "w") as fh:
        fh.write("\n".join(_lines) + "\n")
    say("wrote %s" % RESULT)

    pf.restore()
    for d in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(d)


try:
    main()
except Exception:
    import traceback
    tb = traceback.format_exc()
    FreeCAD.Console.PrintError("[lw] FAILED\n%s\n" % tb)
    try:
        with open(RESULT, "w") as fh:
            fh.write("\n".join(_lines) + "\nFAILED\n" + tb)
    except Exception:
        pass
