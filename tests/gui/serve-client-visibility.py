"""A served client's own object visibility steers the host's picks for it.

docs/CoinRetirement.md 5.18, per client: a browser client sets its own
ObjectVisibilities map with the `view.visibility` op. The host parses it
(a subname path needs the document) onto that client's MirrorViewer, which
holds it the way a desktop view holds its own, and the shared served root
sets it for that client's traversals -- so the picks the host resolves for
that client follow it, and every other client's do not.

Two clients look straight down at two visible boxes and one hidden one.
Each claim is a pick resolved on the host for one client, read back off
the selection that client is told about. Claims:
  - both clients pick Box2 before any table;
  - a bare hide in client A (perView on) takes Box2 out of A's picks,
    while client B still picks it;
  - a bare show in A picks the hidden box in A, and still not in B;
  - a path entry ("Box2.") hides it in A with perView off;
  - clearing A's map gives A back its picks.

Scored against the tree before the feature: the op does not exist and
its reply is an UnknownOp error.
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
DOC = "ServeClientVis"
CLIENT_WAIT_S = 120

# Straight down onto the boxes, from high enough to see all three.
EYE = (20.0, 10.0, 90.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 300.0
VW, VH = 800, 600
TOP_Z = 10.0

# Top-face centres.
BOX = (5.0, 5.0)
BOX2 = (25.0, 5.0)
HID = (25.0, 25.0)

state = {"port": 0, "done": False, "t0": clock(), "clients": []}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def ray_to(x, y):
    depth = EYE[2] - TOP_Z
    direction = ((x - EYE[0]) / depth, (y - EYE[1]) / depth, -1.0)
    length = math.sqrt(sum(c * c for c in direction))
    return EYE, tuple(c / length for c in direction)


class Client(threading.Thread):
    """One connection. Steps are driven from the conversation below; each
    pick's answer is the selection this client is told about next."""

    def __init__(self, name, port):
        super().__init__(daemon=True)
        self.name = name
        self.port = port
        self.error = None
        self.ws = None
        self.ready = threading.Event()
        self.steps = []
        self.results = {}
        self.lock = threading.Lock()
        self.cv = threading.Condition(self.lock)

    def run(self):
        try:
            ws = WS(self.port)
            self.ws = ws
            ws.send(1, ('{"cmd":"hello","client":"%s","snapshot":0}' % self.name).encode())
            if ws.next_binary(20.0) is None:
                self.error = "no snapshot"
                return
            ws.next_binary(0.5)
            ws.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR,
                                             VW, VH, pick_radius=2.0))
            self.ready.set()
            while True:
                with self.cv:
                    while not self.steps:
                        self.cv.wait()
                    tag, kind, arg = self.steps.pop(0)
                if kind == "quit":
                    return
                if kind == "op":
                    reply = ws.op(arg.encode())
                    value = jsonlib.loads(reply.decode("utf-8")) if reply else None
                else:
                    since = len(ws.pushes)
                    ws.send(2, wsclient.pick_frame(*ray_to(*arg), 0))
                    raw = ws.next_push("selection", 3.0, since=since)
                    items = jsonlib.loads(raw.decode("utf-8")).get("items", []) if raw else []
                    value = sorted({it["obj"] for it in items})
                with self.cv:
                    self.results[tag] = value
                    self.cv.notify_all()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()

    def ask(self, tag, kind, arg=None, timeout=15.0):
        with self.cv:
            self.steps.append((tag, kind, arg))
            self.cv.notify_all()
            end = clock() + timeout
            while tag not in self.results and self.error is None:
                left = end - clock()
                if left <= 0:
                    return None
                self.cv.wait(left)
            return self.results.get(tag)


def conversation():
    """Off the GUI thread: the host answers ops and picks on its own."""
    a, b = state["clients"]
    out = state["out"] = {}
    try:
        for c in (a, b):
            c.ready.wait(30.0)
        out["a0"] = a.ask("a0", "pick", BOX2)
        out["b0"] = b.ask("b0", "pick", BOX2)
        out["hide"] = a.ask("hide", "op",
                            '{"id":11,"op":"view.visibility","map":{"Box2":"0"},"perView":true}')
        out["a1"] = a.ask("a1", "pick", BOX2)
        out["a1box"] = a.ask("a1box", "pick", BOX)
        out["b1"] = b.ask("b1", "pick", BOX2)
        out["show"] = a.ask("show", "op",
                            '{"id":12,"op":"view.visibility","map":{"Hid":"1"},"perView":true}')
        out["a2"] = a.ask("a2", "pick", HID)
        out["b2"] = b.ask("b2", "pick", HID)
        out["path"] = a.ask("path", "op",
                            '{"id":13,"op":"view.visibility","map":{"Box2.":"0"},"perView":false}')
        out["a3"] = a.ask("a3", "pick", BOX2)
        out["clear"] = a.ask("clear", "op", '{"id":14,"op":"view.visibility","map":{}}')
        out["a4"] = a.ask("a4", "pick", BOX2)
    except Exception:
        out["error"] = traceback.format_exc()
    for c in (a, b):
        c.ask("quit", "quit", timeout=0.1)
    state["talked"] = True


def build():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        doc = FreeCAD.newDocument(DOC, hidden=True)
        for name, x, y in (("Box", 0, 0), ("Box2", 20, 0), ("Hid", 20, 20)):
            box = doc.addObject("Part::Box", name)
            box.Length = box.Width = box.Height = 10
            box.Placement.Base = FreeCAD.Vector(x, y, 0)
        doc.recompute()
        doc.getObject("Hid").ViewObject.Visibility = False
        port = free_port()
        ok = FreeCADGui.serveDocument(doc, port)
        if not check("the document is served headless", ok, "port %d" % port):
            finish()
            return
        state["clients"] = [Client("vis-a", port), Client("vis-b", port)]
        for c in state["clients"]:
            c.start()
        state["talked"] = False
        threading.Thread(target=conversation, daemon=True).start()
        QtCore.QTimer.singleShot(50, poll)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def poll():
    if not state["talked"]:
        if clock() - state["t0"] > CLIENT_WAIT_S:
            check("the conversation finished", False)
            finish()
            return
        QtCore.QTimer.singleShot(20, poll)
        return
    verify()


def verify():
    out = state["out"]
    for c in state["clients"]:
        check("client %s ran without error" % c.name, c.error is None, c.error or "")
    check("the conversation ran without error", "error" not in out, out.get("error", ""))
    note("results: %s" % jsonlib.dumps(out))
    ok = lambda k: bool(out.get(k)) and out[k].get("ok") is True
    check("both clients pick Box2 before any table",
          out.get("a0") == ["Box2"] and out.get("b0") == ["Box2"],
          (out.get("a0"), out.get("b0")))
    check("the op takes A's hide", ok("hide"), out.get("hide"))
    check("A's bare hide takes Box2 out of A's picks", "Box2" not in (out.get("a1") or []),
          out.get("a1"))
    check("A still picks Box", out.get("a1box") == ["Box"], out.get("a1box"))
    check("B still picks Box2", out.get("b1") == ["Box2"], out.get("b1"))
    check("the op takes A's show", ok("show"), out.get("show"))
    check("A's bare show picks the hidden box in A", out.get("a2") == ["Hid"], out.get("a2"))
    check("B does not pick the hidden box", "Hid" not in (out.get("b2") or []), out.get("b2"))
    check("the op takes A's path entry", ok("path"), out.get("path"))
    check("a path entry hides Box2 in A with perView off",
          "Box2" not in (out.get("a3") or []), out.get("a3"))
    check("the op clears A's map", ok("clear"), out.get("clear"))
    check("clearing gives A back its picks", out.get("a4") == ["Box2"], out.get("a4"))
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(0, build)
