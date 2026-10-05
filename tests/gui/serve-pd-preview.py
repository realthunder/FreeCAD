"""A PartDesign edit preview on a served document reaches the session only.

The serving half of tests/gui/pd-preview-per-view.py. The preview used
to be document scene and document Visibility -- the tinted tool a child
of the base feature's switch, the feature hidden and its base shown --
so on a served document every client drew it, and a drag of the panel
spoiled the one scene they all share. Now the tool hangs in the
session's editing root, which the serving source publishes as the
session's tagged overlay (docs/ThinClient.md 8.12 item J), and the swap
is a transient entry of each session view's own visibility table, which
a client is told as its `visibility` push.

A document with a real 3D window AND a served connection, as
serve-shared-edit.py has: a body of a box and a pad on it. Client A
states a camera, so it has a mirror and joins every session; client B
only says hello, has no view and is in none.

Asserted, the desktop entering the pad's edit first:

  - A is told the swap: one draw hidden (the pad's), one shown (its
    base's) -- and B is told nothing;
  - on the host the tool is under the session's editing root, in the
    desktop window's graph, and NOT under the base feature's switch;
  - neither Visibility is written;
  - the desktop leaving is told to A as an empty table, and nothing of
    the tool is left in the root.

Then A entering the same edit, where the desktop window joins and its
panel drives the preview all the same:

  - A is told the swap again, and the tool is in the session's root;
  - A's resetEdit empties its table, and the Visibility found is the
    Visibility left.

And a second document with NO window, served beside the first, whose one
client enters the pad's edit: the session has no desktop view in it at
all, and the preview is still the session's --

  - the client is told the swap, the tool is in the session's root and
    not under the base's switch, no Visibility is written;
  - a change made while the panel is open is held back from the shape
    (the preview's pause), and leaving the edit applies it.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set and this
directory on PYTHONPATH.

Scored against the tree before the change: A is told no swap, the tool
is under the base's switch and both Visibility properties are written.
"""
import json
import math
import os
import threading
import time
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore

import wsclient
from wsclient import WS, free_port

clock = time.perf_counter

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ServePdPreview"
SOLO = "ServePdPreviewSolo"
CLIENT_WAIT_S = 150
V = FreeCAD.Vector

EYE = (5.0, 5.0, 120.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 400.0
VW, VH = 800, 600

state = {"doc": None, "a": None, "b": None, "done": False, "t0": clock(),
         "phase": "start", "samples": {}}
# GUI thread -> client threads
desktop_entered = threading.Event()
desktop_left = threading.Event()
client_sampled = threading.Event()
solo_sampled = threading.Event()
finished = threading.Event()


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def tables(pushes):
    """The (hidden, shown) counts of every `visibility` push, in order."""
    out = []
    for text in pushes:
        if b'"cmd":"visibility"' not in text:
            continue
        try:
            msg = json.loads(text.decode("utf-8"))
        except Exception:
            continue
        out.append((len(msg.get("hidden", [])), len(msg.get("shown", []))))
    return out


class Watcher(threading.Thread):
    """Client B: says hello and listens. No camera, so no mirror."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.ready = threading.Event()
        self.pushes = []

    def run(self):
        try:
            ws = WS(self.port, path="/scene?doc=" + DOC)
            ws.hello("serve-pd-preview-b")
            self.snapshot = ws.next_binary(20.0) is not None
            self.ready.set()
            while not finished.is_set():
                ws.drain(0.2)
            ws.drain(0.3)
            self.pushes = list(ws.pushes)
            ws.close()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()


class Client(threading.Thread):
    """Client A: states a camera, watches the desktop's session, then
    enters one of its own."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.ready = threading.Event()
        self.watched = threading.Event()
        self.entered = threading.Event()
        self.in_desktop = []
        self.after_desktop = []
        self.edit_reply = None
        self.in_own = []
        self.reset = None
        self.after_own = []

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()
            self.watched.set()
            self.entered.set()

    def since(self, ws, mark, wait=1.5):
        ws.drain(wait)
        return tables(ws.pushes[mark:])

    def talk(self):
        ws = WS(self.port, path="/scene?doc=" + DOC)
        ws.hello("serve-pd-preview-a")
        self.snapshot = ws.next_binary(20.0) is not None
        if not self.snapshot:
            self.ready.set()
            return
        ws.next_binary(0.5)
        ws.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH))
        ws.drain(0.5)
        mark = len(ws.pushes)
        self.ready.set()

        # A. The desktop enters; this client joined without asking.
        desktop_entered.wait(30.0)
        self.in_desktop = self.since(ws, mark)
        mark = len(ws.pushes)
        self.watched.set()
        desktop_left.wait(30.0)
        self.after_desktop = self.since(ws, mark)
        mark = len(ws.pushes)

        # B. This client enters the same edit.
        raw = ws.op('{"id":2,"op":"edit","obj":"Pad","mode":0}')
        self.edit_reply = json.loads(raw.decode("utf-8")) if raw else None
        self.in_own = self.since(ws, mark)
        mark = len(ws.pushes)
        self.entered.set()
        client_sampled.wait(30.0)
        raw = ws.op('{"id":3,"op":"resetEdit"}')
        self.reset = json.loads(raw.decode("utf-8")) if raw else None
        self.after_own = self.since(ws, mark)
        ws.close()


class Solo(threading.Thread):
    """The one client of the document that has no window."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.entered = threading.Event()
        self.edit_reply = None
        self.in_own = []
        self.reset = None
        self.after_own = []

    def run(self):
        try:
            ws = WS(self.port, path="/scene?doc=" + SOLO)
            ws.hello("serve-pd-preview-solo")
            self.snapshot = ws.next_binary(20.0) is not None
            if not self.snapshot:
                self.entered.set()
                return
            ws.next_binary(0.5)
            ws.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH))
            ws.drain(0.5)
            mark = len(ws.pushes)
            raw = ws.op('{"id":2,"op":"edit","obj":"Pad","mode":0}')
            self.edit_reply = json.loads(raw.decode("utf-8")) if raw else None
            ws.drain(1.5)
            self.in_own = tables(ws.pushes[mark:])
            mark = len(ws.pushes)
            self.entered.set()
            solo_sampled.wait(30.0)
            raw = ws.op('{"id":3,"op":"resetEdit"}')
            self.reset = json.loads(raw.decode("utf-8")) if raw else None
            ws.drain(1.5)
            self.after_own = tables(ws.pushes[mark:])
            ws.close()
        except Exception:
            self.error = traceback.format_exc()
            self.entered.set()


def gdoc():
    return FreeCADGui.getDocument(DOC)


def named(node, name):
    """How many nodes called `name` are under `node`."""
    from pivy import coin

    if node is None:
        return -1
    search = coin.SoSearchAction()
    search.setName(name)
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(node)
    return search.getPaths().getLength()


def sample(tag):
    """Where the tool is, and what the document says is visible."""
    from pivy import coin

    doc = state["doc"]
    views = gdoc().mdiViewsOfType("Gui::View3DInventor")
    graph = views[0].getViewer().getSoRenderManager().getSceneGraph()
    search = coin.SoSearchAction()
    search.setName("EditingRoot")
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(graph)
    paths = search.getPaths()
    root = paths[0].getTail() if paths.getLength() else None
    state["samples"][tag] = {
        "edit": gdoc().getInEdit() is not None,
        "roots": paths.getLength(),
        "in_root": named(root, "Pad_preview") if root is not None else 0,
        "in_base": named(doc.Box.ViewObject.SwitchNode, "Pad_preview"),
        "pad": doc.Pad.Visibility,
        "box": doc.Box.Visibility,
        "views": len(views),
    }


def sample_solo():
    """The windowless document's session: where the tool is, what is
    visible, and whether a change is held back from the shape."""
    from pivy import coin

    doc = FreeCAD.getDocument(SOLO)
    # The one live holder of a session node: the other document's
    # sessions are over, and theirs went with them.
    holder = coin.SoNode.getByName("EditingSessionNode")
    before = doc.Pad.Shape.Volume
    doc.openTransaction("Edit Pad")
    doc.Pad.Length = 30
    doc.Pad.recompute(True)
    state["samples"]["solo"] = {
        "in_root": named(holder, "Pad_preview") if holder is not None else 0,
        "in_base": named(doc.Box.ViewObject.SwitchNode, "Pad_preview"),
        "pad": doc.Pad.Visibility,
        "box": doc.Box.Visibility,
        "before": before,
        "held": doc.Pad.Shape.Volume,
    }


def make_body(doc):
    body = doc.addObject("PartDesign::Body", "Body")
    box = body.newObject("PartDesign::AdditiveBox", "Box")
    box.Length = box.Width = box.Height = 10
    sketch = body.newObject("Sketcher::SketchObject", "Sketch")
    sketch.Support = (doc.getObject("XY_Plane"), [""])
    sketch.MapMode = "FlatFace"
    corners = [V(3, 3, 0), V(7, 3, 0), V(7, 7, 0), V(3, 7, 0)]
    for i in range(4):
        sketch.addGeometry(Part.LineSegment(corners[i], corners[(i + 1) % 4]))
    pad = body.newObject("PartDesign::Pad", "Pad")
    pad.Profile = sketch
    pad.Length = 20
    doc.recompute()
    # As the commands leave a body: its last feature shown alone
    box.Visibility = False
    sketch.Visibility = False


def build():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        part = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Part")
        part.SetBool("PreviewOnEdit", True)
        part.SetBool("EditOnTop", False)
        # Not hidden: the document has a real 3D window beside the served
        # connection.
        solo = FreeCAD.newDocument(SOLO, hidden=True)
        make_body(solo)
        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        make_body(doc)
        sample("idle")

        port = free_port()
        ok = FreeCADGui.serveDocument(doc, port)
        if not check("the document is served beside its window", ok, "port %d" % port):
            finish()
            return
        # One server, one port: a connection names its document
        # (docs/MultiDocServe.md)
        state["solo_port"] = port
        ok = FreeCADGui.serveDocument(solo)
        if not check("the windowless document is served too", ok):
            finish()
            return
        state["a"] = Client(port)
        state["b"] = Watcher(port)
        state["a"].start()
        state["b"].start()
        QtCore.QTimer.singleShot(50, poll)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def poll():
    a, b, solo = state["a"], state["b"], state.get("solo")
    if a.error or b.error or (solo and solo.error):
        QtCore.QTimer.singleShot(200, verify)
        return
    phase = state["phase"]
    try:
        if phase == "start" and a.ready.is_set() and b.ready.is_set():
            state["phase"] = "desktop-edit"
            opened = gdoc().setEdit(state["doc"].Pad, 0)
            check("the desktop entered the pad's edit", opened, opened)
            QtCore.QTimer.singleShot(800, lambda: (sample("desktop"),
                                                   desktop_entered.set()))
        elif phase == "desktop-edit" and a.watched.is_set():
            state["phase"] = "desktop-leave"
            gdoc().resetEdit()
            QtCore.QTimer.singleShot(800, lambda: (sample("between"),
                                                   desktop_left.set()))
        elif phase == "desktop-leave" and a.entered.is_set():
            state["phase"] = "client-edit"
            QtCore.QTimer.singleShot(500, lambda: (sample("client"),
                                                   client_sampled.set()))
        elif phase == "client-edit" and not a.is_alive():
            state["phase"] = "solo"
            state["solo"] = solo = Solo(state["solo_port"])
            solo.start()
        elif phase == "solo" and solo.entered.is_set():
            state["phase"] = "solo-edit"
            QtCore.QTimer.singleShot(300, lambda: (sample_solo(), solo_sampled.set()))
        elif phase == "solo-edit" and not solo.is_alive():
            state["phase"] = "end"
            finished.set()
    except Exception:
        note("ABORT poll:\n" + traceback.format_exc())
        QtCore.QTimer.singleShot(200, verify)
        return
    if a.is_alive() or b.is_alive() or state["phase"] != "end":
        if clock() - state["t0"] > CLIENT_WAIT_S:
            check("the clients finished", False,
                  "still talking after %ds in phase %s" % (CLIENT_WAIT_S, state["phase"]))
            finish()
            return
        QtCore.QTimer.singleShot(20, poll)
        return
    QtCore.QTimer.singleShot(500, verify)


def verify():
    a, b = state["a"], state["b"]
    s = state["samples"]
    try:
        check("client A ran without error", a.error is None, a.error or "")
        check("client B ran without error", b.error is None, b.error or "")
        check("both hellos were answered with a snapshot", a.snapshot and b.snapshot)
        sample("end")
        idle, desk, between = s.get("idle", {}), s.get("desktop", {}), s.get("between", {})
        own, end = s.get("client", {}), s.get("end", {})

        # A. The desktop's session.
        check("the desktop's session is on", desk.get("edit") is True, desk)
        check("a client with a view is told the swap: one draw hidden, one shown",
              bool(a.in_desktop) and a.in_desktop[-1] == (1, 1), a.in_desktop)
        check("the tool is under the session's editing root, in the window's graph",
              desk.get("roots") == 1 and desk.get("in_root") == 1, desk)
        check("and not under the base feature's switch", desk.get("in_base") == 0, desk)
        check("entering wrote neither Visibility",
              desk.get("pad") is True and desk.get("box") is False, desk)
        check("the desktop leaving empties the client's table",
              bool(a.after_desktop) and a.after_desktop[-1] == (0, 0), a.after_desktop)
        check("and leaves nothing of the tool in a root",
              between.get("edit") is False and between.get("in_root") == 0
              and between.get("in_base") == 0, between)

        # B. The client's session.
        check("the client's edit is accepted",
              (a.edit_reply or {}).get("ok") is True, a.edit_reply)
        check("the window reports the client's session", own.get("edit") is True, own)
        check("no 3D view was created for it",
              own.get("views") == idle.get("views") == 1, (idle.get("views"), own.get("views")))
        check("the client is told the swap of its own session",
              bool(a.in_own) and a.in_own[-1] == (1, 1), a.in_own)
        check("its tool is under the session's root, not the base's switch",
              own.get("in_root") == 1 and own.get("in_base") == 0, own)
        check("and neither Visibility is written",
              own.get("pad") is True and own.get("box") is False, own)
        check("the client's resetEdit is accepted",
              (a.reset or {}).get("ok") is True, a.reset)
        check("and empties its table",
              bool(a.after_own) and a.after_own[-1] == (0, 0), a.after_own)
        check("the end is as the start: no session, no tool, the Visibility found",
              end.get("edit") is False and end.get("in_root") == 0
              and end.get("in_base") == 0 and end.get("pad") is True
              and end.get("box") is False, end)

        # The client outside every session
        told = tables(b.pushes)
        check("a client with no view is told no swap at any point",
              all(t == (0, 0) for t in told), told)

        # C. The document with no window at all.
        solo = state.get("solo")
        if check("the windowless document's client ran",
                 solo is not None and solo.error is None and solo.snapshot,
                 solo.error if solo else "never started"):
            one = s.get("solo", {})
            pad = FreeCAD.getDocument(SOLO).Pad
            check("its edit is accepted",
                  (solo.edit_reply or {}).get("ok") is True, solo.edit_reply)
            check("no window: the client is told the swap",
                  bool(solo.in_own) and solo.in_own[-1] == (1, 1), solo.in_own)
            check("no window: the tool is in the session's root, not the base's switch",
                  one.get("in_root") == 1 and one.get("in_base") == 0, one)
            check("no window: neither Visibility is written",
                  one.get("pad") is True and one.get("box") is False, one)
            check("no window: a change is held back from the shape while the panel is open",
                  one.get("before") is not None
                  and abs(one["held"] - one["before"]) < 1e-6, one)
            check("no window: the client's resetEdit is accepted",
                  (solo.reset or {}).get("ok") is True, solo.reset)
            check("no window: and empties its table",
                  bool(solo.after_own) and solo.after_own[-1] == (0, 0), solo.after_own)
            check("no window: leaving applies the change",
                  one.get("before") is not None
                  and pad.Shape.Volume > one["before"] + 1.0
                  and "Touched" not in pad.State,
                  (pad.Shape.Volume, one.get("before"), pad.State))
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    desktop_entered.set()
    desktop_left.set()
    client_sampled.set()
    solo_sampled.set()
    finished.set()
    try:
        if gdoc().getInEdit():
            gdoc().resetEdit()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, build)
