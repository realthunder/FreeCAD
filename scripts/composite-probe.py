# Does the readback composite actually put the frame on the screen?
#
# This exists because the question is much harder to answer than it
# sounds, and docs/DeviceAdoption.md section 10 lists four instruments
# that failed it on macOS -- two of which answered with a plausible
# PICTURE rather than with nothing, which is the more dangerous half.
# In particular View3DInventorViewer::saveImage is served by the
# backend's own portable frame dump, so it renders the scene whether the
# composite runs or not: a screenshot taken through it proves nothing.
#
# So this script does NOT capture anything itself. It stages a scene
# that is unmistakable at a glance, holds the window open, and leaves
# the capture to something outside the process that photographs the
# DESKTOP -- the only image that necessarily went through the
# composite. scripts/composite-shot.ps1 is that half.
#
# Three legs, and the point is that they disagree:
#
#   FC_BGFX_READBACK=1 FC_PROBE_HOLD=1    the scene should be there
#   FC_BGFX_READBACK=0 FC_PROBE_HOLD=1    the CONTROL: at render cache
#                                         mode 3 Coin emits no per-frame
#                                         geometry, so a non-GL backend
#                                         with no composite has nothing
#                                         drawing the viewport and it
#                                         should come up EMPTY
#   FC_BGFX_READBACK_VERIFY=1             the in-process instrument's
#                                         verdict, printed and no window
#                                         worth looking at (the probe
#                                         pattern replaces the frame)
#
# A picture that looks right in BOTH of the first two legs is a picture
# of something other than the composite, and should be believed about
# nothing.
import os
import sys

import FreeCAD
import FreeCADGui

# An explicitly EMPTY FC_RENDER_BACKEND means "leave the startup choice
# alone", which is the only way to photograph the PLATFORM DEFAULT --
# RenderParams::selectRenderPath() picks it at startup and naming a
# backend here overwrites it. Unset still means Vulkan, so every caller
# that passes a name keeps working.
BACKEND = os.environ.get("FC_RENDER_BACKEND", "bgfx - Vulkan")
OUT = os.environ.get("FC_PROBE_OUT", "")
READY = os.environ.get("FC_PROBE_READY", "")
HOLD = os.environ.get("FC_PROBE_HOLD", "") not in ("", "0")
SECONDS = float(os.environ.get("FC_PROBE_SECONDS", "12"))
SIZE = os.environ.get("FC_PROBE_SIZE", "900x640")
# Photograph the view as it FIRST comes up, before the fit and before
# the warm-up redraws. The composite is pipelined, so its first frames
# have nothing landed, and everything else in this script deliberately
# waits past that -- which is exactly why the "viewport comes up holding
# garbage" defect survived every instrument here and was caught by a
# person watching the window open.
EARLY = os.environ.get("FC_PROBE_EARLY", "") not in ("", "0")
RENDER = "User parameter:BaseApp/Preferences/View/Render"


def say(line):
    print(line)
    sys.stdout.flush()
    if OUT:
        with open(OUT, "a") as fh:
            fh.write(line + "\n")


p = FreeCAD.ParamGet(RENDER)
if BACKEND:
    p.SetString("Type", BACKEND)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
    "RenderCache", 3)
# The verify line rides on the frame report, which only prints under
# DebugTiming.
p.SetBool("DebugTiming", True)
# A background view hands its render targets back and rebuilds them, and
# this window will sit unfocused while something else photographs it.
p.SetInt("BackgroundReleaseDelay", 0)
# Nothing adaptive: the picture should be the scene, not a rung of the
# fidelity ladder that happened to be current when the shutter opened.
p.SetBool("ProgressiveLoad", False)
p.SetInt("CoarseTessellation", -1)
p.SetInt("CoarseDeferFaces", -1)
p.SetFloat("LevelTolerance", 0.0)
p.SetFloat("LevelScale", 1.0)
p.SetInt("GpuMemoryBudgetMB", 1 << 20)

view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
for off in ("ShowAxisCross", "ShowNaviCube", "CornerCoordSystem", "ShowFPS"):
    view.SetBool(off, False)
# A FLAT background, because this picture is going to be judged by eye
# and the default is not flat: the PBR environment paints the HDR image
# behind the scene, and the viewport gradient runs two or three stops.
# Both make "is anything actually drawn here" a harder question than it
# needs to be. All three gradient stops are set to one mid grey rather
# than hunting for the simple/gradient switch -- equal stops are flat in
# either mode -- and the environment background is turned off so the
# colour behind the boxes is this one and nothing else.
p.SetBool("PBREnvBackground", False)
GREY = 0x50505AFF
for stop in ("BackgroundColor", "BackgroundColor2", "BackgroundColor3",
             "BackgroundColor4"):
    view.SetUnsigned(stop, GREY)


# The engine's own lines, caught with a console observer because a
# GUI-subsystem binary on Windows sends nothing to a pipe.
class Watch(object):
    # Wide on purpose. A filter tuned to the lines you EXPECT cannot
    # show you the line that says the run is invalid -- a mistyped
    # backend name reports "bgfx render not supported", the renderer is
    # never created, and every other line still looks healthy.
    WANT = ("render readback composite", "render frame:", "bgfx",
            "not supported", "ERROR", "Error", "error", "Warning",
            "failed", "Failed", "cannot", "Cannot", "invalid", "Invalid",
            "renderer")

    def __call__(self, notifier, message, level):
        for line in message.splitlines():
            if any(w in line for w in self.WANT):
                say("  | " + line.strip())


def stage():
    from PySide import QtCore, QtWidgets

    doc = FreeCAD.newDocument("CompositeProbe")
    # A 3x3 grid of boxes in nine flat colours, of three different
    # heights. Chosen to be judged by eye in one second: any of the
    # count, the grid, the heights or the colours being wrong is
    # visible without measuring anything.
    colors = [(0.90, 0.20, 0.20), (0.95, 0.60, 0.10), (0.95, 0.90, 0.20),
              (0.30, 0.80, 0.30), (0.20, 0.70, 0.70), (0.20, 0.45, 0.90),
              (0.55, 0.30, 0.85), (0.90, 0.35, 0.70), (0.60, 0.60, 0.62)]
    for i in range(9):
        b = doc.addObject("Part::Box", "Box%d" % i)
        b.Length = 10
        b.Width = 10
        b.Height = 6 + 5 * (i % 3)
        b.Placement.Base = FreeCAD.Vector(15 * (i % 3), 15 * (i // 3), 0)
        b.ViewObject.ShapeColor = colors[i]
    doc.recompute()

    v = FreeCADGui.ActiveDocument.ActiveView
    want = tuple(int(x) for x in SIZE.lower().split("x"))
    area = FreeCADGui.getMainWindow().findChild(QtWidgets.QMdiArea)
    sub = None
    if area:
        sub = area.activeSubWindow()
        if sub is None:
            subs = area.subWindowList()
            sub = subs[-1] if subs else None
    if sub is not None:
        if sub.isMaximized():
            sub.showNormal()
        for _ in range(8):
            got = tuple(v.getSize())
            if got == want:
                break
            sub.resize(sub.width() + (want[0] - got[0]),
                       sub.height() + (want[1] - got[1]))
            FreeCADGui.updateGui()

    if EARLY:
        # One redraw only: enough that the backend has rendered a frame
        # and told the viewer so -- which is what makes the viewer skip
        # its own clear -- and not enough for any copy to have landed.
        # That is the window the defect lived in.
        v.redraw()
        v.waitFrameComplete()
        say("EARLY capture: one frame drawn, nothing landed yet")
        if READY:
            rect = ""
            if sub is not None:
                tl = sub.mapToGlobal(QtCore.QPoint(0, 0))
                dpr = sub.devicePixelRatioF()
                rect = "%d %d %d %d" % (round(tl.x() * dpr),
                                        round(tl.y() * dpr),
                                        round(sub.width() * dpr),
                                        round(sub.height() * dpr))
            with open(READY, "w") as fh:
                fh.write((rect or "ready") + "\n")
        QtCore.QTimer.singleShot(int(SECONDS * 1000), finish_early)
        return

    say("size before fit  %dx%d" % tuple(v.getSize()))
    v.setCameraOrientation((0.4247, 0.1759, 0.3389, 0.8226))
    v.fitAll()
    v.waitFrameComplete()
    say("size after fit   %dx%d" % tuple(v.getSize()))
    # Several frames, because the composite is PIPELINED: the first
    # frames legitimately have nothing landed yet and redraw whatever
    # came before. A shutter opened on frame one would photograph that
    # and call the composite broken.
    for _ in range(40):
        v.redraw()
        v.waitFrameComplete()
    # Fit AGAIN, after the frames have settled. The first fit runs while
    # the subwindow resize is still working through the event loop --
    # the renderer reports its targets rebuilding 400x300 -> 900x640
    # after that point -- so a camera fitted then is fitted to a
    # viewport that no longer exists. Cheap to redo and it makes the
    # picture the same every run.
    say("size before refit %dx%d" % tuple(v.getSize()))
    v.fitAll()
    for _ in range(5):
        v.redraw()
        v.waitFrameComplete()

    # The parameter, not BACKEND: with BACKEND empty this is the whole
    # point of the run, and with it set the two agree anyway.
    say("backend   %s%s" % (p.GetString("Type"),
                            "" if BACKEND else "  (startup default)"))
    say("readback  FC_BGFX_READBACK=%s verify=%s"
        % (os.environ.get("FC_BGFX_READBACK", "(default 1)"),
           os.environ.get("FC_BGFX_READBACK_VERIFY", "(off)")))
    say("viewport  %dx%d  objects 9" % tuple(v.getSize()))

    # Only now: the window is staged and the frames have settled, so a
    # capture triggered by this marker photographs a steady view.
    #
    # The marker carries the 3D SUBWINDOW's rect in screen pixels so the
    # capture can crop to it. That is the only part of the app worth
    # looking at, and cropping keeps the rest of the desktop out of a
    # picture that gets shared. The subwindow title bar is inside the
    # rect deliberately: it keeps the document name visible, so the
    # image still says what it is a picture of.
    #
    # mapToGlobal is in LOGICAL pixels and a screen capture is in
    # PHYSICAL ones, so the ratio has to be applied or the crop lands in
    # the wrong place on any scaled display.
    if READY:
        rect = ""
        if sub is not None:
            tl = sub.mapToGlobal(QtCore.QPoint(0, 0))
            dpr = sub.devicePixelRatioF()
            rect = "%d %d %d %d" % (round(tl.x() * dpr), round(tl.y() * dpr),
                                    round(sub.width() * dpr),
                                    round(sub.height() * dpr))
            say("subwindow %s (dpr %.2f)" % (rect, dpr))
        with open(READY, "w") as fh:
            fh.write((rect or "ready") + "\n")

    if HOLD:
        say("holding %.0fs for an external capture" % SECONDS)

    def finish():
        # Close the document FIRST. It is modified -- nine objects were
        # added -- and quitting with it open raises a modal "save
        # changes?" dialog that stalls the leg with the window still on
        # screen, which is indistinguishable from a leg still running.
        try:
            FreeCAD.closeDocument(doc.Name)
        except Exception:
            pass
        QtCore.QTimer.singleShot(0, QtCore.QCoreApplication.quit)

    QtCore.QTimer.singleShot(int(SECONDS * 1000), finish)


def finish_early():
    from PySide import QtCore
    try:
        for d in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(d)
    except Exception:
        pass
    QtCore.QTimer.singleShot(0, QtCore.QCoreApplication.quit)


def deferred():
    try:
        FreeCAD.Console.AttachObserver(Watch())
        stage()
    except Exception:
        import traceback
        say(traceback.format_exc())
        from PySide import QtCore
        QtCore.QTimer.singleShot(0, QtCore.QCoreApplication.quit)


# A script argument runs BEFORE the event loop, so redraw() and
# waitFrameComplete() here would wait on frames nothing is pumping.
from PySide import QtCore  # noqa: E402
QtCore.QTimer.singleShot(1500, deferred)
