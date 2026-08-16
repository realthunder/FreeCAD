# What does occlusion culling do when the machinery under it fails?
# (docs/FarFieldProxies.md 12.21)
#
# The other harnesses here measure the culling on the rack model, on a real
# GPU, and ask how much it saves. This one asks the opposite question at a
# scale that fits on any machine: turn the knob ON, break one thing at a
# time, and see whether the picture loses anything. 12.2's asymmetry is the
# whole point -- a node drawn when it could have been skipped costs frame
# time, a node skipped when it should have drawn is MISSING GEOMETRY, and
# the resulting frame is fast, which is how that failure disguises itself
# as a result.
#
# The scene is built so an over-cull is visible rather than statistical:
#
#   * a wall, square to the camera, which is the only real occluder;
#   * 64 boxes behind it, magenta, which no correct frame ever shows;
#   * 4 marker boxes, green, three of them standing against the background
#     with nothing in front of them and one in front of the wall. Every
#     correct frame shows all four, whatever the culling does.
#
# Green is what the rows are judged on. Nothing else in the scene can
# produce it, so a row that loses green pixels has deleted geometry the
# eye could see -- no id pass, no reference renderer, no model needed.
#
# /!\ Two controls, because "nothing changed" is also what a mechanism
# that did nothing produces (12.9's correction):
#
#   1. every row prints the backend's own "instances hidden N / drawn M"
#      line, so a row that culled NOTHING says so instead of passing
#      quietly. A run whose rows all read "hidden 0" measures nothing.
#   2. the reference frame is re-grabbed at the end and differenced
#      against the first, so drift is a number rather than a poisoned
#      baseline.
#
# The second half grabs a RUN of consecutive frames per oracle. A verdict
# that flaps deletes geometry in some frames and not others, and one grab
# per row cannot tell that from a clean pass -- the hardware oracle
# (12.6-12.11) is exactly that failure and shows here as an alternating
# series.
#
# Judged on QWidget::grab() of the 3D view, never a capture path: a
# screenshot is not a witness to the screen when the capture path may be
# the broken thing.
#
# Run (headless, software GL is fine -- the failures here are not
# device-dependent):
#
#   cd ~/works/sw/fcad && timeout -k 10 900 .conda/run.sh \
#     xvfb-run -a --server-args='-screen 0 1024x768x24' \
#     env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb PROBE_OUT=/tmp/occl \
#     build/conda-debug-occt801/bin/FreeCAD --user-cfg /tmp/occl/user.cfg \
#     --log-file /tmp/occl/fc.log scripts/occlusion_inject_probe.py
#
# PROBE_OUT is where the report and the PNGs go (default /tmp); the
# --log-file path is read back for the cull stats and defaults to
# PROBE_OUT/fc.log, so pass FC_LOG if it is elsewhere.
import os
import re
import time

import FreeCAD as App
import FreeCADGui as Gui
from PySide6 import QtWidgets
from pivy import coin

OUT = os.environ.get("PROBE_OUT", "/tmp")
LOG = os.environ.get("FC_LOG", os.path.join(OUT, "fc.log"))
R = open(os.path.join(OUT, "occlusion-inject.txt"), "w")


def say(t):
    R.write(t + "\n")
    R.flush()


vgrp = App.ParamGet("User parameter:BaseApp/Preferences/View")
rgrp = App.ParamGet("User parameter:BaseApp/Preferences/View/Render")
vgrp.SetBool("ShowNaviCube", False)
vgrp.SetInt("RenderCache", 3)
rgrp.SetString("Type", "bgfx - OpenGL")
rgrp.SetBool("Matcap", False)
# Gates the "render culling:" report this reads its control from.
rgrp.SetBool("DebugTiming", True)

doc = App.newDocument("Occl")

wall = doc.addObject("Part::Box", "Wall")
wall.Length, wall.Width, wall.Height = 300, 300, 10
wall.Placement.Base = App.Vector(-150, -150, 0)
wall.ViewObject.ShapeColor = (0.6, 0.6, 0.6)

for i in range(8):
    for j in range(8):
        b = doc.addObject("Part::Box", "Hidden%d_%d" % (i, j))
        b.Length = b.Width = b.Height = 20
        b.Placement.Base = App.Vector(-132 + i * 35, -132 + j * 35, -60)
        b.ViewObject.ShapeColor = (1.0, 0.0, 1.0)

for n, pos in enumerate([App.Vector(-230, -20, -30), App.Vector(190, -20, -30),
                         App.Vector(-20, 190, -30), App.Vector(-20, -20, 60)]):
    b = doc.addObject("Part::Box", "Marker%d" % n)
    b.Length = b.Width = b.Height = 40
    b.Placement.Base = pos
    b.ViewObject.ShapeColor = (0.0, 1.0, 0.0)

doc.recompute()

view = Gui.ActiveDocument.ActiveView
view.viewTop()          # down -Z: the wall covers the grid
view.fitAll()
Gui.updateGui()

mw = Gui.getMainWindow()
w3d = None
for w in mw.findChildren(QtWidgets.QWidget):
    if w.metaObject().className() == "Gui::View3DInventor":
        w3d = w
        break
if not w3d:
    say("no 3D view widget; nothing below means anything")

camera = view.getCamera()

# Parked in the wall's own view provider root and switched in for one
# row: a clipped solid may not occlude (12.20), because the CPU
# rasterizer has no clip planes and would stamp the wall's whole surface.
clipsw = coin.SoSwitch()
clip = coin.SoClipPlane()
clip.plane = coin.SbPlane(coin.SbVec3f(0.0, 1.0, 0.0), 0.0)
clipsw.addChild(clip)
clipsw.whichChild = -1
wall.ViewObject.RootNode.insertChild(clipsw, 0)


def settle(n=10):
    for _ in range(n):
        time.sleep(0.15)
        view.redraw()
        Gui.updateGui()


def grab(tag):
    view.setCamera(camera)          # nothing here may reframe between grabs
    settle()
    pm = w3d.grab()
    pm.save(os.path.join(OUT, "oc-%s.png" % tag))
    return pm.toImage()


def count(img, which):
    n = 0
    for y in range(0, img.height(), 2):
        for x in range(0, img.width(), 2):
            p = img.pixel(x, y)
            r, g, b = (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF
            if which == "green" and g > 120 and r < 100 and b < 100:
                n += 1
            elif which == "magenta" and r > 120 and b > 120 and g < 100:
                n += 1
    return n


def diff(a, b):
    n = 0
    for y in range(0, min(a.height(), b.height()), 2):
        for x in range(0, min(a.width(), b.width()), 2):
            if (a.pixel(x, y) & 0xFFFFFF) != (b.pixel(x, y) & 0xFFFFFF):
                n += 1
    return n


HID = re.compile(r"instances hidden (\d+) / drawn (\d+)")


def culled():
    """The last 'render culling' line the backend logged, if any."""
    try:
        with open(LOG, errors="replace") as f:
            hits = HID.findall(f.read())
    except OSError:
        return None
    return (int(hits[-1][0]), int(hits[-1][1])) if hits else None


def setknobs(**kw):
    for k, v in kw.items():
        if isinstance(v, bool):
            rgrp.SetBool(k, v)
        else:
            rgrp.SetInt(k, v)


DEFAULTS = dict(Occlusion=False, OcclusionSoftware=True, OcclusionResolution=1,
                OcclusionOccluderTris=250000, OcclusionSimd=True,
                OcclusionThreads=0, OcclusionCoarse=False,
                OcclusionPerInstance=True, OcclusionConfirm=2)

setknobs(**DEFAULTS)
Gui.updateGui()
ref = grab("00-reference")
ref_green = count(ref, "green")
say("reference (occlusion off): green=%d magenta=%d"
    % (ref_green, count(ref, "magenta")))
if ref_green == 0:
    say("/!\\ no green in the reference: the markers are not on screen and "
        "no row below can lose them")
say("")
say("%-24s %8s %8s %8s  %s"
    % ("row", "changed", "green", "magenta", "culled"))
say("%-24s %8d %8d %8d  %s"
    % ("occlusion off", 0, ref_green, count(ref, "magenta"), "-"))


def row(tag, **kw):
    setknobs(**DEFAULTS)
    setknobs(Occlusion=True, **kw)
    Gui.updateGui()
    img = grab(tag)
    c = culled()
    say("%-24s %8d %8d %8d  %s"
        % (tag, diff(ref, img), count(img, "green"), count(img, "magenta"),
           "hidden %d drawn %d" % c if c else "no stats line"))


row("on-defaults")
row("resolution-4", OcclusionResolution=4)
row("occluder-budget-1", OcclusionOccluderTris=1)
row("simd-off", OcclusionSimd=False)
row("threads-1", OcclusionThreads=1)
row("coarse-hulls", OcclusionCoarse=True)
row("per-instance-off", OcclusionPerInstance=False)
row("hardware-oracle", OcclusionSoftware=False)

# The clipped-occluder guard. With the wall cut away the grid behind it
# is in plain view, and a wall that still stamped its full surface into
# the occlusion buffer would prove that grid hidden.
setknobs(**DEFAULTS)
clipsw.whichChild = 0
Gui.updateGui()
clipref = grab("09-clip-reference")
say("")
say("clip plane, occlusion off:  green=%d magenta=%d"
    % (count(clipref, "green"), count(clipref, "magenta")))
setknobs(Occlusion=True)
Gui.updateGui()
clipon = grab("10-clip-occlusion-on")
c = culled()
say("clip plane, occlusion on:   green=%d magenta=%d  changed=%d  %s"
    % (count(clipon, "green"), count(clipon, "magenta"),
       diff(clipref, clipon), "hidden %d drawn %d" % c if c else "no stats"))

setknobs(**DEFAULTS)
clipsw.whichChild = -1
Gui.updateGui()
again = grab("11-reference-again")
say("")
say("drift (reference re-grabbed at the end): %d px" % diff(ref, again))

# ---- the frame series: one grab per row cannot see a flapping verdict --
say("")
say("consecutive frames, green per frame (the markers are 4 boxes, all "
    "visible in every correct frame):")


def series(tag, frames=10):
    view.setCamera(camera)
    settle(6)
    out = []
    for i in range(frames):
        view.redraw()
        Gui.updateGui()
        time.sleep(0.12)
        img = w3d.grab().toImage()
        out.append(count(img, "green"))
        if i < 3:
            w3d.grab().save(os.path.join(OUT, "oc-series-%s-%d.png" % (tag, i)))
    c = culled()
    say("%-22s %s   (last stats %s)"
        % (tag, " ".join("%4d" % g for g in out),
           "hidden %d drawn %d" % c if c else "-"))


setknobs(**DEFAULTS)
Gui.updateGui()
series("occlusion-off")
setknobs(Occlusion=True)
Gui.updateGui()
series("software-oracle")
setknobs(Occlusion=True, OcclusionSoftware=False)
Gui.updateGui()
series("hardware-oracle")
setknobs(Occlusion=True, OcclusionSoftware=False, OcclusionConfirm=6)
Gui.updateGui()
series("hardware-confirm-6")

setknobs(**DEFAULTS)
R.close()
mw.close()
# The report is written and flushed; lingering here only burns the
# harness timeout (the external `timeout` is still the guarantee).
os._exit(0)
