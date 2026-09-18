"""A sketch tool's click lands where the client pointed (ThinClient 8.3).

A click that hits geometry is placed by the pick; a click in empty space
is placed by projecting the pixel onto the sketch plane. Those are two
different code paths through the same camera and they have to agree.

They did not. ViewProviderSketch::getProjectingLine inlined the DESKTOP
viewer's getNormalizedPosition, aspect correction and all. That
correction exists because nothing sets a desktop SoCamera::aspectRatio,
so its frustum is square and a pixel has to be stretched into it. A
mirror's camera states the client's real aspect -- that is what a mirror
is -- so the correction went in twice and every off-centre x landed
aspect times too far out. On a 4:3 canvas a point aimed at x = -18 was
drawn at x = -24.

Nothing caught it because it is exact at the middle of the canvas, and
the middle of the canvas is where every other served probe clicks.

This draws one line between two off-centre points in an EMPTY sketch --
nothing to pick, nothing to snap to, so the projection is the only thing
placing them -- and reads the endpoints back off the sketch.

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
DOC = "SketchClickPlacement"
OBJ = "Sketch"
CLIENT_WAIT_S = 120

EYE = (0.0, 0.0, 60.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 200.0
VW, VH = 800, 600

# Both well off centre, and off centre in both axes, so a scale error in
# either one shows. Clear of the origin point and of both axes, so
# nothing can be picked or snapped to instead.
START = (-18.0, -9.0)
END = (14.0, 7.0)
# A pixel is about 1/12 of a sketch unit at this camera, so half a unit is
# a loose tolerance for rounding and a tight one for the defect: the
# aspect error put START's x at -24.
TOL = 0.5

state = {"doc": None, "client": None, "done": False, "t0": clock(),
         "phase": "start", "line": None}
drawn = threading.Event()


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
    """The client pixel (top-left origin) showing world (x, y, z): the
    height angle applies vertically, as a browser canvas applies it, and
    the eye looks straight down -z."""
    depth = EYE[2] - z
    half = depth * math.tan(HEIGHT_ANGLE / 2.0)
    scale = (VH / 2.0) / half
    return (int(round(VW / 2.0 + (x - EYE[0]) * scale)),
            int(round(VH / 2.0 - (y - EYE[1]) * scale)))


def click_at(ws, px, py, t):
    ws.send(2, wsclient.input_frame(wsclient.MOVE, px, py, time_ms=t))
    ws.drain(0.3)
    ws.send(2, wsclient.input_frame(wsclient.PRESS, px, py, code=0, time_ms=t + 100))
    ws.drain(0.1)
    ws.send(2, wsclient.input_frame(wsclient.RELEASE, px, py, code=0, time_ms=t + 200))
    ws.drain(0.3)


class Client(threading.Thread):
    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.ready = threading.Event()
        self.edit_reply = None
        self.reset = None

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()
            drawn.set()

    def talk(self):
        ws = WS(self.port)
        ws.hello("serve-sketch-click-placement")
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

        ws.op('{"id":3,"op":"command","name":"Sketcher_CreateLine"}')
        ws.drain(0.5)
        px, py = pixel_of(*START)
        click_at(ws, px, py, 1000)
        px2, py2 = pixel_of(*END)
        click_at(ws, px2, py2, 1400)
        # 0xff1b is the X11 keysym for Escape, which is what the wire
        # carries (docs/ThinClient.md sec 8.5) -- not a Qt key code.
        ws.send(2, wsclient.input_frame(wsclient.KEY_DOWN, px2, py2,
                                        code=0xff1b, time_ms=1800))
        ws.send(2, wsclient.input_frame(wsclient.KEY_UP, px2, py2,
                                        code=0xff1b, time_ms=1820))
        ws.drain(0.3)
        drawn.set()

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


def build():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)
        # The pointer alone places the points: no on-view parameter, and
        # no auto-constraint that could move them.
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").SetInt(
                "OnViewParameterVisibility", 0)

        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        sk = doc.addObject("Sketcher::SketchObject", OBJ)
        sk.ViewObject.Autoconstraints = False
        doc.recompute()
        check("the sketch starts empty", len(sk.Geometry) == 0, len(sk.Geometry))

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
        if state["phase"] == "start" and client.ready.is_set():
            state["phase"] = "drawing"
        elif state["phase"] == "drawing" and drawn.is_set():
            state["phase"] = "sampled"

            def sample():
                geos = sketch().Geometry
                if geos:
                    g = geos[0]
                    state["line"] = ((g.StartPoint.x, g.StartPoint.y),
                                     (g.EndPoint.x, g.EndPoint.y))

            QtCore.QTimer.singleShot(400, sample)
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

        line = state["line"]
        if not check("the two clicks drew a line", line is not None, line):
            finish()
            return

        (sx, sy), (ex, ey) = line
        # The tool may take the clicks in either order; match by proximity.
        if abs(sx - START[0]) > abs(ex - START[0]):
            (sx, sy), (ex, ey) = (ex, ey), (sx, sy)

        check("the first point landed where the client pointed, in x",
              abs(sx - START[0]) < TOL, (sx, START[0], sx - START[0]))
        check("and in y", abs(sy - START[1]) < TOL, (sy, START[1], sy - START[1]))
        check("the second point landed where the client pointed, in x",
              abs(ex - END[0]) < TOL, (ex, END[0], ex - END[0]))
        check("and in y", abs(ey - END[1]) < TOL, (ey, END[1], ey - END[1]))

        reset = reply_of(client.reset)
        check("the client's resetEdit is accepted", reset.get("ok") is True, reset)
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    drawn.set()
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
