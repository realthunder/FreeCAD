"""A client of a served document asks for an object's last change, and it is
its session's.

The serving half of tests/gui/shape-diff-edit.py (docs/TransactionLog.md
sec 32.19). The two states of a shape shown as the last change of an object
are an edit mode, so a client asks for them with the `edit` op it has
always had -- the mode is Gui::ViewProvider::ValueDiff, 6 -- and they hang
in the edit session's root, which the serving source publishes as the
session's tagged overlay: drawn by the clients in the session, and not in
the scene every client shares. The task's panel reaches the client by the
panel mirror, slider and all.

A document served with no window. Client A states a camera, so it has a
view; client B only says hello and has none. Asserted:

  - the object's menu as the host sends it to A has "Show last change", an
    edit's entry; run from there it is taken, and A is told the session --
    B is told nothing;
  - on the host the two states are under a node of the session, and not
    under the object's root, where every client would draw them;
  - the panel is mirrored: A subscribes, the slider is among what it is
    sent, and A's write to it moves the two states -- and is published;
  - A's `resetEdit` ends it: A is told, and nothing of the two states is
    left on the host;
  - none of it wrote a row of the log or an undo step.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set and this
directory on PYTHONPATH.
"""
import json
import math
import os
import queue
import tempfile
import threading
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

import wsclient
from wsclient import WS, free_port

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ServeShapeDiffEdit"
MODE = 6  # Gui::ViewProvider::ValueDiff

EYE = (5.0, 5.0, 120.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 400.0
VW, VH = 800, 600

state = {"done": False}
asked = queue.Queue()
finished = threading.Event()
client_done = threading.Event()


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def on_host(fn, timeout=30.0):
    """Run `fn` on the GUI thread, from a client's thread; its result."""
    box = {"done": threading.Event()}
    asked.put((fn, box))
    if not box["done"].wait(timeout):
        raise RuntimeError("the host did not answer")
    if "error" in box:
        raise RuntimeError(box["error"])
    return box.get("result")


def serve_asked():
    while True:
        try:
            fn, box = asked.get_nowait()
        except queue.Empty:
            break
        try:
            box["result"] = fn()
        except Exception:
            box["error"] = traceback.format_exc()
        box["done"].set()
    if client_done.is_set():
        verify()
    elif not state["done"]:
        QtCore.QTimer.singleShot(20, serve_asked)


def named(node, name):
    from pivy import coin

    if node is None:
        return -1
    search = coin.SoSearchAction()
    search.setName(name)
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(node)
    return search.getPaths().getLength()


def sample():
    """Where the two states are, on the host."""
    from pivy import coin

    doc = FreeCAD.getDocument(DOC)
    gdoc = FreeCADGui.getDocument(DOC)
    cut = doc.getObject("Cut")
    holder = coin.SoNode.getByName("EditingSessionNode")
    return {
        "edit": gdoc.getInEdit() is not None,
        "in_session": named(holder, "ShapeDiff") if holder is not None else 0,
        "in_object": named(cut.ViewObject.RootNode, "ShapeDiff"),
        "panel": bool(FreeCADGui.Control.activeDialog()),
        "found": dict(cut.ViewObject.getValueDiff()),
        "written": (len(doc.getTransactionLog()), doc.UndoCount, doc.RedoCount),
    }


def mirrored_slider():
    fw = FreeCADGui.FormWidgets
    for wid in fw.ids():
        if wid.startswith("pw:"):
            snap = fw.snapshot(wid)
            if snap["state"].get("q_objectName") == "diffPosition":
                return wid, snap["model"], snap["state"].get("q_value")
    return None


def walk(items):
    """Every entry of a menu, submenus flattened"""
    for item in items or []:
        if "items" in item:
            yield from walk(item["items"])
        elif not item.get("separator"):
            yield item


def edits(pushes):
    out = []
    for raw in pushes:
        try:
            msg = json.loads(raw.decode("utf-8"))
        except Exception:
            continue
        if msg.get("cmd") == "edit":
            out.append((msg.get("editing"), msg.get("obj"), msg.get("session")))
    return out


class Watcher(threading.Thread):
    """Client B: says hello and listens. No camera, so no view."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.ready = threading.Event()
        self.pushes = []
        self.over = threading.Event()

    def run(self):
        try:
            ws = WS(self.port)
            ws.hello("serve-shape-diff-edit-b")
            ws.next_binary(20.0)
            self.ready.set()
            while not finished.is_set():
                ws.drain(0.2)
            ws.drain(0.3)
            self.pushes = list(ws.pushes)
            ws.close()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()
        self.over.set()


class Client(threading.Thread):
    """Client A: states a camera and asks for the last change."""

    def __init__(self, port, watcher):
        super().__init__(daemon=True)
        self.port = port
        self.watcher = watcher
        self.error = None
        self.got = {}

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()
        client_done.set()

    def op(self, ws, payload):
        raw = ws.op(json.dumps(payload))
        return json.loads(raw.decode("utf-8")) if raw else None

    def talk(self):
        got = self.got
        ws = WS(self.port)
        ws.hello("serve-shape-diff-edit-a")
        if ws.next_binary(20.0) is None:
            raise RuntimeError("no snapshot")
        ws.next_binary(0.5)
        ws.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH))
        ws.drain(0.5)
        self.watcher.ready.wait(30.0)
        got["before"] = on_host(sample)
        mark = len(ws.pushes)

        # The entry of the object's own menu, as a browser's right click
        # gets it from the host (docs/ThinClient.md sec 8.11b): a ray down
        # onto the box, beside the hole.
        menu = self.op(ws, {"id": 20, "op": "contextMenu",
                            "ray": [2.0, 2.0, EYE[2], 0, 0, -1]}) or {}
        entry = [e for e in walk(menu.get("items")) if e.get("text") == "Show last change"]
        got["entry"] = entry[0] if entry else None
        if entry and entry[0].get("allowed"):
            got["edit"] = self.op(ws, {"id": 2, "op": "contextMenu.trigger",
                                       "menu": menu["menu"], "item": entry[0]["id"]})
        else:
            got["edit"] = self.op(ws, {"id": 2, "op": "edit", "obj": "Cut", "mode": MODE})
        ws.drain(1.5)
        got["told_in"] = edits(ws.pushes[mark:])
        got["in"] = on_host(sample)
        mark = len(ws.pushes)

        got["subscribe"] = self.op(ws, {"id": 3, "op": "widgets.subscribe", "panels": True})
        ws.drain(1.0)
        got["slider"] = on_host(mirrored_slider)
        if got["slider"]:
            ws.drain(0.5)
            pushes = 0
            got["write"] = self.op(ws, {"id": 4, "op": "widgets.update",
                                        "target": got["slider"][0], "state": {"q_value": 30}})
            end = time.perf_counter() + 2.0
            while time.perf_counter() < end:
                if ws.next_binary(0.2) is not None:
                    pushes += 1
            got["write_pushes"] = pushes
            got["moved"] = on_host(sample)
        self.op(ws, {"id": 5, "op": "widgets.unsubscribe"})
        mark = len(ws.pushes)

        got["reset"] = self.op(ws, {"id": 6, "op": "resetEdit"})
        ws.drain(1.5)
        got["told_out"] = edits(ws.pushes[mark:])
        got["after"] = on_host(sample)
        ws.close()


def build():
    try:
        p = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
        state["log"] = p.GetInt("TransactionLog", 2)
        p.SetInt("TransactionLog", 2)
        p.SetInt("AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        doc = FreeCAD.newDocument(DOC, hidden=True)
        doc.UndoMode = 1
        doc.openTransaction("base")
        box = doc.addObject("Part::Box", "Box")
        cyl = doc.addObject("Part::Cylinder", "Cyl")
        cyl.Radius, cyl.Height = 2, 30
        cyl.Placement.Base = FreeCAD.Vector(5, 5, -10)
        cut = doc.addObject("Part::Cut", "Cut")
        cut.Base, cut.Tool = box, cyl
        doc.recompute()
        doc.commitTransaction()
        doc.saveAs(os.path.join(tempfile.mkdtemp(prefix="fc-serve-sd-edit-"), "sd.FCStd"))
        doc.openTransaction("taller")
        box.Height = 16
        doc.recompute()
        doc.commitTransaction()

        port = free_port()
        if not check("the document is served with no window",
                     FreeCADGui.serveDocument(doc, port), "port %d" % port):
            finish()
            return
        watcher = Watcher(port)
        client = Client(port, watcher)
        state["watcher"], state["client"] = watcher, client
        QtCore.QTimer.singleShot(20, serve_asked)
        watcher.start()
        client.start()
        QtCore.QTimer.singleShot(150000, lambda: (check("the run ended", state["done"]), finish()))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
        finish()


def verify():
    if state["done"]:
        return
    try:
        client, watcher = state["client"], state["watcher"]
        finished.set()
        watcher.over.wait(5.0)
        got = client.got
        if client.error or watcher.error:
            check("the clients ran", False, client.error or watcher.error)
        before, now, after = got.get("before", {}), got.get("in", {}), got.get("after", {})

        entry = got.get("entry") or {}
        check("the object's menu, as the host sends it to a client, has the entry as an edit's",
              entry.get("kind") == "edit" and entry.get("allowed") is True,
              {k: entry.get(k) for k in ("text", "kind", "allowed", "enabled")})
        check("run from the menu it is taken, and the document is in the edit",
              (got.get("edit") or {}).get("ok") is True and now.get("edit") is True,
              got.get("edit"))
        told = got.get("told_in", [])
        check("the client with a view is told the session",
              any(t[0] is True and t[1] == "Cut" and t[2] for t in told), told)
        check("the client without one is told nothing", edits(watcher.pushes) == [],
              edits(watcher.pushes))
        check("the two states are a node of the session, not under the object's root",
              now.get("in_session") == 1 and now.get("in_object") == 0,
              (now.get("in_session"), now.get("in_object")))
        found = now.get("found", {})
        check("of the last change: the bottom as it was, six changed",
              found.get("same") == 1 and found.get("changed", 0) + found.get("moved", 0) == 6,
              {k: v for k, v in found.items() if "." not in k})
        slider = got.get("slider")
        check("the panel is mirrored, and has the slider",
              (got.get("subscribe") or {}).get("ok") is True and now.get("panel") is True
              and slider is not None and slider[1] == "QSliderModel" and slider[2] == 100,
              (got.get("subscribe"), slider))
        moved = got.get("moved", {})
        check("the client's write to the slider moves the two states",
              (got.get("write") or {}).get("ok") is True
              and moved.get("found", {}).get("position") == 30,
              (got.get("write"), moved.get("found", {}).get("position")))
        check("and is published", got.get("write_pushes", 0) >= 1, got.get("write_pushes"))
        check("the client's resetEdit ends it, and the client is told",
              (got.get("reset") or {}).get("ok") is True and after.get("edit") is False
              and any(t[0] is False for t in got.get("told_out", [])),
              (got.get("reset"), got.get("told_out")))
        check("nothing of the two states is left on the host, nor the panel",
              after.get("in_session") == 0 and after.get("in_object") == 0
              and after.get("panel") is False and after.get("found") == {},
              (after.get("in_session"), after.get("in_object"), after.get("panel")))
        check("none of it wrote a row of the log or an undo step",
              before.get("written") is not None and after.get("written") == before.get("written"),
              "%s, were %s" % (after.get("written"), before.get("written")))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    finished.set()
    try:
        if "log" in state:
            FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
                "TransactionLog", state["log"])
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, build)
