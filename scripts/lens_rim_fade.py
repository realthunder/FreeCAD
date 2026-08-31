"""How much of a lens rim loses its lines to the SDF support radius?

    FreeCAD scripts/lens_rim_fade.py          (or over MCP)

Companion to lens_line_width.py, which measures the width a line keeps
through glass and deliberately insets 25% of the radius to stay clear of
the silhouette -- "a real limit of the method and not what this
measures".  THIS probe measures that excluded rim.

THE LIMIT.  Lines rasterize into a distance field the glass pass
resamples; the field stores distance out to FC_LINE_SDF_RADIUS = 32
pixels and no further.  The glass pass converts field distance to
post-lens pixels through the Jacobian of the refraction mapping,
`dpost = dc / s`, so rebuilding a half width of `halfw` post-lens needs
`dc = s * halfw` of stored field.  Past compression `s = RADIUS / halfw`
the line's own core lies outside the support and the reconstruction
returns nothing.  A ball lens compresses without bound toward its
silhouette, so somewhere inside the rim every line must dissolve.

THE EXPERIMENT.  Reading the rim as empty proves nothing on its own --
three different things empty it:

  (a) the support radius running out;
  (b) the image compressed past resolution, every line smeared into
      one flat tone;
  (c) the refracted ray landing off the end of a finite comb, where
      there is legitimately nothing to see.

(c) is removed by construction: the comb is extended to three sphere
radii either side and its lines run far past the disc vertically, so
whatever the rim samples, a line is there.

(b) is separated from (a) by INK, not by brightness.  Smearing conserves
ink -- the lines blur together but the light they absorb is still
missing -- whereas a dead reconstruction returns none.  Measuring ink
needs the sphere's own contribution gone, so every frame is differenced
against the SAME scene with the comb hidden: limb darkening, the
environment's specular quads and the shading gradient are identical in
both and cancel exactly.  What is left is the comb's ink and nothing
else, binned by radius.

(a) then has one prediction that is the support radius's alone: the
limit is `RADIUS / halfw`, so a THINNER line must keep its ink FURTHER
out.  Sweeping the comb's line width and watching the ink collapse move
is the test.  If it does not move, the support radius is not what
empties the rim.

MEASURED 2026-08-30, sphere r=46, comb pitch 12mm, disc 282px on screen.
Rim ratio = ink over the outermost two bins / ink over r/R 0.70-0.92:

    width   support limit    ior 1.6    ior 1.15
      2px   compression 32      4.04        2.51
      4px               16      3.52        2.44
      8px                8      3.24        2.41
     16px                4      2.60        2.06
     32px                2      1.68        1.66

The signature is there and it is monotone.  But read the size of it
before widening anything:

- The rim never goes empty.  At every width it carries 1.7 to 4.0 times
  the ink of the band inside it -- a ball lens squeezes the whole scene
  into its rim, so that annulus is the DARKEST part of the disc, not a
  fading one.  Nothing dissolves; the earlier guess that edges "read as
  dissolving at the rim" is not what happens.
- At the widths a CAD scene actually uses the cost is small: 2px -> 4px
  loses 13% of relative rim ink through the strong lens and 3% through
  the gentle one.  It only bites at 16-32px, where the half width is a
  large fraction of the 32px support.
- The gentle lens declines 34% across the same sweep although its
  compression never approaches the limit, so a good part of the
  thick-line decline is NOT the support radius -- a thick line at the
  silhouette also runs its own half width off the edge of the glass.
  The support radius's own share is the excess of the 1.6 column over
  the 1.15 one.

So: fair as it stands.  Widening FC_LINE_SDF_RADIUS to 64 would make a
4px line behave like today's 2px -- about 15% more relative rim ink --
and doubles the width of the quad every line rasterizes into the field.
Not worth it for widths <= 4.

! Knobs come from os.environ, which is FREECAD'S, not the calling
shell's -- this runs inside the app.  Drive it from a wrapper that sets
os.environ and then exec()s this file.  (lens_line_width.py uses a
sidecar file instead; that channel has a BOM trap, see its section 5.)

! Do not frame with fitAll().  The comb is three radii wide on purpose,
so fitAll frames the COMB and leaves the sphere a fraction of the
height -- the first run of this probe measured a 6px rim annulus and
could not have resolved anything in it.  The camera height is set
directly from the radius instead.
"""
import os
import time

import numpy as np
from PIL import Image

import FreeCAD
import FreeCADGui
import Part

OUT = os.environ.get("LL_OUT", os.path.join(os.path.expanduser("~"),
                                            "ll-probe"))
IOR = float(os.environ.get("LR_IOR", "1.6"))
RADIUS = float(os.environ.get("LR_RADIUS", "46"))
WIDTHS = [float(x) for x in
          os.environ.get("LR_WIDTHS", "2,4,8,16,32").split(",")]
PITCH = 12.0
SPAN = 3.0                # comb half-extent in sphere radii
NBINS = 24
RESULT = os.path.join(OUT, "rim_ior%s.txt" % os.environ.get("LR_IOR", "1.6"))
SDF_RADIUS = 32.0
LOG = []


def say(m):
    LOG.append(m)
    FreeCAD.Console.PrintMessage("[rim] %s\n" % m)


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


def build(doc):
    n = int(SPAN * RADIUS / PITCH)
    edges = []
    for i in range(-n, n + 1):
        x = i * PITCH
        edges.append(Part.makeLine(
            FreeCAD.Vector(x, -SPAN * RADIUS, 0.0),
            FreeCAD.Vector(x, SPAN * RADIUS, 0.0)))
    comb = doc.addObject("Part::Feature", "Comb")
    comb.Shape = Part.makeCompound(edges)
    lens = doc.addObject("Part::Sphere", "Lens")
    lens.Radius = RADIUS
    lens.Placement.Base = FreeCAD.Vector(0.0, 0.0, RADIUS)
    doc.recompute()
    comb.ViewObject.LineColor = (0.0, 0.0, 0.0)
    gvo = lens.ViewObject
    gvo.Transparency = 60
    # The sphere's own seam edge and pole vertices are black decorations
    # crossing the disc; Shaded drops them from the scene.
    if "Shaded" in gvo.listDisplayModes():
        gvo.DisplayMode = "Shaded"
    for prop, val, kind in (("Render_Glass", True, "App::PropertyBool"),
                            ("Render_GlassIOR", IOR, "App::PropertyFloat"),
                            ("Render_GlassRoughness", 0.0,
                             "App::PropertyFloat")):
        if not hasattr(gvo, prop):
            gvo.addProperty(kind, prop)
        setattr(gvo, prop, val)
    return comb, lens


def grab(view, path):
    view.saveRenderDump(path, metadata=False)
    view.saveRenderDump(path, metadata=False)
    img = np.asarray(Image.open(path).convert("RGB")).astype(float)
    return img.mean(axis=2)


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
    doc = FreeCAD.newDocument("lensrim")
    comb, lens = build(doc)

    view = get_view()
    if view is None:
        say("NO VIEW")
        return
    view.setCameraType("Orthographic")
    view.viewTop()
    view.fitAll()
    cam = view.getCameraNode()
    cam.height.setValue(2.2 * RADIUS)
    settle(view)

    c = lens.Placement.Base
    cs = view.getPointOnScreen(c.x, c.y, c.z)
    es = view.getPointOnScreen(c.x + lens.Radius.Value, c.y, c.z)
    cx, cy = float(cs[0]), float(cs[1])
    rpx = abs(float(es[0]) - cx)
    say("sphere r=%.0f ior=%.2f, comb pitch %.0fmm spanning +-%.0f radii"
        % (RADIUS, IOR, PITCH, SPAN))
    say("disc radius %.0f px on screen" % rpx)
    if rpx < 150:
        say("!! disc too small to resolve a rim; check the framing")
        return

    # The comb hidden: everything the sphere contributes by itself.
    comb.ViewObject.Visibility = False
    settle(view, 8)
    base = grab(view, os.path.join(OUT, "rim_base.png"))
    comb.ViewObject.Visibility = True
    h, w = base.shape[:2]

    # Radial bin index per pixel, in units of the disc radius. Screen y
    # runs down, the projected y runs up; only the radius matters, so
    # the flip cancels.
    yy, xx = np.mgrid[0:h, 0:w]
    rr = np.sqrt((xx - cx) ** 2 + (yy - ((h - 1) - cy)) ** 2) / rpx
    inside = rr < 0.995
    binid = np.minimum((rr * NBINS).astype(int), NBINS - 1)

    say("")
    say("ink = (comb hidden) - (comb shown), mean over each radial bin,"
        " in luminance levels")
    say("a bin at 0 has NO comb ink reaching it at all")
    say("")
    header = "  r/R  " + " ".join("%5.2f" % ((i + 0.5) / NBINS)
                                  for i in range(NBINS))
    rows = []
    for wdt in WIDTHS:
        comb.ViewObject.LineWidth = wdt
        settle(view, 8)
        shown = grab(view, os.path.join(OUT, "rim_ior%.2f_w%g.png"
                                        % (IOR, wdt)))
        ink = base - shown
        prof = []
        for i in range(NBINS):
            m = inside & (binid == i)
            prof.append(float(ink[m].mean()) if m.any() else 0.0)
        rows.append((wdt, prof))

    say(header)
    for wdt, prof in rows:
        say("%5.0fpx " % wdt + " ".join("%5.1f" % v for v in prof))

    # A ball lens squeezes the whole scene INTO its rim, so ink rises
    # toward the silhouette and an absolute "where does ink vanish"
    # threshold is vacuous -- the first version of this reported
    # "collapses beyond r/R 1.00" for every width. What the support
    # radius predicts is not that the rim goes dark but that it goes
    # dark RELATIVE to the band just inside it, and progressively so as
    # the line thickens: the limit is compression RADIUS / halfw, so a
    # 2px line survives 32x and a 32px line only 2x.
    say("")
    say("rim ratio = mean ink over the outermost two bins, divided by"
        " the mean over r/R 0.70-0.92")
    say("the support radius predicts this FALLS as the line thickens")
    say("")
    say("  width  support limit  band ink  rim ink  rim ratio")
    verdict = []
    for wdt, prof in rows:
        outer = [prof[i] for i in range(NBINS)
                 if (i + 0.5) / NBINS >= 0.92]
        band = [prof[i] for i in range(NBINS)
                if 0.70 <= (i + 0.5) / NBINS < 0.92]
        ov = float(np.mean(outer)) if outer else 0.0
        bv = float(np.mean(band)) if band else 0.0
        ratio = ov / bv if bv > 1e-6 else float("nan")
        limit = SDF_RADIUS / (0.5 * wdt)
        verdict.append((wdt, limit, bv, ov, ratio))
        say("  %5.0f %14.0f %9.2f %8.2f %10.2f"
            % (wdt, limit, bv, ov, ratio))

    say("")
    thin = min(verdict, key=lambda v: v[0])
    thick = max(verdict, key=lambda v: v[0])
    say("thinnest %.0fpx rim ratio %.2f (survives compression %.0f);"
        " thickest %.0fpx rim ratio %.2f (survives %.0f)"
        % (thin[0], thin[4], thin[1], thick[0], thick[4], thick[1]))
    if thin[4] > thick[4] * 1.15:
        say("VERDICT: the rim loses ink relative to the band inside it"
            " as the line thickens, by %.0f%% from the thinnest line to"
            " the thickest -- the support radius's own signature."
            % (100.0 * (1.0 - thick[4] / thin[4])))
    else:
        say("VERDICT: the rim ratio does not fall with line width. The"
            " support radius is not what limits the rim in this"
            " configuration.")

    with open(RESULT, "w") as f:
        f.write("\n".join(LOG) + "\n")
    say("wrote %s" % RESULT)


main()
