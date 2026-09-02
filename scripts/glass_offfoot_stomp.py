"""Can a line IN FRONT of glass erase one behind it, without being drawn?

    FreeCAD scripts/glass_offfoot_stomp.py    (or over MCP)

THE RISK AS FILED.  The SDF writer discards a decoration that is in
front of the glass only where its OWN texel has glass over it.  Off the
glass footprint it cannot make that call -- and it must still write,
because the reader samples at the REFRACTED uv; culling there is what
broke the method once before.  So off the footprint a front-of-glass
line and a behind-glass line compete for the same texels wherever their
supports overlap, ranked by distance to the edge alone.  Where the
FRONT one wins, the reader recognises it as in front and returns no
coverage, and the behind line -- which had support there too -- is not
consulted.  One decoration erases another and puts nothing in its
place.

WHICH ALL DEPENDS ON A PREMISE: that a glass pixel ever samples a texel
with no glass on it.  That is what this probe measures first, and it is
the whole answer for a convex body.

WHAT IS MEASURED.  A line is swept across the disc and, for each
position, two things are read off one frame: where the line really is
(from a row clear of the glass, where it draws undistorted at full
strength) and where it is DRAWN inside the disc (dimmed to
FC_GLASS_LINE_ALPHA and displaced).  The pixel that drew it sampled the
line's true position, so

    sample offset = source - drawn

signed against the disc's axis.  A NEGATIVE offset means the pixel
reached inward, toward the axis, and every texel it can reach is inside
the silhouette.  A POSITIVE one means it reached outward, past the
silhouette, onto texels with no glass -- and only then can the
contested-texel case above arise at all.

MEASURED 2026-08-30, ior 1.6.  The risk did NOT reproduce.

  SPHERE (convex).  Sample offset at r/R 0.20 / 0.40 / 0.55 / 0.70 /
  0.80 / 0.88 = -100 / -109 / -93 / -66 / -26 px.  Inward at every
  radius, monotone, never outward.  A convex body bends toward its
  axis, so every texel it can sample is inside its own silhouette and
  no glass pixel ever reads a texel without glass on it.  For a convex
  glass body the contested off-footprint texel CANNOT ARISE.

  TORUS (concave silhouette).  Offsets +3 and +33 px at r/R 0.40 and
  0.55, so the sample does move away from the axis -- but both ends of
  that move stay on the tube (which spans r/R 0.38 to 1.00), so it
  never lands on a glass-free texel either.  With both lines 12px wide
  and 5px apart at that radius, a line BEHIND the glass added 158 ink
  to the window, and a line IN FRONT cost exactly 0.0.  Exactly zero
  because at that radius the front line has glass over its own texel
  too, so the writer discards it before it can reach the field at all.

So: unreachable for a convex body, proven; not reproduced for a torus,
though not proven impossible there.  What is still missing is a body
whose glass pixel samples a texel that is genuinely glass-free -- for a
torus that means an inner-rim pixel reaching INTO the hole, which this
probe cannot yet read because a line lying in the hole draws
undistorted at full strength and swamps any dimmed copy on the tube.
That is the next thing to build if the symptom is ever seen for real.

! Move the line with Placement, not by reassigning Shape.  A
Part::Feature given a fresh Shape each step did not redraw reliably,
and the sweep returned the same two values alternating -- which reads
like frame lag and is not.

! Measure TOTAL ink in a window at the behind line's drawn position.
Scanning the whole disc for its darkest point finds the front line
instead, which draws undistorted at full strength -- that reported
"+144 ink" once, as if adding a line in front had helped.  And a peak
reading cannot see a stomp at all: it eats a line's flanks and leaves
its core alone.

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
IOR = float(os.environ.get("GS_IOR", "1.6"))
RADIUS = float(os.environ.get("GS_RADIUS", "46"))
WIDTH = float(os.environ.get("GS_WIDTH", "4"))
BODY = os.environ.get("GS_BODY", "sphere")
STOMPW = float(os.environ.get("GS_STOMPW", "12"))
STOMPSEP = float(os.environ.get("GS_STOMPSEP", "5"))
RESULT = os.path.join(OUT, "offfoot_%s.txt" % BODY)
LOG = []


def say(m):
    LOG.append(m)
    FreeCAD.Console.PrintMessage("[stomp] %s\n" % m)


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


def darkest(row, brow, lo, hi):
    """Where a row is darkest against the line-free base, and by how much."""
    lo, hi = max(0, int(lo)), min(len(row), int(hi))
    if hi - lo < 3:
        return None, 0.0
    d = brow[lo:hi] - row[lo:hi]
    k = int(np.argmax(d))
    return lo + k, float(d[k])


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
    doc = FreeCAD.newDocument("offfoot")

    if BODY == "torus":
        # A concave silhouette: the hole gives pixels a direction in
        # which "outward from the local surface" leaves the body.
        lens = doc.addObject("Part::Torus", "Lens")
        lens.Radius1 = RADIUS
        lens.Radius2 = 0.45 * RADIUS
        lens.Placement.Base = FreeCAD.Vector(0.0, 0.0, 0.45 * RADIUS)
        outer = RADIUS + 0.45 * RADIUS
    else:
        lens = doc.addObject("Part::Sphere", "Lens")
        lens.Radius = RADIUS
        lens.Placement.Base = FreeCAD.Vector(0.0, 0.0, RADIUS)
        outer = RADIUS
    doc.recompute()
    lvo = lens.ViewObject
    lvo.Transparency = 60
    if "Shaded" in lvo.listDisplayModes():
        lvo.DisplayMode = "Shaded"
    for prop, val, kind in (("Render_Glass", True, "App::PropertyBool"),
                            ("Render_GlassIOR", IOR, "App::PropertyFloat"),
                            ("Render_GlassRoughness", 0.0,
                             "App::PropertyFloat")):
        if not hasattr(lvo, prop):
            lvo.addProperty(kind, prop)
        setattr(lvo, prop, val)

    line = doc.addObject("Part::Feature", "Line")
    line.Shape = Part.makeLine(FreeCAD.Vector(0.0, -4.0 * outer, 0.0),
                               FreeCAD.Vector(0.0, 4.0 * outer, 0.0))
    line.ViewObject.LineColor = (0.0, 0.0, 0.0)
    line.ViewObject.LineWidth = WIDTH
    doc.recompute()

    view = get_view()
    if view is None:
        say("NO VIEW")
        return
    view.setCameraType("Orthographic")
    view.viewTop()
    view.fitAll()
    view.getCameraNode().height.setValue(2.6 * outer)
    settle(view)

    c = lens.Placement.Base
    cs = view.getPointOnScreen(0.0, 0.0, float(c.z))
    es = view.getPointOnScreen(outer, 0.0, float(c.z))
    cxs, cys = float(cs[0]), float(cs[1])
    rpx = abs(float(es[0]) - cxs)
    mmpx = outer / rpx
    say("body %s, ior %.2f, outer silhouette %.0f px" % (BODY, IOR, rpx))

    line.ViewObject.Visibility = False
    settle(view, 8)
    base = grab(view, os.path.join(OUT, "off_base_%s.png" % BODY))
    line.ViewObject.Visibility = True
    h, w = base.shape[:2]
    midy = int((h - 1) - cys)
    # A row well clear of the body, where the line draws undistorted.
    freey = max(4, midy - int(1.3 * rpx))

    say("")
    say("  source r/R   source px   drawn px   dimmed by   sample"
        " offset   direction")
    rows = []
    for frac in (0.2, 0.4, 0.55, 0.7, 0.8, 0.88, 0.94, 0.98):
        line.Placement.Base = FreeCAD.Vector(frac * outer, 0.0, 0.0)
        doc.recompute()
        settle(view, 6)
        img = grab(view, os.path.join(OUT, "off_%s_%g.png" % (BODY, frac)))
        srcx, srcink = darkest(img[freey], base[freey], 0, w)
        # Inside the disc only, and off the silhouette itself.
        dx, dink = darkest(img[midy], base[midy],
                           cxs - 0.99 * rpx, cxs + 0.99 * rpx)
        if srcx is None or dx is None or dink < 3.0:
            say("  %10.2f  -- nothing resolved" % frac)
            continue
        off = (srcx - cxs) - (dx - cxs)
        rows.append((frac, srcx, dx, dink, off))
        say("  %10.2f %11d %10d %11.1f %14.1f   %s"
            % (frac, srcx, dx, dink, off,
               "inward" if off < -1.0 else
               ("outward" if off > 1.0 else "none")))

    say("")
    if not rows:
        say("!! nothing measured")
    else:
        outward = [r for r in rows if r[4] > 1.0]
        say("A glass pixel samples the field at (drawn - offset), so a"
            " NEGATIVE offset means it reached toward the axis.")
        if not outward:
            say("VERDICT: every pixel of this body reached INWARD."
                " Its samples never leave the silhouette, so no glass"
                " pixel ever reads a texel without glass on it, and the"
                " contested off-footprint texel cannot arise. The risk"
                " as filed is unreachable for this body.")
        else:
            say("%d of %d positions sampled OUTWARD, up to %.0f px past"
                " the silhouette -- off-footprint texels ARE read here,"
                " so the precondition holds for this body."
                % (len(outward), len(rows),
                   max(r[4] for r in outward)))
            # Precondition is not the symptom. Put a second line a few
            # pixels from the one that samples outward hardest, first
            # BEHIND the glass (which can only add ink) and then in
            # FRONT of it, and watch what the behind one loses.
            best = max(outward, key=lambda r: r[4])
            frac = best[0]
            # Both lines THICK and close. The writer ranks a texel by
            # distance to the EDGE, dc - halfw, so a thin neighbour 6px
            # away never wins a texel inside a 4px line and the first
            # run of this measured a stomp of exactly 0.0. At width 12
            # and 5px apart the neighbour wins texels 3px from the other
            # line's own centre, which is well inside its body -- that
            # is the overlap the risk is about.
            line.ViewObject.LineWidth = STOMPW
            say("")
            say("2. STOMP -- both lines %.0f px wide, %.0f px apart, at"
                " the position that reaches furthest out (r/R %.2f)"
                % (STOMPW, STOMPSEP, frac))
            line.Placement.Base = FreeCAD.Vector(frac * outer, 0.0, 0.0)
            other = doc.addObject("Part::Feature", "Other")
            other.Shape = Part.makeLine(
                FreeCAD.Vector(0.0, -4.0 * outer, 0.0),
                FreeCAD.Vector(0.0, 4.0 * outer, 0.0))
            other.ViewObject.LineColor = (0.0, 0.0, 0.0)
            other.ViewObject.LineWidth = STOMPW
            other.ViewObject.Visibility = False
            doc.recompute()
            settle(view, 6)
            img = grab(view, os.path.join(OUT, "off_%s_alone.png" % BODY))
            # Read ink only where the BEHIND line is drawn. Scanning the
            # whole disc for its darkest point finds the front line
            # instead -- it draws undistorted at full strength, three
            # times darker than anything seen through glass, and the
            # first run of this reported "+144 ink" as if adding a line
            # in front had HELPED.
            wlo, whi = best[2] - 14, best[2] + 15

            def ink_sum(im):
                # TOTAL ink in the window, not the peak. A stomp eats
                # the line's flanks while leaving its core alone, and a
                # peak reading cannot see that at all.
                return float((base[midy, wlo:whi]
                              - im[midy, wlo:whi]).clip(0).sum())

            alone = ink_sum(img)

            def with_other(z, tag):
                other.ViewObject.Visibility = True
                other.Placement.Base = FreeCAD.Vector(
                    frac * outer + STOMPSEP * mmpx, 0.0, z)
                doc.recompute()
                settle(view, 6)
                im = grab(view, os.path.join(OUT, "off_%s_%s.png"
                                             % (BODY, tag)))
                return ink_sum(im)

            behind = with_other(0.0, "behind")
            front = with_other(3.0 * outer, "front")
            say("   total ink in a 29px window at x=%d, where the"
                " behind line draws" % best[2])
            say("   alone                          %.1f" % alone)
            say("   + a line BEHIND the glass       %.1f  %+.1f"
                % (behind, behind - alone))
            say("   + a line IN FRONT of the glass  %.1f  %+.1f"
                % (front, front - alone))
            say("")
            if front < 0.95 * alone and behind >= 0.95 * alone:
                say("VERDICT: the contested-texel case is LIVE and the"
                    " symptom reproduces -- a line in front of the"
                    " glass, drawn nowhere near here, took %.0f%% of"
                    " what the line behind it was contributing."
                    % (100.0 * (alone - front) / max(alone, 1e-9)))
            else:
                say("VERDICT: the precondition holds but the symptom"
                    " does not reproduce: the front line cost the"
                    " behind one %.1f against %.1f for a behind one."
                    " Not a defect that shows on this geometry."
                    % (alone - front, alone - behind))

    with open(RESULT, "w") as f:
        f.write("\n".join(LOG) + "\n")
    say("wrote %s" % RESULT)


main()
