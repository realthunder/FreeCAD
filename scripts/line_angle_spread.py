"""How much does a line's drawn weight vary with its screen angle?

    FreeCAD scripts/line_angle_spread.py      (or over MCP)

The question this exists to settle is whether analytic line coverage
fixes a real defect or an imagined one. A screen-space quad of the right
pixel width is geometrically exact but lands on a different subpixel
phase at every orientation, so without analytic coverage its resolved
weight should wobble as the line turns. Whether that wobble is big
enough to matter is a measurement, not an argument.

The scene is straight edges radiating from a point, so one frame carries
every screen angle at once, and there are NO FACES in it -- this must
not be confounded by the polygon-offset artifact, which is about a face
beside the line eating half of it.

The number per angle is INK: the integral of the line's coverage across
a perpendicular cut, in pixels. A line of width W that holds its weight
integrates to W at every angle. The figure of merit is the SPREAD across
angles, not the mean.

Run at 4x MSAA and again with MSAA off. Without analytic coverage MSAA
is the only thing smoothing the quad's edges, so switching it off is
where the defect, if it is one, has to show.
"""
import os
import time

import numpy as np
from PIL import Image

import FreeCAD
import FreeCADGui
import Part

OUT = os.environ.get("LA_OUT", os.path.join(os.path.expanduser("~"),
                                            "la-probe"))
TAG = os.environ.get("LA_TAG", "run")
RESULT = os.path.join(OUT, "result-%s.txt" % TAG)
LOG = []


def say(m):
    LOG.append(m)
    FreeCAD.Console.PrintMessage("[la] %s\n" % m)


class Prefs(object):
    def __init__(self):
        self._saved = []

    def set(self, group, kind, name, value):
        grp = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/" + group)
        had = name in list(getattr(grp, "Get" + kind + "s")())
        self._saved.append((grp, kind, name,
                            getattr(grp, "Get" + kind)(name), had))
        getattr(grp, "Set" + kind)(name, value)

    def restore(self):
        for grp, kind, name, old, had in reversed(self._saved):
            if had:
                getattr(grp, "Set" + kind)(name, old)
            else:
                getattr(grp, "Rem" + kind)(name)
        self._saved = []


def get_view():
    for v in FreeCADGui.ActiveDocument.mdiViewsOfType("Gui::View3DInventor"):
        if hasattr(v, "redraw") and hasattr(v, "getPointOnScreen"):
            return v
    return None


def settle(view, rounds=14):
    for _ in range(rounds):
        time.sleep(0.05)
        for _ in range(3):
            view.redraw()
            FreeCADGui.updateGui()


def grab(view, name):
    p = os.path.join(OUT, "%s-%s.png" % (TAG, name))
    view.saveRenderDump(p, metadata=False)
    return np.asarray(Image.open(p).convert("RGB")).astype(np.float64)


def decode(x):
    c = x / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def bilinear(lum, x, y):
    h, w = lum.shape[:2]
    x = np.clip(x, 0.0, w - 1.001)
    y = np.clip(y, 0.0, h - 1.001)
    x0 = np.floor(x).astype(int)
    y0 = np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    return (lum[y0, x0] * (1 - fx) * (1 - fy)
            + lum[y0, x0 + 1] * fx * (1 - fy)
            + lum[y0 + 1, x0] * (1 - fx) * fy
            + lum[y0 + 1, x0 + 1] * fx * fy)


def ink(lum, cx, cy, ang, radii, half=6.0, step=0.1):
    dx, dy = np.cos(ang), np.sin(ang)
    px, py = -dy, dx
    offs = np.arange(-half, half + 1e-9, step)
    vals = []
    for r in radii:
        prof = bilinear(lum, cx + dx * r + px * offs, cy + dy * r + py * offs)
        bg = 0.5 * (np.mean(prof[:8]) + np.mean(prof[-8:]))
        if bg <= 1e-6:
            continue
        vals.append(float(np.sum(np.clip((bg - prof) / bg, 0, 1)) * step))
    return float(np.mean(vals)) if vals else float("nan")


def star(doc, n=12, radius=60.0):
    edges = []
    for i in range(n):
        a = np.pi * i / n
        v = FreeCAD.Vector(float(np.cos(a)), float(np.sin(a)), 0.0)
        edges.append(Part.makeLine(v * (0.12 * radius), v * radius))
        edges.append(Part.makeLine(v * (-0.12 * radius), v * (-radius)))
    obj = doc.addObject("Part::Feature", "Star")
    obj.Shape = Part.makeCompound(edges)
    return obj


def main():
    if not os.path.isdir(OUT):
        os.makedirs(OUT)
    pf = Prefs()
    pf.set("View", "Int", "RenderCache", 3)
    pf.set("View", "Bool", "ShowNaviCube", False)
    pf.set("View", "Bool", "ShowAxisCross", False)
    pf.set("Document", "Int", "AutoSaveTimeout", 0)
    pf.set("Document", "Bool", "AutoSaveEnabled", False)
    pf.set("View/Render", "String", "Type", "bgfx - OpenGL")
    for off in ("AO", "Shadow", "Bloom", "Volumetric", "Matcap", "Cavity",
                "TemporalAccum", "GroundReflection"):
        pf.set("View/Render", "Bool", off, False)

    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    doc = FreeCAD.newDocument("laprobe")
    s = star(doc)
    doc.recompute()
    s.ViewObject.LineColor = (0.0, 0.0, 0.0)
    s.ViewObject.LineWidth = 2.0

    view = get_view()
    view.setCameraType("Orthographic")
    view.viewTop()
    view.fitAll()
    settle(view, 16)

    say("tag: %s" % TAG)
    for msaa, label in ((3, "MSAA 4x"), (0, "MSAA off")):
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "AntiAliasing", msaa)
        settle(view, 20)
        img = grab(view, "msaa%d" % msaa)
        h, w = img.shape[:2]
        lum = decode(img).mean(axis=2)
        bg = float(np.median(lum))
        ys, xs = np.nonzero(lum < bg * 0.5)
        if len(xs) < 200:
            say("%s: no ink found, skipping" % label)
            continue
        cx, cy = float(np.median(xs)), float(np.median(ys))
        radii = [h * 0.16, h * 0.22, h * 0.28]
        vals = np.array([ink(lum, cx, cy, -np.pi * i / 12.0, radii)
                         for i in range(12)])
        good = vals[~np.isnan(vals)]
        if len(good) < 8:
            say("%s: only %d angles measured, skipping" % (label, len(good)))
            continue
        say("")
        say("%s  (width 2.0, ideal 2.00 at every angle, %dx%d)"
            % (label, w, h))
        say("  " + "  ".join("%.2f" % v for v in vals))
        say("  mean %.3f  min %.3f  max %.3f  SPREAD %.3f px = %.1f%%"
            % (good.mean(), good.min(), good.max(),
               good.max() - good.min(),
               100.0 * (good.max() - good.min()) / good.mean()))

    with open(RESULT, "w") as fh:
        fh.write("\n".join(LOG) + "\n")
    pf.restore()
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)


try:
    main()
except Exception:
    import traceback
    tb = traceback.format_exc()
    FreeCAD.Console.PrintError("[la] FAILED\n%s\n" % tb)
    try:
        if not os.path.isdir(OUT):
            os.makedirs(OUT)
        with open(RESULT, "w") as fh:
            fh.write("\n".join(LOG) + "\nFAILED\n" + tb)
    except Exception:
        pass
