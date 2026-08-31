"""Does an edge seen through glass keep its stated pixel width?

    FreeCAD scripts/lens_line_width.py        (or over MCP)

A glass body refracts by resampling the scene, and resampling a
rasterized line magnifies it. The renderer instead rasterizes lines into
a distance field and lets the glass pass resample THAT, rebuilding the
coverage from the field's own screen gradient -- so the line lands where
the lens puts it while its thickness stays what was asked for.

This is the measurement of that claim. The scene is deliberately bare: a
comb of parallel straight lines and NOTHING else, so a horizontal cut
gives clean isolated bands with no shaded faces, silhouettes or
highlights for a band detector to trip over. An earlier attempt measured
this on a grid of shaded boxes under the sphere and produced widths from
1.0 to 39.2 -- nearly all of it the sphere's own shading, not lines.

The sphere covers the middle of the comb, so one cut crosses the same
lines twice over: warped inside it, untouched outside.
"""
import io
import os
import time

import numpy as np
from PIL import Image

import FreeCAD
import FreeCADGui
import Part

OUT = os.environ.get("LL_OUT", os.path.join(os.path.expanduser("~"),
                                            "ll-probe"))
RESULT = os.path.join(OUT, "result.txt")
WIDTH = float(os.environ.get("LL_WIDTH", "4"))
# How strongly the body bends light. A high IOR on a small sphere
# compresses the scene many-fold near its silhouette, which is
# where the field gets undersampled; a gentle one is the ordinary
# CAD case (a pane, an enclosure).
# ! Not os.environ: this runs INSIDE FreeCAD over MCP, whose environment
# is not the caller's shell. Setting LL_IOR in the shell that launched
# mcp_run left the defaults in place and produced a byte-identical
# "result" that looked like a real second data point. A sidecar file is
# the one channel that does cross.
_CFG = os.path.join(OUT, "config.txt")
IOR, RADIUS, NLINES = 1.6, 46.0, 13
if os.path.isfile(_CFG):
    for _line in io.open(_CFG, encoding="utf-8"):
        _k, _, _v = _line.partition("=")
        if _k.strip() == "ior":
            IOR = float(_v)
        elif _k.strip() == "radius":
            RADIUS = float(_v)
        elif _k.strip() == "lines":
            # lines=1 is the min-composite discriminator: a single
            # line means no contested texels, so if it still comes out
            # thin the nearest-line depth min is innocent and the
            # coverage reconstruction is the bug.
            NLINES = int(_v)
LOG = []


def say(m):
    LOG.append(m)
    FreeCAD.Console.PrintMessage("[ll] %s\n" % m)


def get_view():
    for v in FreeCADGui.ActiveDocument.mdiViewsOfType("Gui::View3DInventor"):
        if hasattr(v, "redraw") and hasattr(v, "getPointOnScreen"):
            return v
    return None


def settle(view, rounds=18):
    for _ in range(rounds):
        time.sleep(0.05)
        for _ in range(3):
            view.redraw()
            FreeCADGui.updateGui()


def bands(row, lo, hi, alpha=1.0, min_depth=8.0, max_width=40.0):
    """Isolated dark bands, measured where line COVERAGE crosses 0.5.

    The dump is sRGB and the compositing is linear, so a half-max cut
    of raw luminance is not comparable between a full-strength line
    (outside the glass) and one dimmed to FC_GLASS_LINE_ALPHA behind
    it: the same true 4px coverage profile reads ~3.4px one way and
    ~4.0px the other, entirely from the two conventions. Instead,
    invert what the renderer did: linearize, divide out the local
    background (fitted straight across the band, because next to the
    sphere's glint the two sides differ), divide by the pass's alpha
    -- outside 1.0, behind glass FC_GLASS_LINE_ALPHA = 0.4 -- and
    measure where the implied coverage crosses 0.5. A box-filtered
    line of width W crosses 0.5 exactly W apart, whatever the alpha.
    """
    seg = row[lo:hi].astype(float)
    lin = np.power(np.maximum(seg, 0.0) / 255.0, 2.2)
    n = len(seg)
    out = []
    i = 1
    while i < n - 1:
        if seg[i] < seg[i - 1]:
            j = i
            while j < n - 1 and seg[j + 1] <= seg[j]:
                j += 1
            k = j
            while k < n - 1 and seg[k + 1] >= seg[k]:
                k += 1
            lbg, rbg = seg[i - 1], seg[k]
            depth = 0.5 * (lbg + rbg) - seg[j]
            if depth >= min_depth and k > i - 1:
                xs = np.arange(i - 1, k + 1)
                bg = np.interp(xs, [i - 1, k], [lin[i - 1], lin[k]])
                dip = lin[i - 1:k + 1] / np.maximum(bg, 1e-6)
                cov = (1.0 - dip) / alpha
                a = b = None
                jj = j - (i - 1)
                for x in range(0, jj):
                    if cov[x] < 0.5 <= cov[x + 1]:
                        a = x + (0.5 - cov[x]) / max(
                            cov[x + 1] - cov[x], 1e-6)
                        break
                for x in range(len(cov) - 1, jj, -1):
                    if cov[x] < 0.5 <= cov[x - 1]:
                        b = x - (0.5 - cov[x]) / max(
                            cov[x - 1] - cov[x], 1e-6)
                        break
                if (a is not None and b is not None
                        and 0 < b - a <= max_width):
                    out.append((lo + i - 1 + 0.5 * (a + b), b - a,
                                depth))
            i = k
        else:
            i += 1
    return out


def main():
    if not os.path.isdir(OUT):
        os.makedirs(OUT)
    prm = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    prm.SetInt("RenderCache", 3)
    prm.SetBool("ShowNaviCube", False)
    prm.SetBool("ShowAxisCross", False)
    prm.SetInt("AntiAliasing", 0)
    FreeCAD.ParamGet(
        "User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
    d = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
    d.SetInt("AutoSaveTimeout", 0)
    d.SetBool("AutoSaveEnabled", False)

    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    doc = FreeCAD.newDocument("lensline")

    # A comb of parallel lines: nothing else in the scene, so every dark
    # band in a cut is a line and only a line.
    edges = []
    for i in range(NLINES):
        # A single line goes under the sphere centre; a comb starts at 0.
        x = 72.0 if NLINES == 1 else i * 12.0
        edges.append(Part.makeLine(FreeCAD.Vector(x, -70.0, 0.0),
                                   FreeCAD.Vector(x, 70.0, 0.0)))
    comb = doc.addObject("Part::Feature", "Comb")
    comb.Shape = Part.makeCompound(edges)

    lens = doc.addObject("Part::Sphere", "Lens")
    lens.Radius = RADIUS
    lens.Placement.Base = FreeCAD.Vector(72.0, 0.0, RADIUS)
    doc.recompute()

    cvo = comb.ViewObject
    cvo.LineColor = (0.0, 0.0, 0.0)
    cvo.LineWidth = WIDTH
    gvo = lens.ViewObject
    gvo.Transparency = 60
    # The sphere's own seam edge and pole vertices are black decorations
    # too, and they cross the measurement row: near the poles they sit at
    # the glass surface and draw at full strength, which a band detector
    # reads as fat "lines". Shaded mode drops them from the scene.
    if "Shaded" in gvo.listDisplayModes():
        gvo.DisplayMode = "Shaded"
    for prop, val, kind in (("Render_Glass", True, "App::PropertyBool"),
                            ("Render_GlassIOR", IOR, "App::PropertyFloat"),
                            ("Render_GlassRoughness", 0.0,
                             "App::PropertyFloat")):
        if not hasattr(gvo, prop):
            gvo.addProperty(kind, prop)
        setattr(gvo, prop, val)

    view = get_view()
    view.setCameraType("Orthographic")
    view.viewTop()
    view.fitAll()
    settle(view)

    path = os.path.join(OUT, "lensline.png")
    view.saveRenderDump(path, metadata=False)
    img = np.asarray(Image.open(path).convert("RGB")).astype(float)
    h, w = img.shape[:2]
    lum = img.mean(axis=2)
    say("frame %dx%d, comb at LineWidth %.1f, sphere r=%.0f ior=%.2f"
        % (w, h, WIDTH, RADIUS, IOR))

    # Where the sphere lands on screen, computed rather than detected.
    # ! The background is a GRADIENT, so an earlier version that looked
    # for "columns that differ from the background" flagged the entire
    # row, making the inside and outside sets identical and the verdict
    # vacuous. Project the centre and a point one radius away instead.
    mid = h // 2
    c = lens.Placement.Base
    cs = view.getPointOnScreen(c.x, c.y, c.z)
    es = view.getPointOnScreen(c.x + lens.Radius.Value, c.y, c.z)
    cx = float(cs[0])
    rpx = abs(float(es[0]) - cx)
    gl, gr = int(cx - rpx), int(cx + rpx)
    say("sphere centre x=%.0f, radius %.0f px -> columns %d..%d"
        % (cx, rpx, gl, gr))
    if rpx < 40 or gl < 60 or gr > w - 60:
        say("!! the sphere does not sit clear of both frame edges")
        return

    row = lum[mid]
    # Stay clear of the silhouette: within a few pixels of it the
    # refraction offset runs away and the field has no useful
    # gradient, which is a real limit of the method and not what
    # this measures.
    inset = int(rpx * 0.25)
    inside = [b for b in bands(row, gl + inset, gr - inset, alpha=0.4)]
    outside = []
    for b in bands(row, 40, gl - 8):
        outside.append(b)
    for b in bands(row, gr + 8, w - 40):
        outside.append(b)

    say("")
    say("outside the glass: %d bands" % len(outside))
    if outside:
        ws = np.array([b[1] for b in outside])
        say("  widths %s" % " ".join("%.2f" % x for x in ws))
        say("  mean %.2f  min %.2f  max %.2f" % (ws.mean(), ws.min(), ws.max()))
    say("")
    say("through the glass: %d bands" % len(inside))
    if inside:
        wi = np.array([b[1] for b in inside])
        say("  widths %s" % " ".join("%.2f" % x for x in wi))
        say("  mean %.2f  min %.2f  max %.2f" % (wi.mean(), wi.min(), wi.max()))
        # Spacing shows the lens really is warping: evenly spaced
        # outside, unevenly inside.
        cs = np.array([b[0] for b in inside])
        if len(cs) > 2:
            gaps = np.diff(cs)
            say("  centre spacing %s" % " ".join("%.0f" % g for g in gaps))
    if inside and outside:
        say("")
        say("VERDICT: %.2f px through the glass against %.2f px outside"
            % (wi.mean(), ws.mean()))
        say("         (asked for %.1f; ratio %.2f -- 1.00 is the claim,"
            % (WIDTH, wi.mean() / max(ws.mean(), 1e-6)))
        say("         and >1.3 would be the magnification this replaced)")

    with open(RESULT, "w") as fh:
        fh.write("\n".join(LOG) + "\n")


try:
    main()
except Exception:
    import traceback
    tb = traceback.format_exc()
    FreeCAD.Console.PrintError("[ll] FAILED\n%s\n" % tb)
    try:
        if not os.path.isdir(OUT):
            os.makedirs(OUT)
        with open(RESULT, "w") as fh:
            fh.write("\n".join(LOG) + "\nFAILED\n" + tb)
    except Exception:
        pass
