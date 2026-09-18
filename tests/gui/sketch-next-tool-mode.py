"""The sketch tool's mode is a command, not a hardcoded key.

`M` used to be the raw Coin constant `SoKeyboardEvent::M`, tested inside
each handler's `registerPressedKey`, which `ViewProviderSketch` feeds
straight from the viewport. It was not a `Gui::Command`, so it had no
entry in Tools > Customize > Keyboard, could not be rebound, could not be
put on a tool bar, and could not be sent by a client with no key
bindings. Four handlers hardcoded it.

Now each handler answers `canIterateToolMode()` / `iterateToolMode()` and
`Sketcher_NextToolMode` asks, with `M` as an ordinary accelerator that
`Gui::ShortcutManager` arbitrates like any other.

What this reads is the polyline, whose modes are visible in the geometry
it produces. With a line already drawn, the cycle runs

    Line/Free -> Line/Perpendicular_L -> Line/Tangent -> Arc/Tangent

so three invocations of the command turn the *next* segment from a line
into an arc. That is the claim, end to end: the command reached the
running handler and moved it.

  - two clicks draw the first segment, a line,
  - three runs of Sketcher_NextToolMode,
  - a third click ends a segment that is an ArcOfCircle.

The control is the same run's first segment, which is a LineSegment: if
the command did nothing, the second would be one too.

Also checked: the command is inactive when no tool is running, which is
what keeps the accelerator out of the way the rest of the time.

Clicks go over the wire because a sketch tool is driven by the viewport,
not by Python; the command is run on this side because
`Sketcher_NextToolMode` is deliberately not on the browser-safe command
allowlist (one of its handlers opens a modal dialog).

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
DOC = "SketchNextToolMode"
OBJ = "Sketch"
CLIENT_WAIT_S = 180

EYE = (0.0, 0.0, 60.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 200.0
VW, VH = 800, 600

# A first segment along the bottom, then a third point well above it, so
# the arc that should appear has room and is unambiguous.
P1 = (-20.0, -10.0)
P2 = (0.0, -10.0)
P3 = (15.0, 8.0)

state = {"doc": None, "client": None, "done": False, "t0": clock(),
         "phase": "start", "cycles": 0, "inactive_before": None}
first_drawn = threading.Event()
cycled = threading.Event()
finished = threading.Event()


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

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()
            for ev in (self.ready, first_drawn, finished):
                ev.set()

    def talk(self):
        ws = WS(self.port)
        ws.hello("sketch-next-tool-mode")
        self.snapshot = ws.next_binary(20.0) is not None
        if not self.snapshot:
            for ev in (self.ready, first_drawn, finished):
                ev.set()
            return
        ws.next_binary(0.5)
        ws.send(2, camera_frame())
        ws.drain(0.5)
        self.ready.set()

        self.edit_reply = ws.op('{"id":2,"op":"edit","obj":"%s","mode":0}' % OBJ)
        ws.drain(0.5)

        ws.op('{"id":3,"op":"command","name":"Sketcher_CreatePolyline"}')
        ws.drain(0.5)

        # The first segment: a line, and the tool is now seeking the second
        # point with a previous curve behind it, which is when the mode can
        # be cycled at all.
        click_at(ws, *pixel_of(*P1), t=1000)
        click_at(ws, *pixel_of(*P2), t=1400)
        first_drawn.set()

        # The desktop runs the command three times while the tool waits.
        if not cycled.wait(60.0):
            self.error = "the desktop never ran Sketcher_NextToolMode"
            finished.set()
            ws.close()
            return

        click_at(ws, *pixel_of(*P3), t=2200)
        # 0xff1b is the X11 keysym for Escape, which is what the wire
        # carries (docs/ThinClient.md sec 8.5) -- not a Qt key code.
        px, py = pixel_of(*P3)
        ws.send(2, wsclient.input_frame(wsclient.KEY_DOWN, px, py,
                                        code=0xff1b, time_ms=2600))
        ws.send(2, wsclient.input_frame(wsclient.KEY_UP, px, py,
                                        code=0xff1b, time_ms=2620))
        ws.drain(0.3)
        ws.op('{"id":9,"op":"resetEdit"}')
        ws.drain(0.5)
        finished.set()
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
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").SetInt(
                "OnViewParameterVisibility", 0)

        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        sk = doc.addObject("Sketcher::SketchObject", OBJ)
        sk.ViewObject.Autoconstraints = False
        sk.ViewObject.GridAuto = False
        doc.recompute()

        # With no tool running the command has nothing to move, which is
        # what keeps M out of the way when it is not wanted.
        state["inactive_before"] = FreeCADGui.Command.get(
            "Sketcher_NextToolMode").isActive()

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


def run_cycles():
    """Line/Free -> Perpendicular_L -> Tangent -> Arc/Tangent."""
    try:
        for _ in range(3):
            FreeCADGui.runCommand("Sketcher_NextToolMode")
            state["cycles"] += 1
    except Exception:
        note("ABORT cycles:\n" + traceback.format_exc())
    cycled.set()


def poll():
    client = state["client"]
    if client.error:
        QtCore.QTimer.singleShot(200, verify)
        return
    try:
        if state["phase"] == "start" and first_drawn.is_set():
            state["phase"] = "cycling"
            QtCore.QTimer.singleShot(300, run_cycles)
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
    QtCore.QTimer.singleShot(600, verify)


def verify():
    client = state["client"]
    try:
        check("the client ran without error", client.error is None, client.error or "")
        check("the hello was answered with a snapshot", client.snapshot)
        check("the client's edit is accepted",
              reply_of(client.edit_reply).get("ok") is True, reply_of(client.edit_reply))
        check("the command is inactive with no tool running",
              state["inactive_before"] is False, state["inactive_before"])
        check("the command ran three times", state["cycles"] == 3, state["cycles"])

        geos = sketch().Geometry
        if not check("two segments were drawn", len(geos) >= 2,
                     [g.TypeId for g in geos]):
            finish()
            return

        kinds = [g.TypeId for g in geos]
        # The control: the first segment was drawn before any cycling.
        check("the first segment is a line",
              kinds[0] == "Part::GeomLineSegment", kinds)
        # The claim: three runs of the command moved the tool into arc mode,
        # so the segment drawn after them is an arc.
        check("the segment drawn after cycling is an arc",
              kinds[1] == "Part::GeomArcOfCircle", kinds)
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    for ev in (first_drawn, cycled, finished):
        ev.set()
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
