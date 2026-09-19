"""The tangent auto-constraint search (upstream ed45e20768, b71a54d9cc,
596fa2856b, carried by the f9f76a2516 decomposition).

`seekAutoConstraint` used to be one 300-line function here. Upstream
split it into a preselection half, an alignment half and a tangency
search, and in doing so changed three things this checks:

  - **tangency wins over alignment.** The search now runs the tangency
    pass FIRST and skips the horizontal/vertical suggestion when it
    finds one. Drawing a vertical line tangent to a circle used to
    leave BOTH a Vertical and a Tangent, which over-constrains what the
    user actually aimed at.

  - **the tangency search speaks GeoIds, not loop indices.**
    `getCompleteGeometry()` appends external geometry in REVERSE, so the
    loop index is not a GeoId. The fork's own conversion
    (`getHighestCurveIndex() - tangId`) gets the direction wrong: with a
    single projected circle it yields -1, which is the X AXIS, so the
    tangency was written against the wrong geometry entirely. The sketch
    object's `getGeoIdFromCompleteGeometryIndex` is the only thing that
    knows the mapping.

  - a Tangent is no longer suggested for a preselected line when the
    tool has no direction to be tangent with (596fa2856b); that half is
    not reachable from a click sequence and is left to the suites.

Driven over the wire because an auto-constraint is only suggested for
PRESELECTED geometry and synthetic Qt mouse events preselect nothing;
a served mirror's pointer does. See
tests/gui/sketch-midpoint-autoconstraint.py, the same harness.

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
DOC = "SketchTangentAuto"
OBJ = "Sketch"
SRC = "Sketch2"
CLIENT_WAIT_S = 150

EYE = (0.0, 0.0, 60.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 200.0
VW, VH = 800, 600

R = 5.0
# The sketch's own circle, and a vertical line exactly tangent to it on
# the right. Vertical as well as tangent, which is the point: the two
# suggestions compete.
IN_CENTRE = (20.0, 0.0)
IN_LINE_X = IN_CENTRE[0] + R
# The circle projected in from the other sketch, and its tangent line.
EXT_CENTRE = (-20.0, 0.0)
EXT_LINE_X = EXT_CENTRE[0] - R
LINE_Y0, LINE_Y1 = -15.0, 15.0

state = {"doc": None, "client": None, "done": False, "t0": clock(),
         "phase": "start", "internal": None, "external": None,
         "ext_geoid": None}
internal_sampled = threading.Event()
external_sampled = threading.Event()


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
    depth = EYE[2] - z
    half = depth * math.tan(HEIGHT_ANGLE / 2.0)
    scale = (VH / 2.0) / half
    # VH - 1 - py is the flip a canvas pixel goes through to reach
    # Coin's bottom-left y -- desktop (Quarter/Mouse.cpp) and mirror
    # alike -- so the inverse carries that -1. x is not flipped.
    return (int(round(VW / 2.0 + (x - EYE[0]) * scale)),
            int(round(VH / 2.0 - 1.0 - (y - EYE[1]) * scale)))


def click_at(ws, px, py, t):
    ws.send(2, wsclient.input_frame(wsclient.MOVE, px, py, time_ms=t))
    ws.drain(0.3)
    ws.send(2, wsclient.input_frame(wsclient.PRESS, px, py, code=0, time_ms=t + 100))
    ws.drain(0.1)
    ws.send(2, wsclient.input_frame(wsclient.RELEASE, px, py, code=0, time_ms=t + 200))
    ws.drain(0.3)


def draw_vertical_line(ws, x, t):
    """One line drawn bottom-to-top with Sketcher_CreateLine. Both clicks
    land on the same pixel column, so the line is exactly vertical and
    exactly tangent to a circle whose centre is R away from it."""
    ws.op('{"id":%d,"op":"command","name":"Sketcher_CreateLine"}' % (t // 1000 + 10))
    ws.drain(0.5)
    px, py = pixel_of(x, LINE_Y0)
    click_at(ws, px, py, t)
    px2, py2 = pixel_of(x, LINE_Y1)
    click_at(ws, px2, py2, t + 400)
    # 0xff1b is the X11 keysym for Escape, which is what the wire carries
    # (docs/ThinClient.md sec 8.5) -- not a Qt key code.
    ws.send(2, wsclient.input_frame(wsclient.KEY_DOWN, px2, py2,
                                    code=0xff1b, time_ms=t + 800))
    ws.send(2, wsclient.input_frame(wsclient.KEY_UP, px2, py2,
                                    code=0xff1b, time_ms=t + 820))
    ws.drain(0.3)


class Client(threading.Thread):
    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.ready = threading.Event()
        self.drew_internal = threading.Event()
        self.drew_external = threading.Event()
        self.edit_reply = None
        self.reset = None

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()
            self.drew_internal.set()
            self.drew_external.set()

    def talk(self):
        ws = WS(self.port)
        ws.hello("sketch-tangent-autoconstraint")
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

        draw_vertical_line(ws, IN_LINE_X, 1000)
        self.drew_internal.set()
        internal_sampled.wait(30.0)

        draw_vertical_line(ws, EXT_LINE_X, 3000)
        self.drew_external.set()
        external_sampled.wait(30.0)

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
    return [(c.Type, c.First, c.FirstPos, c.Second, c.SecondPos)
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
        # Driven by the pointer alone.
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").SetInt(
                "OnViewParameterVisibility", 0)

        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc

        src = doc.addObject("Sketcher::SketchObject", SRC)
        src.addGeometry(Part.Circle(
            FreeCAD.Vector(EXT_CENTRE[0], EXT_CENTRE[1], 0),
            FreeCAD.Vector(0, 0, 1), R), False)

        sk = doc.addObject("Sketcher::SketchObject", OBJ)
        sk.addGeometry(Part.Circle(
            FreeCAD.Vector(IN_CENTRE[0], IN_CENTRE[1], 0),
            FreeCAD.Vector(0, 0, 1), R), False)
        doc.recompute()
        sk.addExternal(SRC, "Edge1")
        doc.recompute()

        check("the sketch has its own circle", len(sk.Geometry) == 1, len(sk.Geometry))
        ext = sk.ExternalGeometry
        check("and one projected circle", len(ext) == 1 and ext[0][0].Name == SRC, ext)
        # ExternalGeo carries the two axes before any projection, so the
        # first real external is GeoId -3. That is what the tangency
        # should name; the fork's own index arithmetic said -1.
        state["ext_geoid"] = -3

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
            state["phase"] = "drawing-internal"
        elif phase == "drawing-internal" and client.drew_internal.is_set():
            state["phase"] = "internal-sampled"

            def sample_internal():
                state["internal"] = constraints()
                internal_sampled.set()

            QtCore.QTimer.singleShot(400, sample_internal)
        elif phase == "internal-sampled" and client.drew_external.is_set():
            state["phase"] = "external-sampled"

            def sample_external():
                state["external"] = constraints()
                external_sampled.set()

            QtCore.QTimer.singleShot(400, sample_external)
        elif phase == "external-sampled" and not client.is_alive():
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

        inner = state["internal"] or []
        tangents = [c for c in inner if c[0] == "Tangent"]
        check("a line drawn tangent to the sketch's own circle is constrained Tangent",
              len(tangents) == 1, inner)
        if tangents:
            check("against the circle, which is GeoId 0",
                  0 in (tangents[0][1], tangents[0][3]), tangents[0])
        # The line is exactly vertical too, and that is the competition:
        # the tangency pass runs first and the alignment pass is skipped.
        check("and NOT also Vertical, because the tangency won",
              not any(c[0] == "Vertical" for c in inner), inner)

        outer = state["external"] or []
        added = outer[len(inner):]
        ext_tangents = [c for c in added if c[0] == "Tangent"]
        check("a line drawn tangent to the PROJECTED circle is constrained Tangent",
              len(ext_tangents) == 1, added)
        if ext_tangents:
            want = state["ext_geoid"]
            check("against the external geometry's real GeoId, not a loop index",
                  want in (ext_tangents[0][1], ext_tangents[0][3]),
                  (want, ext_tangents[0]))
            check("and in particular not against the X axis (GeoId -1)",
                  -1 not in (ext_tangents[0][1], ext_tangents[0][3]),
                  ext_tangents[0])
        check("that one is not also Vertical either",
              not any(c[0] == "Vertical" for c in added), added)

        reset = reply_of(client.reset)
        check("the client's resetEdit is accepted", reset.get("ok") is True, reset)
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    internal_sampled.set()
    external_sampled.set()
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
