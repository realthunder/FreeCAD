"""Construction mode belongs to the sketch, not to the application
(upstream 9a1020929e).

`geometryCreationMode` was one global in CommandCreateGeo.cpp, so the
answer to "is the next line construction geometry?" was the same for
every sketch in every document open in the process. Turn construction on
to draw a couple of guide lines in one sketch, leave it, open another,
and the next line you drew there was construction too -- with the toolbar
button showing it, which is the only reason it was ever noticed.

The state lives on ViewProviderSketch now, and the toggle command reads
and writes the mode of whichever sketch is in edit.

What this reads, in one run, through the geometry each sketch ends up
with:

  - A, with construction toggled on: construction,
  - B, never toggled: NORMAL -- the discriminating one, and the line
    that fails against the global,
  - A again, re-entered: still construction, so the mode went with the
    sketch and was not merely reset by leaving edit.

The lines are drawn by the Line tool over the wire, because the mode is
only consulted where a tool creates geometry; `addGeometry` from Python
takes its construction flag as an argument and would read nothing. The
toggle itself is run on this side: `Sketcher_ToggleConstruction` is not
on the browser-safe command allowlist (SceneControl.cpp, and widening it
is gated on the modal-dialog question), so the client waits while the
desktop toggles.

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
DOC = "SketchConstructionMode"
OBJ_A = "SketchA"
OBJ_B = "SketchB"
CLIENT_WAIT_S = 180

EYE = (0.0, 0.0, 60.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 200.0
VW, VH = 800, 600

# Kept well apart so no click lands on the other sketch's line, and away
# from the axes so nothing is snapped or auto-constrained onto them.
LINE_A1 = ((-24.0, 12.0), (-8.0, 12.0))
LINE_B = ((8.0, 12.0), (24.0, 12.0))
LINE_A2 = ((-24.0, -12.0), (-8.0, -12.0))

state = {"doc": None, "client": None, "done": False, "t0": clock(),
         "phase": "start", "toggled": False}
edited = threading.Event()
toggled = threading.Event()
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
    """The client pixel (top-left origin) showing world (x, y, z)."""
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


class Client(threading.Thread):
    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.ready = threading.Event()
        self.edits = []

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()
            edited.set()
            drawn.set()

    def draw(self, ws, ident, line, t):
        ws.op('{"id":%d,"op":"command","name":"Sketcher_CreateLine"}' % ident)
        ws.drain(0.5)
        for i, point in enumerate(line):
            px, py = pixel_of(*point)
            click_at(ws, px, py, t + i * 400)
        # 0xff1b is the X11 keysym for Escape, which is what the wire
        # carries (docs/ThinClient.md sec 8.5) -- not a Qt key code.
        px, py = pixel_of(*line[-1])
        ws.send(2, wsclient.input_frame(wsclient.KEY_DOWN, px, py,
                                        code=0xff1b, time_ms=t + 800))
        ws.send(2, wsclient.input_frame(wsclient.KEY_UP, px, py,
                                        code=0xff1b, time_ms=t + 820))
        ws.drain(0.3)

    def talk(self):
        ws = WS(self.port)
        ws.hello("sketch-construction-mode")
        self.snapshot = ws.next_binary(20.0) is not None
        if not self.snapshot:
            self.ready.set()
            edited.set()
            drawn.set()
            return
        ws.next_binary(0.5)
        ws.send(2, camera_frame())
        ws.drain(0.5)
        self.ready.set()

        # A, with construction on. The toggle is the desktop's to run, so
        # the edit is announced and this waits for it to have happened.
        self.edits.append(ws.op('{"id":2,"op":"edit","obj":"%s","mode":0}' % OBJ_A))
        ws.drain(0.5)
        edited.set()
        if not toggled.wait(60.0):
            self.error = "the desktop never toggled construction mode"
            drawn.set()
            ws.close()
            return
        self.draw(ws, 3, LINE_A1, 1000)
        ws.op('{"id":4,"op":"resetEdit"}')
        ws.drain(0.5)

        # B, never toggled.
        self.edits.append(ws.op('{"id":5,"op":"edit","obj":"%s","mode":0}' % OBJ_B))
        ws.drain(0.5)
        self.draw(ws, 6, LINE_B, 3000)
        ws.op('{"id":7,"op":"resetEdit"}')
        ws.drain(0.5)

        # A again, without touching the toggle.
        self.edits.append(ws.op('{"id":8,"op":"edit","obj":"%s","mode":0}' % OBJ_A))
        ws.drain(0.5)
        self.draw(ws, 9, LINE_A2, 5000)
        ws.op('{"id":10,"op":"resetEdit"}')
        ws.drain(0.5)

        drawn.set()
        ws.close()


def reply_of(raw):
    if raw is None:
        return {}
    try:
        import json
        return json.loads(raw.decode("utf-8"))
    except Exception:
        return {}


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
        # The pointer alone places the points: no on-view parameter to
        # take the click first.
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").SetInt(
                "OnViewParameterVisibility", 0)

        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        for name in (OBJ_A, OBJ_B):
            sk = doc.addObject("Sketcher::SketchObject", name)
            sk.ViewObject.Autoconstraints = False
            sk.ViewObject.GridAuto = False
        doc.recompute()
        check("both sketches start empty",
              not doc.getObject(OBJ_A).Geometry and not doc.getObject(OBJ_B).Geometry)

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


def toggle_construction():
    """Run the toggle on this side, with the client's edit in force."""
    try:
        FreeCADGui.Selection.clearSelection()
        active = FreeCADGui.getDocument(DOC)
        check("the client's edit put the sketch in edit here",
              active.getInEdit() is not None)
        FreeCADGui.runCommand("Sketcher_ToggleConstruction")
        state["toggled"] = True
    except Exception:
        note("ABORT toggle:\n" + traceback.format_exc())
    toggled.set()


def poll():
    client = state["client"]
    if client.error:
        QtCore.QTimer.singleShot(200, verify)
        return
    try:
        if state["phase"] == "start" and edited.is_set():
            state["phase"] = "toggling"
            # After the reply, so the edit is fully in place.
            QtCore.QTimer.singleShot(300, toggle_construction)
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


def construction_flags(name):
    sk = state["doc"].getObject(name)
    return [sk.getConstruction(i) for i in range(len(sk.Geometry))]


def verify():
    client = state["client"]
    try:
        check("the client ran without error", client.error is None, client.error or "")
        check("the hello was answered with a snapshot", client.snapshot)
        check("the toggle command ran", state["toggled"])
        for i, raw in enumerate(client.edits):
            check("edit %d is accepted" % (i + 1),
                  reply_of(raw).get("ok") is True, reply_of(raw))

        a = construction_flags(OBJ_A)
        b = construction_flags(OBJ_B)
        if not check("each tool run drew its line", len(a) == 2 and len(b) == 1,
                     (len(a), len(b))):
            finish()
            return

        check("the line drawn after the toggle is construction geometry", a[0], a)
        # The one that fails against a single global mode.
        check("the other sketch, never toggled, draws normal geometry",
              b[0] is False, b)
        check("and the mode was still on when the first sketch was re-entered",
              a[1], a)
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    toggled.set()
    edited.set()
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
