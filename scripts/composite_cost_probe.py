"""What it costs to put a bgfx frame on the screen, per route.

The renderer draws a frame; something then has to get it into the
QOpenGLWidget next to everything Qt draws. There are two routes in the
tree and this measures both inside a real frame:

  blit      BGFXView::blit -- wrap bgfx's own colour and depth
            attachments in a GL framebuffer and glBlitFramebuffer them
            across. Costs almost nothing and works only while bgfx is
            running on OpenGL, because only then is the attachment a GL
            texture name.
  readback  BGFXView::blitReadback -- copy the finished frame to system
            memory, upload it into a GL texture, draw a quad. Works on
            every backend, which is why Metal and Vulkan sessions can
            now show a frame at all, and costs a readback plus an upload
            every frame (docs/DeviceAdoption.md section 2).

The third route, device adoption, is what those numbers are the case
for: if Qt owns the device and bgfx adopts it, the frame is handed over
as a texture and neither the copy nor the upload happens.

The route is chosen by environment, read once at startup, so a leg is a
PROCESS -- scripts/composite-cost.sh drives the set. This script is one
leg: it builds a fixed scene, spins the camera so no frame is a no-op,
runs for a fixed time and exits.

  FC_BGFX_READBACK=0   force the GL blit (GL backends only)
  FC_BGFX_READBACK=1   readback wherever the blit cannot run (default)
  FC_BGFX_READBACK=2   readback always -- the comparable leg on GL
  FC_BGFX_READBACK_SYNC=1  spin frames until the copy lands, which is
                       the fully serialized form section 2 costed

!! Launch with FC_SWAP_INTERVAL=0. Vblank-locked, every leg costs
16.7 ms by definition and the whole difference sits in the swap wait.

The numbers come out of the renderer's own once-a-second report lines
("render readback composite", "render cpu phases", "render frame"),
which go to the log, so the run needs --log-file.

Usage: FC_SWAP_INTERVAL=0 FreeCAD --log-file <log> \
           scripts/composite_cost_probe.py
Env:   COMP_COST_SECS     seconds of spinning frames (default 12)
       COMP_COST_OBJECTS  Part::Box count (default 192)
       COMP_COST_SIZE     viewport as WxH (default 1400x900)
       COMP_COST_TYPE     renderer type (default: per platform)
       COMP_COST_EXIT     "1" = quit when done (default 1)
       COMP_COST_GRADIENT "1" = keep the viewer's gradient background
                          (default: flat, so the frame can be judged by
                          eye as well as by the counters)
       COMP_COST_STEP     radians of camera turn per frame (default
                          0.01); a large value is a diagnostic
       COMP_COST_SHOT     path to save a grab of the window, so the run
                          can show the frame REACHED the screen and not
                          only that the composite cost something
"""
import math
import os
import sys
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide.QtCore import QTimer

SECS = float(os.environ.get("COMP_COST_SECS", "12"))
OBJECTS = int(os.environ.get("COMP_COST_OBJECTS", "192"))
EXIT = os.environ.get("COMP_COST_EXIT", "1") == "1"
SIZE = os.environ.get("COMP_COST_SIZE", "1400x900")
# Where to save a grab of the finished window; empty = none.
SHOT = os.environ.get("COMP_COST_SHOT", "")
# Radians of camera turn per frame. Large values are a diagnostic:
# if consecutive frames still come back identical, the scene is not
# what is failing to change.
STEP = float(os.environ.get("COMP_COST_STEP", "0.01"))


def say(text):
    """Diagnostics go to STDERR as well as the report view.

    A GUI run sends FreeCAD.Console to the report view, which the log
    file does not capture -- a scene script that only printed there
    once looked like a script that had not run at all
    (docs/RenderDebug.md 5.2c).
    """
    FreeCAD.Console.PrintMessage("COMPCOST %s\n" % text)
    sys.stderr.write("COMPCOST %s\n" % text)
    sys.stderr.flush()


def default_type():
    if sys.platform == "darwin":
        return "bgfx - Metal"
    return "bgfx - OpenGL"


# Before the first 3D view, both of them: the backend is only created
# at render cache 3, and bgfx::init happens once per process so the
# type string in force is whichever was set before the warm-up.
_view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
_view.SetInt("RenderCache", 3)
_render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
_render.SetString("Type", os.environ.get("COMP_COST_TYPE", default_type()))
_render.SetBool("DebugTiming", True)
# Autosave rewrites the document mid-run and lands inside a timing
# window; the cube adds draws that belong to no route.
_doc = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
_doc.SetInt("AutoSaveTimeout", 0)
_doc.SetBool("AutoSaveEnabled", False)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/NaviCube").SetBool(
    "ShowNaviCube", False)
# A FLAT background by default, the same settings the golden tests' flat
# leg uses. The viewer's gradient is three colours and a radial flag,
# and none of it is the thing under test -- worse, it makes the frame
# hard to JUDGE BY EYE, which is how this composite was finally
# confirmed after four instruments could not see it. A uniform ground
# makes both the geometry and any composite artifact obvious at a
# glance. COMP_COST_GRADIENT=1 restores the gradient.
if os.environ.get("COMP_COST_GRADIENT", "0") == "0":
    _view.SetBool("Gradient", False)
    _view.SetBool("RadialGradient", False)
    _view.SetBool("UseBackgroundColorMid", False)
    _view.SetUnsigned("BackgroundColor", 858993663)


def build_scene():
    doc = FreeCAD.newDocument("CompositeCost")
    # A cube-root grid, so the count is what was asked for and the
    # camera sees depth as well as area.
    side = max(1, int(round(OBJECTS ** (1.0 / 3.0))))
    made = 0
    for i in range(side):
        for j in range(side):
            for k in range(side):
                if made >= OBJECTS:
                    break
                box = doc.addObject("Part::Box", "Box%d" % made)
                box.Length = box.Width = box.Height = 6.0
                box.Placement.Base = FreeCAD.Vector(i * 10.0, j * 10.0,
                                                    k * 10.0)
                made += 1
    doc.recompute()
    say("scene: %d Part::Box solids" % made)
    return doc


def size_view():
    try:
        w, h = (int(v) for v in SIZE.lower().split("x"))
    except ValueError:
        say("COMP_COST_SIZE=%r is not WxH; leaving the window alone" % SIZE)
        return
    mw = FreeCADGui.getMainWindow()
    view = FreeCADGui.ActiveDocument.ActiveView
    # The 3D view is what has to end up at the asked-for size, not the
    # window: the difference is the menu bar, the docks and the status
    # bar, and a frame measured at the wrong resolution is not
    # comparable with one measured at the right one.
    widget = view.getViewer() if hasattr(view, "getViewer") else None
    mw.resize(w + 40, h + 120)
    FreeCADGui.updateGui()
    say("window %dx%d requested for a %dx%d viewport (widget %r)"
        % (w + 40, h + 120, w, h, widget))


class Spinner:
    """One camera step per timer tick, so no frame is a no-op.

    A still camera lets the viewer skip redraws entirely, and a
    composite that never runs measures nothing.
    """

    def __init__(self, view, seconds):
        self.view = view
        self.ticks = 0
        self.deadline = seconds
        self.timer = QTimer()
        self.timer.timeout.connect(self.step)
        self.elapsed = 0.0
        self.timer.start(0)

    def step(self):
        if not hasattr(self, "t0"):
            self.t0 = time.time()
        self.elapsed = time.time() - self.t0
        if self.elapsed >= self.deadline:
            self.timer.stop()
            say("done: %d frames driven in %.1f s" % (self.ticks,
                                                      self.elapsed))
            # The composite's whole point is that the frame reaches
            # the screen, and a milliseconds column cannot say whether
            # it did -- a quad that draws nothing costs the same as one
            # that draws the scene. mw.grab() renders the window
            # through Qt's backing store, into which Qt composites the
            # QOpenGLWidget's framebuffer -- the very framebuffer the
            # quad drew into. A dark grab is a composite that ran and
            # showed nothing.
            if SHOT:
                try:
                    img = FreeCADGui.getMainWindow().grab()
                    img.save(SHOT)
                    say("window grabbed to %s (%dx%d)"
                        % (SHOT, img.width(), img.height()))
                except Exception:
                    traceback.print_exc()
            if EXIT:
                # os._exit, not close(): a modified document turns
                # mw.close() into a modal question nobody answers, and
                # everything this run had to say is already in the log.
                try:
                    FreeCAD.closeDocument(
                        FreeCAD.ActiveDocument.Name)
                except Exception:
                    pass
                os._exit(0)
            return
        self.ticks += 1
        a = self.ticks * STEP
        # A quaternion around a tilted axis: the scene turns in both
        # screen axes, so no cached frame is reusable.
        ax, ay, az = 0.4, 0.3, 0.866
        s = math.sin(a * 0.5)
        self.view.setCameraOrientation((ax * s, ay * s, az * s,
                                        math.cos(a * 0.5)))
        self.view.redraw()


def main():
    build_scene()
    view = FreeCADGui.ActiveDocument.ActiveView
    view.viewAxonometric()
    view.fitAll()
    size_view()
    say("readback mode %s, sync %s"
        % (os.environ.get("FC_BGFX_READBACK", "(default 1)"),
           os.environ.get("FC_BGFX_READBACK_SYNC", "0")))
    global _spinner
    _spinner = Spinner(view, SECS)


try:
    QTimer.singleShot(600, main)
except Exception:
    traceback.print_exc()
