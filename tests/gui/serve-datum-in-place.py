"""A served client makes a dimension and types its value at the label.

The dimensional constraint commands were off the browser's command list
for one reason: their value was asked for in a modal dialog on the serving
machine, which stops every client with nobody there to close it. The value
is typed in the view now (SketcherGui::editDatums), in the entry box a
drawing tool's on-view parameter uses, so a client is shown it by the
"onview" push it already draws and types into it with the key frames it
already sends (docs/ThinClient.md 8.7).

Sketch: two lines, 10 long. A client over a real socket:

  - starts Sketcher_ConstrainDistance: admitted, not refused;
  - clicks the first line: one entry box is stated, with the line's length
    in it and the keys;
  - types 2, 5 and Enter: the box is gone, the constraint is there with
    25, and the two are ONE undo step;
  - clicks the second line, and Escape in its box: no constraint is left;
  - all through, the host shows no modal dialog -- watched from the GUI
    thread while the client talks;
  - Sketcher_ConstrainSnellsLaw, which has a dialog of its own, is still
    refused.

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
from PySide import QtCore, QtWidgets

import wsclient
from wsclient import WS, free_port

clock = time.perf_counter

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ServeDatumInPlace"
OBJ = "Sketch"
CLIENT_WAIT_S = 120

EYE = (0.0, 0.0, 60.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 200.0
VW, VH = 800, 600
RETURN, ESCAPE = 0xff0d, 0xff1b

state = {"doc": None, "client": None, "done": False, "t0": clock(), "dialogs": 0}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


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


def pixel_of(x, y, z=0.0):
    depth = EYE[2] - z
    half = depth * math.tan(HEIGHT_ANGLE / 2.0)
    scale = (VH / 2.0) / half
    return (int(round(VW / 2.0 + (x - EYE[0]) * scale)),
            int(round(VH / 2.0 - 1.0 - (y - EYE[1]) * scale)))


def click_at(ws, px, py, t):
    ws.send(2, wsclient.input_frame(wsclient.MOVE, px, py, time_ms=t))
    ws.drain(0.2)
    ws.send(2, wsclient.input_frame(wsclient.MOVE, px, py, time_ms=t + 20))
    ws.drain(0.2)
    ws.send(2, wsclient.input_frame(wsclient.PRESS, px, py, code=0, time_ms=t + 100))
    ws.drain(0.1)
    ws.send(2, wsclient.input_frame(wsclient.RELEASE, px, py, code=0, time_ms=t + 200))


def key(ws, code, t, char=0):
    px, py = VW // 2, 40
    ws.send(2, wsclient.input_frame(wsclient.KEY_DOWN, px, py, code=code, delta=char, time_ms=t))
    ws.send(2, wsclient.input_frame(wsclient.KEY_UP, px, py, code=code, delta=char,
                                    time_ms=t + 20))
    ws.drain(0.3)


class Client(threading.Thread):
    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.command = None
        self.opened = None
        self.typed = None
        self.after_enter = None
        self.sampled = threading.Event()
        self.opened_again = None
        self.after_escape = None
        self.refused = None
        self.all_pushes = []
        self.phase = ""

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()

    def talk(self):
        ws = WS(self.port)
        ws.hello("serve-datum-in-place")
        if ws.next_binary(20.0) is None:
            self.error = "no snapshot"
            return
        ws.next_binary(0.5)
        ws.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH))
        ws.drain(0.3)
        reply = parsed(ws.op('{"id":1,"op":"edit","obj":"%s","mode":0}' % OBJ))
        if not (reply or {}).get("ok"):
            self.error = "edit refused: %s" % reply
            ws.close()
            return
        ws.drain(0.5)

        self.command = parsed(ws.op(
            '{"id":2,"op":"command","name":"Sketcher_ConstrainDistance"}'))
        ws.drain(0.3)
        # the undo count to measure from: entering the edit is behind us
        self.phase = "started"
        self.sampled.wait(30.0)
        self.sampled.clear()

        # the first line: its dimension, and the box for its value
        mark = len(ws.pushes)
        click_at(ws, *pixel_of(2, 0), t=1000)
        self.opened = params_of(ws.next_push("onview", 8.0, since=mark))
        ws.drain(0.5)

        # the number is selected, as in the dialog: typing replaces it
        mark = len(ws.pushes)
        key(ws, ord("2"), 2000, ord("2"))
        key(ws, ord("5"), 2100, ord("5"))
        self.typed = params_of(ws.pushes[-1]) if len(ws.pushes) > mark else None
        for raw in ws.pushes[mark:]:
            got = parsed(raw)
            if got and got.get("cmd") == "onview":
                self.typed = got.get("params", [])
        mark = len(ws.pushes)
        key(ws, RETURN, 2300)
        ws.drain(0.8)
        for raw in ws.pushes[mark:]:
            got = parsed(raw)
            if got and got.get("cmd") == "onview":
                self.after_enter = got.get("params", [])

        # let the GUI thread look at the document before the second run
        self.phase = "entered"
        self.sampled.wait(30.0)
        self.sampled.clear()

        # the second line, and Escape in its box
        mark = len(ws.pushes)
        click_at(ws, *pixel_of(2, 5), t=4000)
        self.opened_again = params_of(ws.next_push("onview", 8.0, since=mark))
        ws.drain(0.5)
        mark = len(ws.pushes)
        key(ws, ESCAPE, 5000)
        ws.drain(0.8)
        for raw in ws.pushes[mark:]:
            got = parsed(raw)
            if got and got.get("cmd") == "onview":
                self.after_escape = got.get("params", [])
        self.phase = "escaped"
        self.sampled.wait(30.0)

        self.refused = parsed(ws.op(
            '{"id":3,"op":"command","name":"Sketcher_ConstrainSnellsLaw"}'))
        ws.op('{"id":4,"op":"resetEdit"}')
        ws.drain(0.5)
        self.all_pushes = [p[:160] for p in ws.pushes]
        ws.close()


def build():
    try:
        import Part

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        # the host's own preference is the dialog: a served view has no
        # dialog whatever it says
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/General").SetBool(
            "EditDatumInPlace", False)
        # The first dimension of a sketch drawn freehand scales the whole
        # sketch to it, and the second line would not be where the client
        # clicks next. Off (1 is Never): this is about the entry box.
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/dimensioning").SetInt(
            "AutoScaleMode", 1)
        doc = FreeCAD.newDocument(DOC, hidden=True)
        state["doc"] = doc
        sketch = doc.addObject("Sketcher::SketchObject", OBJ)
        V = FreeCAD.Vector
        sketch.addGeometry(Part.LineSegment(V(-5, 0, 0), V(5, 0, 0)), False)
        sketch.addGeometry(Part.LineSegment(V(-5, 5, 0), V(5, 5, 0)), False)
        doc.recompute()
        state["undo0"] = doc.UndoCount

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


def poll():
    client = state["client"]
    dialog = QtWidgets.QApplication.activeModalWidget()
    if dialog is not None:
        state["dialogs"] += 1
        dialog.reject()
    sketch = state["doc"].getObject(OBJ)
    if client.phase == "started" and "started" not in state:
        state["started"] = True
        state["undo0"] = state["doc"].UndoCount
        client.sampled.set()
    if client.phase == "entered" and "entered" not in state:
        state["entered"] = (sketch.ConstraintCount,
                            [round(c.Value, 6) for c in sketch.Constraints],
                            state["doc"].UndoCount - state["undo0"])
        client.sampled.set()
    if client.phase == "escaped" and "escaped" not in state:
        state["escaped"] = (sketch.ConstraintCount,
                            state["doc"].UndoCount - state["undo0"])
        client.sampled.set()
    if client.is_alive():
        if clock() - state["t0"] > CLIENT_WAIT_S:
            check("the client finished", False, "still talking after %ds" % CLIENT_WAIT_S)
            finish()
            return
        QtCore.QTimer.singleShot(20, poll)
        return
    QtCore.QTimer.singleShot(500, verify)


def verify():
    client = state["client"]
    try:
        check("the client ran without error", client.error is None, client.error or "")
        check("a dimensional command is admitted",
              (client.command or {}).get("ok") is True, client.command)
        opened = client.opened
        check("the pick states one entry box, at the new dimension",
              bool(opened) and len(opened) == 1, opened if opened else client.all_pushes)
        if opened:
            check("with the line's length in it, and the keys",
                  str(opened[0].get("text", "")).startswith("10") and opened[0].get("focus"),
                  opened[0])
        check("the typed digits replace the number in the box",
              bool(client.typed) and str(client.typed[0].get("text", "")).startswith("25"),
              client.typed)
        check("Enter takes the box away", client.after_enter == [], client.after_enter)
        entered = state.get("entered")
        check("and the constraint is there with the typed value",
              entered is not None and entered[0] == 1 and entered[1] == [25.0], entered)
        check("the constraint and its value are one undo step",
              entered is not None and entered[2] == 1, entered)
        check("the second pick states a box again",
              bool(client.opened_again) and len(client.opened_again) == 1,
              client.opened_again)
        escaped = state.get("escaped")
        check("Escape takes the box away and leaves no constraint behind",
              client.after_escape == [] and escaped is not None and escaped == (1, 1),
              (client.after_escape, escaped))
        check("the host showed no modal dialog", state["dialogs"] == 0, state["dialogs"])
        refused = client.refused or {}
        check("a command with a dialog of its own is still refused",
              refused.get("ok") is False and refused.get("code") == "CommandRefused", refused)
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
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/General").RemBool(
        "EditDatumInPlace")
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/dimensioning").RemInt(
        "AutoScaleMode")
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, build)
