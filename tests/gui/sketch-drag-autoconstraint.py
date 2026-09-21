"""Auto-constraints suggested for a DRAG, not only for a drawing tool.

Upstream's drag auto-constraint family (ec298e9e9a and its six
follow-ups) gives a drag the same courtesy a drawing tool already had:
hold a dragged point still on top of something and the constraint that
would pin it there is suggested, then written when the button comes up.
The fork had none of it -- dragging a point onto another point left the
two merely near each other, and the sketch under-constrained.

The suggestion is deliberately NOT instant. It waits out a dwell timer
(DragAutoConstraintDelay, 400 ms by default), restarted by every mouse
move, so sweeping a point across the drawing on the way somewhere else
proposes nothing. That is the part a test has to respect: the pointer
must come to rest on the target and stay there longer than the delay
before the button is released.

Driven over the wire rather than with synthetic Qt events, for the same
reason as the sibling auto-constraint tests: the drag only starts from a
PRESELECTED vertex, and synthetic mouse events preselect nothing
(docs/ThinClient.md sec 8.3). A served mirror's pointer does.

What is asserted:

  - dragging one line's start point onto another line's end point, and
    holding it there, leaves a Coincident constraint between exactly
    those two points;
  - dragging a different line's start point out into empty space, with
    the same dwell, leaves no constraint at all -- so the answer is a
    discrimination and not a reflex.

Snapping is switched off so the drag lands where the pointer says and
the tolerance under test is the auto-constraint's own.

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
DOC = "SketchDragAuto"
OBJ = "Sketch"
CLIENT_WAIT_S = 200

EYE = (0.0, 0.0, 60.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 200.0
VW, VH = 800, 600

# Line A (GeoId 0) holds still; its END is the point dragged onto.
A_START = (-20.0, 14.0)
A_END = (-12.0, 14.0)
# Line B (GeoId 1): its START is dragged onto A's end. Deliberately
# slanted, so the drag does not also earn a Horizontal or Vertical
# suggestion off the resulting direction (the 2 degree window).
B_START = (8.0, -14.0)
B_END = (18.0, -18.0)
# Line C (GeoId 2): the control. Its START is dragged to empty space,
# clear of both axes, of the origin, and of every other element.
C_START = (20.0, 8.0)
C_END = (30.0, 4.0)
EMPTY = (24.0, -14.0)

# Longer than DragAutoConstraintDelay (400 ms), which every mouse move
# restarts -- so this is time with the pointer held still.
DWELL_S = 1.2

state = {"doc": None, "client": None, "done": False, "t0": clock(),
         "phase": "start", "before": None, "after_drag": None,
         "after_empty": None}
drag_sampled = threading.Event()
empty_sampled = threading.Event()


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


def drag(ws, from_world, to_world, t):
    """One drag of a vertex, as a browser's drag arrives.

    The move before the press is what preselects the vertex; the press
    only marks the mode. The first move after it starts the drag and is
    consumed doing so, so the pointer has to move again to reach the
    target -- and then hold, because each move restarts the dwell."""
    fx, fy = pixel_of(*from_world)
    tx, ty = pixel_of(*to_world)
    mx, my = (fx + tx) // 2, (fy + ty) // 2

    ws.send(2, wsclient.input_frame(wsclient.MOVE, fx, fy, time_ms=t))
    ws.drain(0.4)
    ws.send(2, wsclient.input_frame(wsclient.PRESS, fx, fy, code=0, time_ms=t + 50))
    ws.drain(0.2)
    # consumed by initDragging
    ws.send(2, wsclient.input_frame(wsclient.MOVE, mx, my, code=0, time_ms=t + 100))
    ws.drain(0.2)
    # the step that actually moves the point, and arms the dwell
    ws.send(2, wsclient.input_frame(wsclient.MOVE, tx, ty, code=0, time_ms=t + 150))
    ws.drain(DWELL_S)
    ws.send(2, wsclient.input_frame(wsclient.RELEASE, tx, ty, code=0, time_ms=t + 1400))
    ws.drain(0.5)


class Client(threading.Thread):
    """The wire conversation, off the GUI thread."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.ready = threading.Event()
        self.dragged = threading.Event()
        self.emptied = threading.Event()
        self.edit_reply = None
        self.reset = None

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()
            self.dragged.set()
            self.emptied.set()

    def talk(self):
        ws = WS(self.port)
        ws.hello("sketch-drag-autoconstraint")
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

        # A: B's start point onto A's end point.
        drag(ws, B_START, A_END, 1000)
        self.dragged.set()
        drag_sampled.wait(40.0)

        # B: C's start point into empty space, same dwell.
        drag(ws, C_START, EMPTY, 4000)
        self.emptied.set()
        empty_sampled.wait(40.0)

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


def point_of(geoId, posId):
    p = sketch().getPoint(geoId, posId)
    return (round(p.x, 4), round(p.y, 4))


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
        # The drag has to land where the pointer says: snapping would
        # move the point itself and hide what the suggestion tolerance
        # actually is.
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Snap").SetBool(
                "Snap", False)
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").SetInt(
                "OnViewParameterVisibility", 0)

        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        sk = doc.addObject("Sketcher::SketchObject", OBJ)
        for a, b in ((A_START, A_END), (B_START, B_END), (C_START, C_END)):
            sk.addGeometry(Part.LineSegment(FreeCAD.Vector(a[0], a[1], 0),
                                            FreeCAD.Vector(b[0], b[1], 0)), False)
        doc.recompute()
        state["before"] = constraints()
        check("the sketch has the three lines", len(sk.Geometry) == 3, len(sk.Geometry))
        check("and no constraints to start with", state["before"] == [], state["before"])
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
            state["phase"] = "dragging"
        elif phase == "dragging" and client.dragged.is_set():
            state["phase"] = "drag-sampled"

            def sample_drag():
                state["after_drag"] = (constraints(), point_of(1, 1))
                drag_sampled.set()

            QtCore.QTimer.singleShot(400, sample_drag)
        elif phase == "drag-sampled" and client.emptied.is_set():
            state["phase"] = "empty-sampled"

            def sample_empty():
                state["after_empty"] = (constraints(), point_of(2, 1))
                empty_sampled.set()

            QtCore.QTimer.singleShot(400, sample_empty)
        elif phase == "empty-sampled" and not client.is_alive():
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


def near(got, want, tol=0.35):
    return abs(got[0] - want[0]) <= tol and abs(got[1] - want[1]) <= tol


def verify():
    client = state["client"]
    try:
        check("the client ran without error", client.error is None, client.error or "")
        check("the hello was answered with a snapshot", client.snapshot)
        reply = reply_of(client.edit_reply)
        check("the client's edit is accepted", reply.get("ok") is True, reply)

        drag_cs, b_start = (state["after_drag"] or ([], None))
        # The drag itself has to have happened, or the rest says nothing.
        check("the dragged point reached the target", b_start is not None and
              near(b_start, A_END), "%s vs %s" % (b_start, A_END))
        coincident = [c for c in drag_cs if c[0] == "Coincident"]
        check("holding it there left one Coincident constraint",
              len(coincident) == 1, drag_cs)
        if coincident:
            typ, first, firstpos, second, secondpos = coincident[0]
            # start = 1, end = 2 in PointPos
            check("between B's start and A's end",
                  sorted([(first, firstpos), (second, secondpos)])
                  == sorted([(1, 1), (0, 2)]),
                  coincident[0])
        check("and nothing else", len(drag_cs) == len(coincident), drag_cs)

        empty_cs, c_start = (state["after_empty"] or ([], None))
        check("the control drag reached empty space", c_start is not None and
              near(c_start, EMPTY), "%s vs %s" % (c_start, EMPTY))
        added = empty_cs[len(drag_cs):]
        check("a drag into empty space added no constraint", added == [], added)

        reset = reply_of(client.reset)
        check("the client's resetEdit is accepted", reset.get("ok") is True, reset)
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    drag_sampled.set()
    empty_sampled.set()
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
