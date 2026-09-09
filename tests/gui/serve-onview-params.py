"""A sketch tool's on-view parameters reach a client and are typed into.

Stage 5 of docs/ThinClient.md sec 8.9, over a real socket. The unit set
next door (tests/src/Gui/OnViewParameter.cpp) proves the mechanism on a
mirror built by hand; this proves the whole path with a real sketch, a
real tool and a real server: the tool opens its entry boxes, the server
states them to the client that owns the view, and a key frame off the
wire is typed into the one that has the keys.

What is asserted:

  - a client that is not editing is told about no entry boxes. There are
    none, and a client showing one would be showing a tool nobody is
    running;
  - the `command` op is allowlisted, and narrowly. Sketcher_CreateLine
    runs; anything outside Sketcher_Create* is refused by name, without
    being run -- because many commands open a modal dialog, and a modal
    dialog on the GUI thread of a serving process stops serving every
    client with nobody at the machine to dismiss it;
  - the tool, once it has a pointer position, pushes a non-empty on-view
    set with a world anchor rather than a pixel position (sec 8.7: the
    client projects it with the camera of the frame it is drawing, which
    is the only camera that cannot be behind the picture). Not before the
    pointer: the set is opened by the first mode change, and the first
    mode change is the first move;
  - one of the boxes has the keys, and a key frame carrying a digit
    changes THAT box's text and no other. This is the claim the whole
    design rests on -- the entry box is the same QuantitySpinBox the
    desktop uses, never shown, driven by replayed events -- and it is
    what keeps the parsing, the units and the rule for which keys a box
    claims from having a second implementation in the browser;
  - `onViewFocus` moves the keys, and a stale index is refused;
  - and leaving the edit takes the boxes away, so nothing is left on a
    client's screen forwarding keystrokes to a tool that has finished.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt.
"""
import json as jsonlib
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
DOC = "ServeOnView"
OBJ = "Sketch"
CLIENT_WAIT_S = 120

EYE = (0.0, 0.0, 60.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 200.0
VW, VH = 800, 600

state = {"doc": None, "client": None, "done": False, "t0": clock()}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def camera_frame():
    return wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH)


def parsed(raw):
    if raw is None:
        return None
    try:
        return jsonlib.loads(raw.decode("utf-8"))
    except Exception:
        return None


def params_of(raw):
    message = parsed(raw)
    return (message or {}).get("params", []) if message else None


class Client(threading.Thread):
    """The whole wire conversation, off the GUI thread."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.idle_set = None
        self.refused_command = None
        self.accepted_command = None
        self.opened = None
        self.after_digit = None
        self.before_digit = None
        self.focus_moved = None
        self.stale_focus = None
        self.after_reset = None
        self.all_pushes = []

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()

    def talk(self):
        ws = WS(self.port)
        ws.hello("serve-onview-params")
        self.snapshot = ws.next_binary(20.0) is not None
        if not self.snapshot:
            return
        ws.next_binary(0.5)

        # A camera, so this connection has a view to bind an edit to.
        ws.send(2, camera_frame())
        ws.drain(0.3)

        # Nothing is running, so there are no boxes to hear about.
        self.idle_set = ws.next_push("onview", 0.5)

        reply = parsed(ws.op('{"id":1,"op":"edit","obj":"%s","mode":0}' % OBJ))
        if not (reply or {}).get("ok"):
            self.error = "edit refused: %s" % reply
            ws.close()
            return
        ws.drain(0.5)

        # Outside the allowlist, and refused by name rather than run.
        self.refused_command = parsed(ws.op(
            '{"id":2,"op":"command","name":"Std_ViewFitAll"}'))

        mark = len(ws.pushes)
        self.accepted_command = parsed(ws.op(
            '{"id":3,"op":"command","name":"Sketcher_CreateLine"}'))

        # And then a pointer position, because a tool that has never seen
        # one has nothing to place its boxes against: the set is opened by
        # the first mode change, and the first mode change is the first
        # move. So there is deliberately no push between the command and
        # this, and waiting for one here would be waiting for something
        # that should not exist.
        ws.send(2, wsclient.input_frame(wsclient.MOVE, VW // 2, VH // 2,
                                        time_ms=1000))
        self.opened = params_of(ws.next_push("onview", 8.0, since=mark))
        self.before_digit = self.opened

        # The digit. The keysym travels with the character it produced,
        # because the keysym has the case folded out of it and which
        # character a key makes is a keyboard-layout question only the
        # client can answer (sec 8.5).
        mark = len(ws.pushes)
        ws.send(2, wsclient.input_frame(wsclient.KEY_DOWN, VW // 2, VH // 2,
                                        code=ord("7"), delta=ord("7"),
                                        time_ms=1100))
        ws.send(2, wsclient.input_frame(wsclient.KEY_UP, VW // 2, VH // 2,
                                        code=ord("7"), delta=ord("7"),
                                        time_ms=1120))
        ws.drain(0.5)
        self.after_digit = params_of(ws.next_push("onview", 3.0, since=mark))

        # The one thing about the boxes a client decides, and it still
        # goes through the server so the tool's own focus tracking stays
        # the authority.
        mark = len(ws.pushes)
        self.focus_moved = parsed(ws.op('{"id":4,"op":"onViewFocus","index":1}'))
        self.stale_focus = parsed(ws.op('{"id":5,"op":"onViewFocus","index":99}'))

        # Escape ends the tool, and leaving the edit ends the session.
        mark = len(ws.pushes)
        ws.op('{"id":6,"op":"resetEdit"}')
        ws.drain(0.7)
        self.after_reset = params_of(ws.next_push("onview", 3.0, since=mark))
        # Every unsolicited frame, for the failure message: "no boxes"
        # and "no push at all" look identical from the assertions alone.
        self.all_pushes = [p[:200] for p in ws.pushes]
        ws.close()


def build():
    try:
        import Part

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        # The default setting shows only the DIMENSIONAL boxes, and a line
        # tool's first state has only positioning ones -- so with the
        # default there is nothing on screen until the second click. This
        # probe is about the mechanism, so ask for all of them.
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").SetInt(
                "OnViewParameterVisibility", 2)
        doc = FreeCAD.newDocument(DOC, hidden=True)
        state["doc"] = doc
        sketch = doc.addObject("Sketcher::SketchObject", OBJ)
        sketch.addGeometry(Part.LineSegment(FreeCAD.Vector(-5, 0, 0),
                                            FreeCAD.Vector(5, 0, 0)), False)
        doc.recompute()

        port = free_port()
        ok = FreeCADGui.serveDocument(doc, port)
        if not check("the document is served headless", ok, "port %d" % port):
            finish()
            return
        state["client"] = Client(port)
        state["client"].start()
        QtCore.QTimer.singleShot(50, poll)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def views_3d():
    return len(FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor"))


def poll():
    client = state["client"]
    state.setdefault("views", []).append(views_3d())
    if client.is_alive():
        if clock() - state["t0"] > CLIENT_WAIT_S:
            check("the client finished", False,
                  "still talking after %ds" % CLIENT_WAIT_S)
            finish()
            return
        QtCore.QTimer.singleShot(20, poll)
        return
    QtCore.QTimer.singleShot(500, verify)


def text_of(params, index):
    for param in params or []:
        if param.get("i") == index:
            return param.get("text")
    return None


def focused(params):
    return [p.get("i") for p in (params or []) if p.get("focus")]


def verify():
    client = state["client"]
    try:
        check("the client ran without error", client.error is None,
              client.error or "")
        check("the hello was answered with a snapshot", client.snapshot)
        check("a client that is not editing hears of no entry boxes",
              client.idle_set is None, client.idle_set)

        refused = client.refused_command or {}
        check("a command outside the allowlist is refused",
              refused.get("ok") is False
              and refused.get("code") == "CommandRefused", refused)
        check("a sketch tool is accepted",
              (client.accepted_command or {}).get("ok") is True,
              client.accepted_command)

        opened = client.opened
        check("starting the tool states its entry boxes",
              bool(opened), opened if opened else client.all_pushes)
        if opened:
            first = opened[0]
            check("a box is anchored in the world, not in pixels",
                  all(k in first for k in ("x", "y", "z")), first)
            check("a box carries text to display", "text" in first, first)
        check("the session never made a 3D view",
              all(v == 0 for v in state.get("views", [])),
              sorted(set(state.get("views", []))))

        before, after = client.before_digit, client.after_digit
        held = focused(before)
        check("one box has the keys", len(held) == 1, focused(before))
        check("a replayed digit is typed into it",
              bool(held) and bool(after)
              and text_of(after, held[0]) != text_of(before, held[0]),
              (before, after))
        if held and after:
            others = [p.get("i") for p in after
                      if p.get("i") != held[0]
                      and text_of(after, p.get("i")) != text_of(before, p.get("i"))]
            check("and into no other", not others, others)

        moved = client.focus_moved or {}
        check("a client may move the keys between boxes",
              moved.get("ok") is True, moved)
        stale = client.stale_focus or {}
        check("a stale index is refused rather than applied",
              stale.get("ok") is False, stale)

        check("leaving the edit takes the boxes away",
              client.after_reset == [] or client.after_reset is None,
              client.after_reset)
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
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
