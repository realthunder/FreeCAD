"""The line-extension auto-constraint hint, and the snap it offers.

Group C of the keystone: the subsystem that shows where a constraint a
tool is about to suggest would come from. Its line-extension half looks
for a line whose *prolongation* passes near the cursor -- past one of
the segment's ends, where nothing is drawn at all -- draws that
prolongation as a dashed hint, suggests a PointOnObject to the line, and
offers the cursor a snap point on it.

That "where nothing is drawn" is what makes it testable without pixels.
No other part of the auto-constraint search can suggest anything out
there: preselection needs geometry under the pointer and there is none,
and alignment and tangency speak about the direction being drawn, not
about a line metres away. So a PointOnObject on the far side of a
segment's end can only have come from group C.

It is also the one place the port had to be rewritten rather than
translated. Upstream gates the hint on whether it would be visible, and
answers that by asking getActiveView() for a View3DInventor and then its
GL widget for a width and height. A mirror has neither: "which window is
active" has no useful answer in a process serving several browsers, and
a mirror has no widget (docs/ThinClient.md sec 8.3). The fork asks the
edit session's viewer for its viewport region instead. Driving this over
the wire is therefore not incidental -- a desktop-only test would never
exercise the code that had to change.

What is asserted:

  - a first click on the prolongation of an existing segment, well past
    its end, leaves a PointOnObject tying the new line's start to that
    segment;
  - the point it leaves is EXACTLY on the prolongation, not merely near
    it: the pixel clicked does not invert to y == 6.0, so an unsnapped
    tool cannot produce that answer;
  - a click the same distance out but three units off the prolongation
    leaves no PointOnObject and lands where it was clicked, which is the
    discrimination rather than a constant answer.

On-view parameters are switched off so the tool is driven by the
pointer alone.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt.
"""
import math
import os
import threading
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

import wsclient
from wsclient import WS, free_port

clock = time.perf_counter

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchLineExtensionAuto"
OBJ = "Sketch"
CLIENT_WAIT_S = 150

EYE = (0.0, 0.0, 60.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 200.0
# Deliberately taller than the 800x600 the other tests use. The search
# distance is 0.1 * ViewProviderSketch::getScaleFactor(), and that factor
# is a world length proportional to what the camera shows, so the band is
# a fixed ~0.002 * VH PIXELS wide however the view is zoomed: 1.2 px at
# 600, which no integer pixel can be relied on to land inside, and 3.6 px
# here, which leaves room for the half-pixel of rounding and the one
# pixel the mirror's projection sits below this helper's model.
VW, VH = 2400, 1800

# The segment whose prolongation is under test. Horizontal, kept clear of
# both axes and of the origin point, and stopping well short of where the
# clicks land so that nothing is drawn there to preselect.
SEG_Y = 6.0
SEG_START = (-14.0, SEG_Y)
SEG_END = (-6.0, SEG_Y)

# On the prolongation, far past SEG_END and still inside the view: the
# hint has to be on screen for the search to offer it.
ON_EXT = (10.0, SEG_Y)
ON_END = (16.0, 2.0)
# Three units above the prolongation -- a hundred pixels here, and thirty
# times the search distance, so this one must come back unconstrained and
# unsnapped. It is also kept well off the prolongation of the line case A
# draws, which runs down-right from ON_EXT: a control that happens to lie
# on the extension of the line just drawn measures nothing, because group
# C will quite correctly snap it to that one instead.
OFF_EXT = (10.0, SEG_Y + 3.0)
OFF_END = (16.0, 14.0)

state = {"doc": None, "client": None, "done": False, "t0": clock(),
         "phase": "start", "on_constraints": None, "off_constraints": None,
         "on_start": None, "off_start": None, "geo_before": None,
         "on_clicked_y": None, "off_clicked_y": None}
on_sampled = threading.Event()
off_sampled = threading.Event()


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def camera_frame():
    return wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH)


def scale_px_per_unit():
    """Pixels per world unit on the sketch plane, for this eye."""
    half = EYE[2] * math.tan(HEIGHT_ANGLE / 2.0)
    return (VH / 2.0) / half


def pixel_of(x, y, z=0.0):
    """The client pixel (top-left origin) showing world (x, y, z) from
    the stated eye: the height angle applies vertically, as a canvas
    applies it, and the eye looks straight down -z."""
    depth = EYE[2] - z
    half = depth * math.tan(HEIGHT_ANGLE / 2.0)
    scale = (VH / 2.0) / half
    return (int(round(VW / 2.0 + (x - EYE[0]) * scale)),
            int(round(VH / 2.0 - (y - EYE[1]) * scale)))


def world_y_of_pixel(py):
    """The inverse of pixel_of's y, which is what an unsnapped tool would
    place. Integer pixels do not land on SEG_Y, and that gap is what
    tells a snapped answer from an unsnapped one."""
    return (VH / 2.0 - py) / scale_px_per_unit()


def click_at(ws, px, py, t):
    """A move, a press and a release at one pixel, as a browser's click
    arrives: the tool reads the auto-constraint search on the move, and
    the point it stores is the one the release commits."""
    ws.send(2, wsclient.input_frame(wsclient.MOVE, px, py, time_ms=t))
    ws.drain(0.3)
    ws.send(2, wsclient.input_frame(wsclient.PRESS, px, py, code=0, time_ms=t + 100))
    ws.drain(0.1)
    ws.send(2, wsclient.input_frame(wsclient.RELEASE, px, py, code=0, time_ms=t + 200))
    ws.drain(0.3)


def draw_line_from(ws, start_world, end_world, t):
    """One line drawn with Sketcher_CreateLine: the first point is the
    one under test, the second in clear space away from the segment."""
    ws.op('{"id":%d,"op":"command","name":"Sketcher_CreateLine"}' % (t // 1000 + 10))
    ws.drain(0.5)
    px, py = pixel_of(*start_world)
    click_at(ws, px, py, t)
    px2, py2 = pixel_of(*end_world)
    click_at(ws, px2, py2, t + 400)
    # The tool restarts for another line; take it out of the view.
    # 0xff1b is the X11 keysym for Escape, which is what the wire carries
    # (docs/ThinClient.md sec 8.5) -- not a Qt key code.
    ws.send(2, wsclient.input_frame(wsclient.KEY_DOWN, px2, py2,
                                    code=0xff1b, time_ms=t + 800))
    ws.send(2, wsclient.input_frame(wsclient.KEY_UP, px2, py2,
                                    code=0xff1b, time_ms=t + 820))
    ws.drain(0.3)
    return py


class Client(threading.Thread):
    """The wire conversation, off the GUI thread."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.ready = threading.Event()
        self.drew_on = threading.Event()
        self.drew_off = threading.Event()
        self.edit_reply = None
        self.reset = None
        self.on_py = None
        self.off_py = None

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()
            self.drew_on.set()
            self.drew_off.set()

    def talk(self):
        ws = WS(self.port)
        ws.hello("sketch-line-extension-autoconstraint")
        self.snapshot = ws.next_binary(20.0) is not None
        if not self.snapshot:
            self.ready.set()
            return
        ws.next_binary(0.5)
        ws.send(2, camera_frame())
        ws.drain(0.5)
        self.ready.set()

        self.edit_reply = ws.op('{"id":2,"op":"edit","obj":"%s","mode":0}' % OBJ)
        ws.drain(0.5)

        # A: on the segment's prolongation, past its end.
        self.on_py = draw_line_from(ws, ON_EXT, ON_END, 1000)
        self.drew_on.set()
        on_sampled.wait(30.0)

        # B: the same distance out, three units off it.
        self.off_py = draw_line_from(ws, OFF_EXT, OFF_END, 3000)
        self.drew_off.set()
        off_sampled.wait(30.0)

        self.reset = ws.op('{"id":9,"op":"resetEdit"}')
        ws.drain(0.5)
        ws.close()


def reply_of(raw):
    if raw is None:
        return {}
    try:
        import json
        return json.loads(raw.decode("utf-8"))
    except Exception:
        return {}


def sketch():
    return state["doc"].getObject(OBJ)


def constraints():
    """(Type, First, FirstPos, Second, SecondPos) for each constraint."""
    return [(c.Type, c.First, c.FirstPos, c.Second, c.SecondPos)
            for c in sketch().Constraints]


def start_point_of(geo_index):
    """The start point of the line the tool just drew, or None."""
    geo = sketch().Geometry
    if geo_index >= len(geo):
        return None
    g = geo[geo_index]
    if not hasattr(g, "StartPoint"):
        return None
    return (g.StartPoint.x, g.StartPoint.y)


def build():
    try:
        import Part

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)
        # Driven by the pointer alone: an on-view parameter would take
        # the click before the auto-constraint is ever consulted.
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").SetInt(
                "OnViewParameterVisibility", 0)
        # The hints are what is under test; the default is on, and this
        # says so rather than relying on it.
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/General").SetBool(
                "ShowDirectionalAutoConstraintHints", True)

        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        sk = doc.addObject("Sketcher::SketchObject", OBJ)
        sk.addGeometry(Part.LineSegment(
            FreeCAD.Vector(SEG_START[0], SEG_START[1], 0),
            FreeCAD.Vector(SEG_END[0], SEG_END[1], 0)), False)
        doc.recompute()
        state["geo_before"] = len(sk.Geometry)
        check("the sketch has the segment to extend", state["geo_before"] == 1,
              state["geo_before"])
        check("auto-constraints are on", sk.ViewObject.Autoconstraints,
              sk.ViewObject.Autoconstraints)

        port = free_port()
        ok = FreeCADGui.serveDocument(doc, port)
        if not check("the document is served", ok, "port %d" % port):
            finish()
            return
        state["client"] = Client(port)
        state["client"].start()
        QtCore.QTimer.singleShot(50, poll)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def poll():
    client = state["client"]
    if client.error:
        QtCore.QTimer.singleShot(200, verify)
        return
    try:
        phase = state["phase"]
        if phase == "start" and client.ready.is_set():
            state["phase"] = "drawing-on"
        elif phase == "drawing-on" and client.drew_on.is_set():
            state["phase"] = "on-sampled"

            def sample_on():
                state["on_constraints"] = constraints()
                state["on_start"] = start_point_of(1)
                state["on_clicked_y"] = world_y_of_pixel(client.on_py)
                on_sampled.set()

            QtCore.QTimer.singleShot(400, sample_on)
        elif phase == "on-sampled" and client.drew_off.is_set():
            state["phase"] = "off-sampled"

            def sample_off():
                state["off_constraints"] = constraints()
                state["off_start"] = start_point_of(2)
                state["off_clicked_y"] = world_y_of_pixel(client.off_py)
                off_sampled.set()

            QtCore.QTimer.singleShot(400, sample_off)
        elif phase == "off-sampled" and not client.is_alive():
            state["phase"] = "end"
    except Exception:
        note("ABORT poll:\n" + traceback.format_exc())
        QtCore.QTimer.singleShot(200, verify)
        return
    if client.is_alive():
        if clock() - state["t0"] > CLIENT_WAIT_S:
            check("the client finished", False,
                  "still talking after %ds in phase %s" % (CLIENT_WAIT_S, state["phase"]))
            finish()
            return
        QtCore.QTimer.singleShot(20, poll)
        return
    QtCore.QTimer.singleShot(500, verify)


def verify():
    client = state["client"]
    try:
        check("the client ran without error", client.error is None, client.error or "")
        check("the hello was answered with a snapshot", client.snapshot)
        reply = reply_of(client.edit_reply)
        check("the client's edit is accepted", reply.get("ok") is True, reply)

        # The premise the snap assertion rests on: the pixel clicked does
        # not itself invert to the segment's y, so "exactly SEG_Y" cannot
        # be reached by placing the point where it was clicked.
        clicked_y = state["on_clicked_y"]
        check("the clicked pixel does not already sit on the prolongation",
              clicked_y is not None and abs(clicked_y - SEG_Y) > 1e-6,
              "pixel inverts to y=%r" % (clicked_y,))

        on = state["on_constraints"] or []
        on_pooj = [c for c in on if c[0] == "PointOnObject"]
        check("a click past the segment's end left a PointOnObject", len(on_pooj) == 1, on)
        if on_pooj:
            typ, first, firstpos, second, secondpos = on_pooj[0]
            check("it ties the new line's start to the segment",
                  first == 1 and firstpos == 1 and second == 0, on_pooj[0])

        on_start = state["on_start"]
        check("the new line starts exactly on the prolongation",
              on_start is not None and abs(on_start[1] - SEG_Y) < 1e-9,
              "start=%r, clicked y=%r" % (on_start, clicked_y))

        off = state["off_constraints"] or []
        # Everything the first line left is still there, so count the new ones.
        added = off[len(on):]
        check("a click three units off the prolongation left no PointOnObject",
              not any(c[0] == "PointOnObject" for c in added), added)

        off_start = state["off_start"]
        off_clicked_y = state["off_clicked_y"]
        check("and that point landed where it was clicked, unsnapped",
              off_start is not None and off_clicked_y is not None
              and abs(off_start[1] - off_clicked_y) < 0.1
              and abs(off_start[1] - SEG_Y) > 1.0,
              "start=%r, clicked y=%r" % (off_start, off_clicked_y))

        reset = reply_of(client.reset)
        check("the client's resetEdit is accepted", reset.get("ok") is True, reset)
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    on_sampled.set()
    off_sampled.set()
    try:
        FreeCADGui.getDocument(DOC).resetEdit()
    except Exception:
        pass
    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, build)
