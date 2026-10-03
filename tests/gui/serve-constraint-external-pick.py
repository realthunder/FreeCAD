"""Outside picking stacked on a constraint tool, from a browser (the
served run of tests/gui/sketch-constraint-external-pick.py; docs/ThinClient.md
8.11 item 3).

A browser can join a constraint tool the desktop started, and start one of
its own: the constraint commands that open no dialog are on the browser's
command list (the dimensional ones too, since their value is typed in
the view: serve-datum-in-place.py). The desktop enters the sketch
and starts Sketcher_ConstrainParallel, and the client, in that same
session, presses Sketcher_External -- which a running constraint tool
takes as "switch outside picking" instead of being replaced by it.

A document with a real 3D window under xvfb AND a served connection with
a mirror, a box beside the sketch.

What is asserted:

  - Sketcher_External from the client is admitted, and sets the setting
    (Mod/Sketcher/General/ConstraintExternalPick) instead of replacing the
    desktop's tool;
  - a click on the sketch's line and a click on the box's top edge, both
    from the client and picked in its own mirror, add ONE external
    geometry referring to the box and ONE Parallel between the line and
    it, in one undo step;
  - Sketcher_External from the client once more switches it off.

Then the same with the session the other way round -- the CLIENT enters
the sketch, the desktop window joins it and starts the Parallel tool from
there, as a press on its tool bar does, and the client toggles and clicks:
the session's selection is the client's own then, and the tool has to
listen where the clicks land.

And once more in the client's session with everything from the client: it
undoes the second run, starts Sketcher_ConstrainPerpendicular itself in
place of the desktop's Parallel tool, switches outside picking on and
clicks the line and the box's other top edge. Sketcher_MapSketch is
refused -- it has a modal dialog of its own.

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
DOC = "ServeConstraintExternalPick"
OBJ = "Sketch"
GENERAL = "User parameter:BaseApp/Preferences/Mod/Sketcher/General"
BOX = "Box"
CLIENT_WAIT_S = 150

EYE = (0.0, 0.0, 60.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 200.0
VW, VH = 800, 600

state = {"doc": None, "client": None, "done": False, "t0": clock(),
         "phase": "start", "before": None, "toggled": None, "after": None,
         "off": None, "before2": None, "toggled2": None, "after2": None,
         "off2": None, "toggled3": None, "after3": None, "in_edit": []}
# GUI thread -> client thread: the desktop did its part.
desktop_entered = threading.Event()
toggle_sampled = threading.Event()
picks_sampled = threading.Event()
desktop_left = threading.Event()
tool_started = threading.Event()
toggle2_sampled = threading.Event()
picks2_sampled = threading.Event()
toggle3_sampled = threading.Event()
picks3_sampled = threading.Event()


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def camera_frame():
    return wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH)


def pixel_of(x, y, z):
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
    arrives: the root preselects on the move and selects on the release."""
    ws.send(2, wsclient.input_frame(wsclient.MOVE, px, py, time_ms=t))
    ws.drain(0.2)
    ws.send(2, wsclient.input_frame(wsclient.PRESS, px, py, code=0, time_ms=t + 100))
    ws.drain(0.1)
    ws.send(2, wsclient.input_frame(wsclient.RELEASE, px, py, code=0, time_ms=t + 200))


class Client(threading.Thread):
    """The wire conversation, off the GUI thread."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.ready = threading.Event()
        self.toggled = threading.Event()
        self.clicked = threading.Event()
        self.switched_off = threading.Event()
        self.entered = threading.Event()
        self.toggled2 = threading.Event()
        self.clicked2 = threading.Event()
        self.toggled3 = threading.Event()
        self.clicked3 = threading.Event()
        self.undo_reply = None
        self.tool_reply = None
        self.on3_reply = None
        self.refused = None
        self.flavour_reply = None
        self.flavour_mode = None
        self.told_entered = None
        self.on_reply = None
        self.off_reply = None
        self.edit_reply = None
        self.on2_reply = None
        self.off2_reply = None
        self.reset = None

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()
            for event in (self.toggled, self.clicked, self.switched_off, self.entered,
                          self.toggled2, self.clicked2, self.toggled3, self.clicked3):
                event.set()

    def talk(self):
        ws = WS(self.port)
        ws.hello("serve-constraint-external-pick")
        self.snapshot = ws.next_binary(20.0) is not None
        if not self.snapshot:
            self.ready.set()
            return
        ws.next_binary(0.5)
        ws.send(2, camera_frame())
        ws.drain(0.5)
        self.ready.set()

        # The desktop is in the sketch with the Parallel tool running.
        desktop_entered.wait(30.0)
        self.told_entered = ws.next_push("edit", 10.0)
        ws.drain(0.3)
        self.on_reply = ws.op('{"id":2,"op":"command","name":"Sketcher_External"}')
        ws.drain(0.5)
        self.toggled.set()
        toggle_sampled.wait(30.0)

        # The sketch's line, then the box's top edge along x: the pixel is
        # a hair OUTSIDE the top face, so the only thing within the pick
        # radius is the edge at y = 10, z = 10.
        px, py = pixel_of(-5.0, -8.0, 0.0)
        click_at(ws, px, py, 1000)
        ws.drain(0.8)
        px, py = pixel_of(5.0, 10.2, 10.0)
        click_at(ws, px, py, 2000)
        ws.drain(1.0)
        self.clicked.set()
        picks_sampled.wait(30.0)

        self.off_reply = ws.op('{"id":3,"op":"command","name":"Sketcher_External"}')
        ws.drain(0.5)
        self.switched_off.set()

        # The other way round: this client enters, the desktop joins and
        # starts the tool.
        desktop_left.wait(30.0)
        ws.drain(0.5)
        self.edit_reply = ws.op('{"id":4,"op":"edit","obj":"%s","mode":0}' % OBJ)
        ws.drain(0.5)
        self.entered.set()
        tool_started.wait(30.0)
        self.on2_reply = ws.op('{"id":5,"op":"command","name":"Sketcher_External"}')
        ws.drain(0.5)
        self.toggled2.set()
        toggle2_sampled.wait(30.0)
        px, py = pixel_of(-5.0, -8.0, 0.0)
        click_at(ws, px, py, 3000)
        ws.drain(0.8)
        px, py = pixel_of(5.0, 10.2, 10.0)
        click_at(ws, px, py, 4000)
        ws.drain(1.0)
        self.clicked2.set()
        picks2_sampled.wait(30.0)
        self.off2_reply = ws.op('{"id":6,"op":"command","name":"Sketcher_External"}')
        ws.drain(0.5)

        # Once more, all of it from here: the second run undone, the tool
        # started by this client.
        self.undo_reply = ws.op('{"id":8,"op":"undo"}')
        ws.drain(0.5)
        self.tool_reply = ws.op(
            '{"id":9,"op":"command","name":"Sketcher_ConstrainPerpendicular"}')
        ws.drain(0.5)
        self.on3_reply = ws.op('{"id":10,"op":"command","name":"Sketcher_External"}')
        ws.drain(0.5)
        self.toggled3.set()
        toggle3_sampled.wait(30.0)
        px, py = pixel_of(-5.0, -8.0, 0.0)
        click_at(ws, px, py, 5000)
        ws.drain(0.8)
        # the box's top edge along y, a hair outside the top face again
        px, py = pixel_of(10.2, 5.0, 10.0)
        click_at(ws, px, py, 6000)
        ws.drain(1.0)
        self.clicked3.set()
        picks3_sampled.wait(30.0)
        ws.op('{"id":11,"op":"command","name":"Sketcher_External"}')
        ws.drain(0.3)
        # the other flavours are on the list too: on, read, off again
        self.flavour_reply = ws.op('{"id":13,"op":"command","name":"Sketcher_Intersection"}')
        ws.drain(0.3)
        self.flavour_mode = FreeCAD.ParamGet(GENERAL).GetInt("ConstraintExternalPick", 0)
        ws.op('{"id":14,"op":"command","name":"Sketcher_Intersection"}')
        ws.drain(0.3)
        self.refused = ws.op('{"id":12,"op":"command","name":"Sketcher_MapSketch"}')
        ws.drain(0.3)
        self.reset = ws.op('{"id":7,"op":"resetEdit"}')
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


def gdoc():
    return FreeCADGui.getDocument(DOC)


def in_edit():
    return gdoc().getInEdit() is not None


def sketch():
    return state["doc"].getObject(OBJ)


def external_geometry():
    return [(obj.Name, list(subs)) for obj, subs in sketch().ExternalGeometry]


def mode():
    return FreeCAD.ParamGet(GENERAL).GetInt("ConstraintExternalPick", 0)


def sample():
    sk = sketch()
    return {"ext": external_geometry(),
            "cons": [(c.Type, c.First, c.Second) for c in sk.Constraints],
            "undo": state["doc"].UndoCount, "mode": mode()}


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
        FreeCAD.ParamGet(GENERAL).SetInt("ConstraintExternalPick", 0)
        doc = FreeCAD.newDocument(DOC)
        doc.UndoMode = 1
        state["doc"] = doc
        box = doc.addObject("Part::Box", BOX)
        sk = doc.addObject("Sketcher::SketchObject", OBJ)
        sk.addGeometry(Part.LineSegment(FreeCAD.Vector(-8, -8, 0),
                                        FreeCAD.Vector(-2, -8, 0)), False)
        doc.recompute()
        check("the box has a shape to pick", not box.Shape.isNull())

        port = free_port()
        ok = FreeCADGui.serveDocument(doc, port)
        if not check("the document is served beside its window", ok, "port %d" % port):
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
    phase = state["phase"]
    try:
        if phase == "start" and client.ready.is_set():
            state["phase"] = "desktop-edit"
            opened = gdoc().setEdit(sketch(), 0)
            check("the desktop entered edit", opened, opened)

            def start_tool():
                FreeCADGui.runCommand("Sketcher_ConstrainParallel")
                state["before"] = sample()
                QtCore.QTimer.singleShot(300, lambda: desktop_entered.set())

            QtCore.QTimer.singleShot(400, start_tool)
        elif phase == "desktop-edit" and client.toggled.is_set():
            state["phase"] = "toggled"

            def sample_toggle():
                state["toggled"] = sample()
                toggle_sampled.set()

            QtCore.QTimer.singleShot(300, sample_toggle)
        elif phase == "toggled" and client.clicked.is_set():
            state["phase"] = "picked"

            def sample_picks():
                state["after"] = sample()
                state["in_edit"].append(in_edit())
                picks_sampled.set()

            QtCore.QTimer.singleShot(400, sample_picks)
        elif phase == "picked" and client.switched_off.is_set():
            state["phase"] = "left"

            def leave():
                state["off"] = mode()
                gdoc().resetEdit()

                def undo_first():
                    # what the first run made goes, for the second to
                    # start as the first did
                    # (leaving the edit recorded a step of its own on top)
                    while state["doc"].UndoCount:
                        state["doc"].undo()
                    state["doc"].recompute()
                    QtCore.QTimer.singleShot(400, lambda: desktop_left.set())

                QtCore.QTimer.singleShot(500, undo_first)

            QtCore.QTimer.singleShot(300, leave)
        elif phase == "left" and client.entered.is_set():
            state["phase"] = "client-edit"

            def start_tool2():
                state["in_edit"].append(in_edit())
                FreeCADGui.runCommand("Sketcher_ConstrainParallel")
                state["before2"] = sample()
                QtCore.QTimer.singleShot(300, lambda: tool_started.set())

            QtCore.QTimer.singleShot(400, start_tool2)
        elif phase == "client-edit" and client.toggled2.is_set():
            state["phase"] = "toggled2"

            def sample_toggle2():
                state["toggled2"] = sample()
                toggle2_sampled.set()

            QtCore.QTimer.singleShot(300, sample_toggle2)
        elif phase == "toggled2" and client.clicked2.is_set():
            state["phase"] = "picked2"

            def sample_picks2():
                state["after2"] = sample()
                picks2_sampled.set()

            QtCore.QTimer.singleShot(400, sample_picks2)
        elif phase == "picked2" and client.toggled3.is_set():
            state["phase"] = "toggled3"

            def sample_toggle3():
                state["toggled3"] = sample()
                toggle3_sampled.set()

            QtCore.QTimer.singleShot(300, sample_toggle3)
        elif phase == "toggled3" and client.clicked3.is_set():
            state["phase"] = "picked3"

            def sample_picks3():
                state["after3"] = sample()
                picks3_sampled.set()

            QtCore.QTimer.singleShot(400, sample_picks3)
        elif phase == "picked3" and not client.is_alive():
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
        told = client.told_entered or b""
        check("the client is told the desktop's edit began",
              b'"editing":true' in told, told[:120])

        before, toggled, after = state["before"], state["toggled"], state["after"]
        check("before: no external geometry, no constraint, outside picking off",
              before is not None and before["ext"] == [] and before["cons"] == []
              and before["mode"] == 0, before)
        reply = reply_of(client.on_reply)
        check("Sketcher_External is admitted by the command op",
              reply.get("ok") is True, reply)
        check("and switches outside picking on for the desktop's tool",
              toggled is not None and toggled["mode"] == 1, toggled)

        check("the client's two clicks added one external geometry, the box's edge",
              after is not None and len(after["ext"]) == 1 and after["ext"][0][0] == BOX
              and len(after["ext"][0][1]) == 1 and after["ext"][0][1][0].startswith("Edge"),
              after)
        check("and a Parallel between the sketch's line and it",
              after is not None
              and after["cons"] in ([("Parallel", 0, -3)], [("Parallel", -3, 0)]), after)
        check("in one undo step",
              after is not None and before is not None
              and after["undo"] == before["undo"] + 1,
              (before and before["undo"], after and after["undo"]))
        check("the desktop was still in edit when it was read",
              state["in_edit"][:1] == [True], state["in_edit"])

        reply = reply_of(client.off_reply)
        check("Sketcher_External once more is admitted", reply.get("ok") is True, reply)
        check("and switches outside picking off", state["off"] == 0, state["off"])

        # The session the client began.
        before, toggled, after = state["before2"], state["toggled2"], state["after2"]
        reply = reply_of(client.edit_reply)
        check("the client's edit is accepted", reply.get("ok") is True, reply)
        check("the desktop window joined the client's session",
              state["in_edit"][1:2] == [True], state["in_edit"])
        check("client's session, before: as the first run began",
              before is not None and before["ext"] == [] and before["cons"] == []
              and before["mode"] == 0, before)
        reply = reply_of(client.on2_reply)
        check("client's session: Sketcher_External switches outside picking on",
              reply.get("ok") is True and toggled is not None and toggled["mode"] == 1,
              (reply, toggled))
        check("client's session: the two clicks added the box's edge",
              after is not None and len(after["ext"]) == 1 and after["ext"][0][0] == BOX,
              after)
        check("client's session: and a Parallel to it",
              after is not None
              and after["cons"] in ([("Parallel", 0, -3)], [("Parallel", -3, 0)]), after)
        check("client's session: in one undo step",
              after is not None and before is not None
              and after["undo"] == before["undo"] + 1,
              (before and before["undo"], after and after["undo"]))
        reply = reply_of(client.off2_reply)
        check("client's session: switched off again",
              reply.get("ok") is True and mode() == 0, (reply, mode()))

        # The client's own tool.
        toggled, after = state["toggled3"], state["after3"]
        reply = reply_of(client.undo_reply)
        check("the client's own tool: its undo takes the second run back",
              reply.get("ok") is True and toggled is not None
              and toggled["ext"] == [] and toggled["cons"] == [], (reply, toggled))
        reply = reply_of(client.tool_reply)
        check("the client's own tool: Sketcher_ConstrainPerpendicular is admitted",
              reply.get("ok") is True, reply)
        reply = reply_of(client.on3_reply)
        check("the client's own tool: Sketcher_External switches outside picking on",
              reply.get("ok") is True and toggled is not None and toggled["mode"] == 1,
              (reply, toggled))
        check("the client's own tool: the two clicks added the box's edge",
              after is not None and len(after["ext"]) == 1 and after["ext"][0][0] == BOX,
              after)
        check("the client's own tool: and a Perpendicular to it, not the Parallel of the "
              "tool the desktop left running",
              after is not None
              and after["cons"] in ([("Perpendicular", 0, -3)], [("Perpendicular", -3, 0)]),
              after)
        check("the client's own tool: in one undo step",
              after is not None and toggled is not None
              and after["undo"] == toggled["undo"] + 1,
              (toggled and toggled["undo"], after and after["undo"]))
        reply = reply_of(client.flavour_reply)
        check("the client's own tool: Sketcher_Intersection is admitted and switches the flavour",
              reply.get("ok") is True and client.flavour_mode == 3,
              (reply, client.flavour_mode))
        reply = reply_of(client.refused)
        check("a command with a dialog of its own is still refused",
              reply.get("ok") is not True and "CommandRefused" in str(reply), reply)
        reset = reply_of(client.reset)
        check("the client's resetEdit is accepted", reset.get("ok") is True, reset)
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    desktop_entered.set()
    for event in (toggle_sampled, picks_sampled, desktop_left, tool_started,
                  toggle2_sampled, picks2_sampled):
        event.set()
    try:
        FreeCAD.ParamGet(GENERAL).SetInt("ConstraintExternalPick", 0)
    except Exception:
        pass
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
