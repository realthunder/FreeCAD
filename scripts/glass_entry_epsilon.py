"""How thick is the band just inside glass where a decoration vanishes?

    FreeCAD scripts/glass_entry_epsilon.py    (or over MCP)

THE LIMIT.  A decoration in FRONT of a glass body is not seen through
it -- it draws undistorted in ViewGlassLine instead -- so both ends of
the SDF path test for that, and they do not use the same test:

    writer (fc_line_sdf_fs.sh)  discard if fragZ <= gfront.z
    reader (fs_fc_glass.sc)     drop   if aux.z <= entry
                                          + 1.0e-3 * max(|entry|, 1)

The writer's is exact against its OWN texel's glass; the reader's
carries a relative epsilon, because both depths ride 16F channels whose
step near a view depth Z is about Z / 2048.  The gap between the two is
a band: a decoration between the entry surface and 1e-3 of the view
depth behind it is WRITTEN into the field and then DROPPED by every
reader, and it is not in the undistorted pass either because it is not
in front.  It disappears outright.

FIXED.  aux.z now carries the CLEARANCE `fragZ - entry` where the
writer's own texel has glass, and `-fragZ` where it does not, told
apart by sign.  A clearance is millimetres, so 16F resolves it to
microns however far away the camera is, and the comparison it comes
from was made in the writer in full float.  The epsilon now governs
only the off-footprint form, which is what it was always for.  Sign
rather than a flag bit because the aux target is filtered: a flag would
blend to a meaningless in-between wherever two decorations meet, while
a blend of two clearances is still a clearance.

    scene extent   entry viewZ    before      after
          (60mm)        33.6mm    0.037mm    0.0006mm
           300mm       303.0mm    0.265mm    0.0006mm
          3000mm      3000.3mm    3.700mm    0.0006mm
         30000mm     30000.0mm    >20mm      0.0006mm
                                (the whole body)

0.0006mm is this probe's bisection floor, not a measurement: nothing
detectable is lost at any scene size now.

WHAT IS MEASURED.  Not whether the band exists -- the two tests differ,
so it must -- but how wide it is in MODEL units, which is the only form
in which the answer means anything to someone building a part.  A glass
slab is viewed face on, so refraction displaces nothing and the epsilon
is the only thing under test.  A comb of lines is swept from the entry
surface downward into the body and the depth at which it reappears is
bisected.

The prediction is `d = 1e-3 * entryViewZ`, and the thing to notice
about it is what it scales with: the camera's VIEW DEPTH, not the size
of the part being looked at.

Which makes the axis to sweep the extent of the whole SCENE, not the
zoom.  An orthographic camera's `height` does not move it -- a first
version of this probe swept the zoom over 8x and measured the same
33.6mm view depth three times, three identical answers, and would have
reported "the band is a constant 0.036mm" as if that were a result.
`fitAll` is what sets the distance, and it fits the whole document.  So
the hazard is a small glass part inside a LARGE assembly: the camera is
parked far enough back for the assembly, the dead band is scaled to
that, and it does not shrink when you zoom into the part.  Markers are
placed at +-E along the view axis to inflate the scene by E while the
slab stays the same size and centred, then the zoom is set to frame the
slab alone -- which is exactly "big assembly, zoomed into one part".

! Knobs come from os.environ, which is FREECAD'S, not the calling
shell's -- this runs inside the app.  Drive it from a wrapper that sets
os.environ and then exec()s this file.
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
SIDE = float(os.environ.get("GE_SIDE", "60"))
THICK = float(os.environ.get("GE_THICK", "20"))
IOR = float(os.environ.get("GE_IOR", "1.5"))
WIDTH = float(os.environ.get("GE_WIDTH", "3"))
NLINES = int(os.environ.get("GE_LINES", "7"))
# Scene extents to sweep, in mm: how far the rest of the document
# reaches along the view axis while the slab stays 60mm. This, not the
# zoom, is what moves an orthographic camera.
EXTENTS = [float(x) for x in
           os.environ.get("GE_EXTENTS", "0,300,3000,30000").split(",")]
BISECT = 14
RESULT = os.path.join(OUT, "entry_eps.txt")
LOG = []


def say(m):
    LOG.append(m)
    FreeCAD.Console.PrintMessage("[eps] %s\n" % m)


def get_view():
    for v in FreeCADGui.ActiveDocument.mdiViewsOfType("Gui::View3DInventor"):
        if hasattr(v, "redraw") and hasattr(v, "getPointOnScreen"):
            return v
    return None


def settle(view, rounds=10):
    for _ in range(rounds):
        time.sleep(0.04)
        for _ in range(3):
            view.redraw()
            FreeCADGui.updateGui()


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
    doc = FreeCAD.newDocument("glasseps")

    slab = doc.addObject("Part::Box", "Slab")
    slab.Length = slab.Width = SIDE
    slab.Height = THICK
    # Lines spanning the slab, inside it, at a depth this probe drives.
    edges = []
    for i in range(NLINES):
        x = SIDE * (i + 1.0) / (NLINES + 1.0)
        edges.append(Part.makeLine(FreeCAD.Vector(x, 2.0, 0.0),
                                   FreeCAD.Vector(x, SIDE - 2.0, 0.0)))
    comb = doc.addObject("Part::Feature", "Comb")
    comb.Shape = Part.makeCompound(edges)
    doc.recompute()

    comb.ViewObject.LineColor = (0.0, 0.0, 0.0)
    comb.ViewObject.LineWidth = WIDTH
    svo = slab.ViewObject
    svo.Transparency = 60
    # Shaded: the slab's own edges are decorations of its own, and they
    # sit exactly ON the entry surface -- precisely the depth under
    # test, so they would be indistinguishable from the comb.
    if "Shaded" in svo.listDisplayModes():
        svo.DisplayMode = "Shaded"
    for prop, val, kind in (("Render_Glass", True, "App::PropertyBool"),
                            ("Render_GlassIOR", IOR, "App::PropertyFloat"),
                            ("Render_GlassRoughness", 0.0,
                             "App::PropertyFloat")):
        if not hasattr(svo, prop):
            svo.addProperty(kind, prop)
        setattr(svo, prop, val)

    view = get_view()
    if view is None:
        say("NO VIEW")
        return
    view.setCameraType("Orthographic")
    view.viewTop()
    view.fitAll()
    cam = view.getCameraNode()

    say("slab %.0f x %.0f x %.0f, ior %.2f, comb %d lines at width %.0f"
        % (SIDE, SIDE, THICK, IOR, NLINES, WIDTH))
    say("the slab is always 60mm and always framed to fill the view;"
        " only the REST of the scene moves")
    say("")
    say("  scene    entry viewZ   1e-3 * viewZ     measured band"
        "   measured/epsilon")
    say("  extent       (mm)      (the old law)         (mm)")
    rows = []
    for ext in EXTENTS:
        # Inflate the document along the view axis, symmetrically, so
        # the slab stays centred and only the camera's distance moves.
        for old in ("MarkA", "MarkB"):
            o = doc.getObject(old)
            if o:
                doc.removeObject(old)
        if ext > 0.0:
            for nm, sgn in (("MarkA", 1.0), ("MarkB", -1.0)):
                m = doc.addObject("Part::Feature", nm)
                m.Shape = Part.Vertex(0.5 * SIDE, 0.5 * SIDE,
                                      THICK + sgn * ext)
        doc.recompute()
        view.fitAll()
        # Then frame the slab alone: the camera has already been parked
        # by fitAll, and an ortho zoom does not bring it back.
        cam.height.setValue(SIDE * 1.2)
        settle(view)
        # View depth of the entry surface: the slab's top face at
        # z = THICK, under a top view looking down -z.
        campos = cam.position.getValue().getValue()
        entry = float(campos[2]) - THICK
        pred = 1.0e-3 * max(abs(entry), 1.0)

        # The footprint to measure in, well inside the slab so its own
        # silhouette never enters.
        pts = [view.getPointOnScreen(SIDE * f, SIDE * g, THICK)
               for (f, g) in ((0.25, 0.25), (0.75, 0.75))]
        x0 = int(min(pts[0][0], pts[1][0]))
        x1 = int(max(pts[0][0], pts[1][0]))
        y0 = int(min(pts[0][1], pts[1][1]))
        y1 = int(max(pts[0][1], pts[1][1]))

        def ink_at(depth):
            comb.Placement.Base = FreeCAD.Vector(0.0, 0.0, THICK - depth)
            doc.recompute()
            settle(view, 6)
            shown = grab(view, os.path.join(OUT, "eps_shown.png"))
            h = shown.shape[0]
            a, b = (h - 1) - y1, (h - 1) - y0
            return base[a:b, x0:x1].mean() - shown[a:b, x0:x1].mean()

        comb.ViewObject.Visibility = False
        settle(view, 6)
        base = grab(view, os.path.join(OUT, "eps_base.png"))
        comb.ViewObject.Visibility = True
        settle(view, 4)

        # The comb as deep inside the body as it will go: the reference
        # for "the lines are being drawn at all". Not an absolute ink
        # threshold -- the mean over the sampled rectangle falls as the
        # zoom tightens, and a fixed gate of 2.0 rejected a perfectly
        # good 1.62 when the framing changed.
        floor_ = 0.95 * THICK
        deep = ink_at(floor_)
        if deep < 0.3:
            # Not a probe failure: if even the far side of the body is
            # inside the dead band, the band is thicker than the part.
            say("  %6.0f %13.1f %14.4f      > %.1f (the whole body)"
                % (ext, entry, pred, THICK))
            rows.append((ext, entry, pred, float(THICK), False))
            continue
        # Bisect the shallowest depth at which half that ink survives.
        lo, hi = 0.0, floor_
        for _ in range(BISECT):
            mid = 0.5 * (lo + hi)
            if ink_at(mid) < 0.5 * deep:
                lo = mid
            else:
                hi = mid
        band = 0.5 * (lo + hi)
        rows.append((ext, entry, pred, band, True))
        say("  %6.0f %13.1f %14.4f %15.4f %16.2f"
            % (ext, entry, pred, band, band / max(pred, 1e-9)))

    say("")
    if rows:
        say("Bisection floor is %.4f mm; a band at the floor means"
            " nothing measurable is lost." % (0.95 * THICK / 2 ** BISECT))
        say("The slab never changed size. Its dead band: %s"
            % "  ".join("scene %.0fmm -> %s%.3fmm dead"
                        % (r[0], "" if r[4] else ">", r[3])
                        for r in rows))
        worst = max(rows, key=lambda r: r[3])
        say("In a document reaching %.0f mm, anything within %s%.3f mm"
            " inside the entry surface of a %.0fmm glass part is"
            " invisible, however far you zoom in."
            % (worst[0], "" if worst[4] else "at least ", worst[3],
               SIDE))
        if any(not r[4] for r in rows):
            say("At the largest extent the band swallowed the whole"
                " %.0fmm body: no decoration inside the glass renders"
                " at all." % THICK)

    with open(RESULT, "w") as f:
        f.write("\n".join(LOG) + "\n")
    say("wrote %s" % RESULT)


main()
