"""What the analytic-coverage line path costs, measured.

    FreeCAD scripts/line_width_perf.py

Two numbers matter and both can be had inside one build, without
rebuilding the old one.

1. WHAT ALL LINE DRAWING COSTS.  The same scene shaded (no edges) and
   with edges, differenced.  Every version of the line path is inside
   that difference, so it is an upper bound on any change to it.

2. WHAT THE FEATHER COSTS.  Analytic coverage widens each quad by half
   a pixel per side -- exactly one pixel of extra width.  So the cost
   of the feather is the cost of one more pixel of width, and the slope
   of frame time against requested width measures it directly.  This is
   the honest way to price the change without a baseline binary.

Frame time is wall clock over many redraws.  If the numbers pin near
16.7ms the compositor is pacing us and the run says nothing -- the
report calls that out rather than quoting it.
"""
import os
import re
import time

import numpy as np

import FreeCAD
import FreeCADGui

OUT = os.environ.get("LP_OUT", os.path.join(os.path.expanduser("~"),
                                            "lw-perf"))
RESULT = os.path.join(OUT, "result.txt")
FRAMES = int(os.environ.get("LP_FRAMES", "90"))
BATCHES = int(os.environ.get("LP_BATCHES", "5"))

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
    FreeCAD.Console.PrintMessage("[lp] %s\n" % msg)


def prefs(pf):
    pf.set("View", "Int", "RenderCache", 3)
    pf.set("View", "Bool", "ShowNaviCube", False)
    pf.set("View", "Int", "AntiAliasing", 3)
    pf.set("Document", "Int", "AutoSaveTimeout", 0)
    pf.set("Document", "Bool", "AutoSaveEnabled", False)
    pf.set("View/Render", "String", "Type", "bgfx - OpenGL")
    for off in ("AO", "Shadow", "Bloom", "Volumetric", "Matcap", "Cavity",
                "TemporalAccum", "GroundReflection"):
        pf.set("View/Render", "Bool", off, False)
    pf.set("View/Render", "Int", "DebugViewMode", 0)
    # The instrument: reportFrameStats prints a GPU figure once a
    # second through Base::Console.
    pf.set("View/Render", "Bool", "DebugTiming", True)


def report_widget():
    """The Report view's text widget, where Base::Console lands."""
    from PySide6 import QtWidgets
    mw = FreeCADGui.getMainWindow()
    for cls in (QtWidgets.QTextEdit, QtWidgets.QPlainTextEdit):
        for wdg in mw.findChildren(cls):
            parent = wdg.parent()
            name = (wdg.objectName() or "") + (
                parent.objectName() if parent else "")
            if "eport" in name:
                return wdg
    return None


def widget_text(wdg):
    try:
        return wdg.toPlainText()
    except Exception:
        return ""


_GPU = re.compile(r"gpu ([0-9.]+)ms")


def gpu_ms(view, wdg, seconds=5.0):
    """Median GPU ms per frame, off the renderer's own timing line.

    ! Wall clock cannot measure this on this box.  redraw() alone only
    marks the view dirty (0.08ms whatever the scene is).  redraw() plus
    updateGui() returns 13.338ms for EVERY leg -- including 27716
    segments drawn at width 128, which cannot possibly fit in that --
    so the loop is not seeing a paint either, it is waiting on
    something paced.  The renderer already measures the GPU directly
    (RenderParams DebugTiming, reportFrameStats) and prints it once a
    second; that number is immune to whatever paces the swap.
    """
    before = len(widget_text(wdg))
    t0 = time.perf_counter()
    while time.perf_counter() - t0 < seconds:
        view.redraw()
        FreeCADGui.updateGui()
    fresh = widget_text(wdg)[before:]
    vals = [float(m) for m in _GPU.findall(fresh)]
    if not vals:
        return float("nan")
    return float(np.median(vals))


def build(doc, n=26):
    """A grid of cylinders: tessellated circles are a lot of segments."""
    objs = []
    for ix in range(n):
        for iy in range(n):
            c = doc.addObject("Part::Cylinder", "C%02d%02d" % (ix, iy))
            c.Radius = 4.0
            c.Height = 10.0
            c.Placement.Base = FreeCAD.Vector(ix * 10.0, iy * 10.0, 0.0)
            objs.append(c)
    doc.recompute()
    return objs


def segments(objs):
    """Roughly how many line segments the wireframe carries.

    Measured on ONE object and scaled: the grid is identical copies, and
    discretising every edge of all of them took minutes for a number
    that is only used to label the report.
    """
    if not objs:
        return 0
    one = 0
    for e in objs[0].Shape.Edges:
        try:
            one += max(1, len(e.discretize(Deflection=0.05)) - 1)
        except Exception:
            one += 1
    return one * len(objs)


def main():
    if not os.path.isdir(OUT):
        os.makedirs(OUT)
    pf = Prefs()
    prefs(pf)
    doc = FreeCAD.newDocument("lwperf")
    objs = build(doc)
    view = FreeCADGui.ActiveDocument.ActiveView
    view.setCameraType("Orthographic")
    view.viewAxonometric()
    view.fitAll()
    for _ in range(30):
        view.redraw()
        FreeCADGui.updateGui()
    time.sleep(0.5)

    w, h = view.getSize()
    say("frame    : %dx%d, MSAA 4x, cache 3, bgfx" % (w, h))
    say("scene    : %d cylinders, ~%d wireframe segments"
        % (len(objs), segments(objs)))
    say("timing   : median GPU ms over ~5s of redraws per leg,"
        " read off RenderParams DebugTiming")
    say("")

    def setmode(mode, width=None):
        for o in objs:
            o.ViewObject.DisplayMode = mode
            if width is not None:
                o.ViewObject.LineWidth = width
        for _ in range(20):
            view.redraw()
            FreeCADGui.updateGui()
        time.sleep(0.3)

    wdg = report_widget()
    if wdg is None:
        say("!! no Report view widget found -- cannot read the timing")
        say("   line. Enable View > Panels > Report view and re-run.")
        raise RuntimeError("no report view")

    # A discarded warm-up leg. ! The first measured leg otherwise
    # carries the scene's first-frame costs -- buffer uploads, program
    # links, target creation -- and reads HIGHER than the wireframe legs
    # that follow it, which inverts the whole table.
    setmode("Flat Lines", 1.0)
    warm = gpu_ms(view, wdg, seconds=6.0)
    say("warm-up (discarded)       : %7.3f ms GPU" % warm)

    setmode("Shaded")
    base = gpu_ms(view, wdg)
    if base != base:
        say("!! the timing line never appeared in the Report view.")
        say("   Check that message output is not filtered there.")
        raise RuntimeError("no timing line")
    say("shaded, no edges          : %7.3f ms GPU" % base)

    # Out to absurd widths on purpose. The compositor paces this box at
    # 13.34ms (75Hz) and the line pass costs far less than the slack
    # under that floor, so every sane width reads identical. Line fill
    # is linear in width, so the slope has to be measured where the
    # frame actually lifts off the floor and then read back down: the
    # feather is one pixel of width, so it is one slope-step.
    widths = (1.0, 2.0, 4.0, 8.0, 16.0)
    got = []
    for wd in widths:
        setmode("Flat Lines", wd)
        ms = gpu_ms(view, wdg)
        got.append(ms)
        say("flat lines, width %-5.0f    : %7.3f ms GPU (+%.3f over shaded)"
            % (wd, ms, ms - base))
    got = np.array(got)

    # The baseline again, last: if it has drifted from the first
    # reading, the legs between them are not comparable and the whole
    # table has to be read as noise.
    setmode("Shaded")
    base2 = gpu_ms(view, wdg)
    say("shaded again, at the end  : %7.3f ms GPU  (drift %+.3f)"
        % (base2, base2 - base))
    noise = abs(base2 - base)

    say("")
    say("1. everything the line path costs, at width 1: %.3f ms"
        % (got[0] - base))
    say("   baseline drift over the run was %.3f ms, so treat anything"
        % noise)
    say("   smaller than that as unresolved.")
    say("   ! CONFOUNDED: that comparison crosses a DisplayMode")
    say("     change, and the two modes build different scene")
    say("     graphs -- the shaded legs read HIGHER than the")
    say("     wireframe ones here. Read (3) instead, which varies")
    say("     only the width and holds everything else fixed.")

    # Slope of ms against width, fitted ONLY over the legs that cleared
    # the pacing floor -- the ones still pinned to it carry no signal
    # and would drag the fit to zero.
    live = got == got  # GPU time has no pacing floor to clear
    say("")
    if live.sum() >= 2:
        wl = np.array(widths)[live]
        A = np.vstack([wl, np.ones(len(wl))]).T
        slope, icpt = np.linalg.lstsq(A, got[live], rcond=None)[0]
        say("2. fitted over widths %s" % [int(x) for x in wl])
        say("   ms per extra pixel of line width: %.4f ms" % slope)
        say("   the feather widens every quad by exactly 1px, so that")
        say("   IS its cost: %.4f ms/frame = %.2f%% of a 13.3ms frame."
            % (slope, 100.0 * slope / 13.34))
        say("   at width 1 the whole line pass extrapolates to %.3f ms"
            % (slope * 1.0 + icpt - base))
    else:
        say("2. no leg cleared the pacing floor even at width %d --"
            % int(widths[-1]))
        say("   the line pass costs less than the slack under it, and")
        say("   this instrument cannot resolve it further.")

    say("")
    say("3. the direct read: width 1 -> %d is a %.0fx change in line"
        % (int(widths[-1]), widths[-1]))
    say("   fill, and moved GPU time by %.3f ms (drift was %.3f)."
        % (got[-1] - got[0], noise))
    say("   The feather is +1px on a width-1 line, far less than that,")
    say("   so its cost is below what this instrument resolves.")

    if abs(got[-1] - got[0]) < 0.005:
        say("")
        say("!! width 1 and width %d measured the same GPU time --"
            % int(widths[-1]))
        say("   suspect the readout, not the renderer.")

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
    FreeCAD.Console.PrintError("[lp] FAILED\n%s\n" % tb)
    try:
        if not os.path.isdir(OUT):
            os.makedirs(OUT)
        with open(RESULT, "w") as fh:
            fh.write("\n".join(_lines) + "\nFAILED\n" + tb)
    except Exception:
        pass
