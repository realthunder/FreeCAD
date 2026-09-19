"""The line mid-point auto-constraint (upstream 64054d13c4, 280452bcc8).

Dropping a point on an existing line auto-constrains it to that line.
Where the point lands within 5% of the line's length from its middle,
the constraint upstream suggests is not PointOnObject but Symmetric --
the new point symmetric about the line's two endpoints, which is what
pins it to the middle and keeps it there when the line moves.

Three pieces have to agree for that to happen, and this exercises all
three at once: seekAutoConstraint has to recognise the middle and say
Symmetric, suggestedConstraintsPixmaps has to have an icon for it (a
type with none falls through to the default and the cursor tail shows
nothing), and createAutoConstraints has to know how to write a
three-element Symmetric out of a suggestion that carries only one GeoId.

Driven over the wire rather than with synthetic Qt events, because the
constraint is only suggested for geometry that is PRESELECTED, and
synthetic mouse events preselect nothing. A served mirror's pointer
does: the client states a camera, and a move at the pixel showing the
line's midpoint preselects the edge in that client's own mirror, which
is what the tool's seekAutoConstraint then reads.

What is asserted:

  - a click at the line's MIDDLE, followed by a second point, leaves a
    Symmetric constraint whose third element is the new line's start
    and whose first two are the existing line's ends;
  - a click on the same line but away from the middle leaves a
    PointOnObject instead, which is the discrimination rather than a
    constant answer.

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
DOC = "SketchMidpointAuto"
OBJ = "Sketch"
CLIENT_WAIT_S = 150

EYE = (0.0, 0.0, 60.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 200.0
VW, VH = 800, 600

# The line the new points are dropped on. Kept clear of both axes and of
# the origin point, so the only thing the pointer can preselect is it.
LINE_START = (-4.0, 6.0)
LINE_END = (12.0, 6.0)
MID = ((LINE_START[0] + LINE_END[0]) / 2.0, (LINE_START[1] + LINE_END[1]) / 2.0)
# A quarter of the way along: on the line, and well outside the 5% of
# its length that counts as the middle.
OFF_MID = (LINE_START[0] + (LINE_END[0] - LINE_START[0]) * 0.25, LINE_START[1])

state = {"doc": None, "client": None, "done": False, "t0": clock(),
         "phase": "start", "mid_constraints": None, "off_constraints": None,
         "geo_before": None}
mid_sampled = threading.Event()
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


def pixel_of(x, y, z=0.0):
    """The client pixel (top-left origin) showing world (x, y, z) from
    the stated eye: the height angle applies vertically, as a canvas
    applies it, and the eye looks straight down -z."""
    depth = EYE[2] - z
    half = depth * math.tan(HEIGHT_ANGLE / 2.0)
    scale = (VH / 2.0) / half
    # VH - 1 - py is the flip a canvas pixel goes through to reach
    # Coin's bottom-left y -- desktop (Quarter/Mouse.cpp) and mirror
    # alike -- so the inverse carries that -1. x is not flipped.
    return (int(round(VW / 2.0 + (x - EYE[0]) * scale)),
            int(round(VH / 2.0 - 1.0 - (y - EYE[1]) * scale)))


def click_at(ws, px, py, t):
    """A move, a press and a release at one pixel, as a browser's click
    arrives: the mirror preselects on the move, which is what the tool
    reads, and the tool takes the point on the release."""
    ws.send(2, wsclient.input_frame(wsclient.MOVE, px, py, time_ms=t))
    ws.drain(0.3)
    ws.send(2, wsclient.input_frame(wsclient.PRESS, px, py, code=0, time_ms=t + 100))
    ws.drain(0.1)
    ws.send(2, wsclient.input_frame(wsclient.RELEASE, px, py, code=0, time_ms=t + 200))
    ws.drain(0.3)


def draw_line_from(ws, start_world, end_world, t):
    """One line drawn with Sketcher_CreateLine: the first point on the
    geometry under test, the second in clear space."""
    ws.op('{"id":%d,"op":"command","name":"Sketcher_CreateLine"}' % (t // 1000 + 10))
    ws.drain(0.5)
    px, py = pixel_of(*start_world)
    click_at(ws, px, py, t)
    px, py = pixel_of(*end_world)
    click_at(ws, px, py, t + 400)
    # The tool restarts for another line; take it out of the view.
    # 0xff1b is the X11 keysym for Escape, which is what the wire carries
    # (docs/ThinClient.md sec 8.5) -- not a Qt key code.
    ws.send(2, wsclient.input_frame(wsclient.KEY_DOWN, px, py,
                                    code=0xff1b, time_ms=t + 800))
    ws.send(2, wsclient.input_frame(wsclient.KEY_UP, px, py,
                                    code=0xff1b, time_ms=t + 820))
    ws.drain(0.3)


class Client(threading.Thread):
    """The wire conversation, off the GUI thread."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.ready = threading.Event()
        self.drew_mid = threading.Event()
        self.drew_off = threading.Event()
        self.edit_reply = None
        self.reset = None

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()
            self.drew_mid.set()
            self.drew_off.set()

    def talk(self):
        ws = WS(self.port)
        ws.hello("sketch-midpoint-autoconstraint")
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

        # A: the middle of the line.
        draw_line_from(ws, (MID[0], MID[1]), (MID[0], MID[1] - 12.0), 1000)
        self.drew_mid.set()
        mid_sampled.wait(30.0)

        # B: the same line, a quarter along.
        draw_line_from(ws, (OFF_MID[0], OFF_MID[1]), (OFF_MID[0], OFF_MID[1] - 12.0), 3000)
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
    """(Type, First, FirstPos, Second, SecondPos, Third, ThirdPos) for
    each constraint the sketch carries."""
    return [(c.Type, c.First, c.FirstPos, c.Second, c.SecondPos, c.Third, c.ThirdPos)
            for c in sketch().Constraints]


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

        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        sk = doc.addObject("Sketcher::SketchObject", OBJ)
        sk.addGeometry(Part.LineSegment(
            FreeCAD.Vector(LINE_START[0], LINE_START[1], 0),
            FreeCAD.Vector(LINE_END[0], LINE_END[1], 0)), False)
        doc.recompute()
        state["geo_before"] = len(sk.Geometry)
        check("the sketch has the line to drop points on", state["geo_before"] == 1,
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
            state["phase"] = "drawing-mid"
        elif phase == "drawing-mid" and client.drew_mid.is_set():
            state["phase"] = "mid-sampled"

            def sample_mid():
                state["mid_constraints"] = constraints()
                mid_sampled.set()

            QtCore.QTimer.singleShot(400, sample_mid)
        elif phase == "mid-sampled" and client.drew_off.is_set():
            state["phase"] = "off-sampled"

            def sample_off():
                state["off_constraints"] = constraints()
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
        import Sketcher

        check("the client ran without error", client.error is None, client.error or "")
        check("the hello was answered with a snapshot", client.snapshot)
        reply = reply_of(client.edit_reply)
        check("the client's edit is accepted", reply.get("ok") is True, reply)

        mid = state["mid_constraints"] or []
        # The suggestion carries the line's GeoId only; createAutoConstraints
        # turns it into (line start, line end, the new point).
        symmetric = [c for c in mid if c[0] == "Symmetric"]
        check("a click on the line's middle left a Symmetric constraint",
              len(symmetric) == 1, mid)
        if symmetric:
            typ, first, firstpos, second, secondpos, third, thirdpos = symmetric[0]
            check("it is symmetric about the existing line's two ends",
                  first == 0 and second == 0 and firstpos == 1 and secondpos == 2,
                  symmetric[0])
            check("and its third element is a point on the new line",
                  third == 1 and thirdpos in (1, 2), symmetric[0])
        check("the middle click did not also leave a PointOnObject",
              not any(c[0] == "PointOnObject" for c in mid), mid)

        off = state["off_constraints"] or []
        # Everything the first line left is still there, so count the new ones.
        added = off[len(mid):]
        check("a click a quarter along the same line left a PointOnObject",
              any(c[0] == "PointOnObject" for c in added), added)
        check("and not a second Symmetric, so the middle is discriminated",
              not any(c[0] == "Symmetric" for c in added), added)

        reset = reply_of(client.reset)
        check("the client's resetEdit is accepted", reset.get("ok") is True, reset)
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    mid_sampled.set()
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
