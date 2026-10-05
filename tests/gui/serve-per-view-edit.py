"""On a served document, an edit that is one view's is told to that view only.

The serving half of tests/gui/edit-per-view.py. With the preference
PerViewEdit a session is its initiating view's alone: the serving source
joins no other mirror to it, tells no other client that it began or
ended -- a browser told of an edit sends its left button up the input
channel and draws the session's overlay -- and neither lets another
connection end it nor replace it with an edit of its own.

A document with a real 3D window and two served clients, A and B, both
with a stated camera (so both would join a shared session): a body of a
box and a pad, the pad's PartDesign preview on, so that a view in the
session is told the preview's swap of its visibility table.

Asserted, with PerViewEdit on and the DESKTOP entering:

  - neither client is told an edit began, or any swap;
  - a client's resetEdit is refused (NotInSession) and so is its edit
    (EditInProgress): the desktop's session survives both;
  - neither is told anything when the desktop leaves.

Then client A entering:

  - A is told the edit and the swap; B is told neither;
  - the desktop window did not join: the session's editing root is not
    in its graph, and asked from the window the document is not in edit;
  - B can neither leave A's edit nor replace it;
  - A leaves it, and is the only one told.

And with PerViewEdit off, the control: the desktop entering is told to
both clients, with the swap, and so is its leaving.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set and this
directory on PYTHONPATH.

Not scored against the tree before the preference: there every session
is shared, which is the behaviour the control at the end pins.
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
DOC = "ServePerViewEdit"
CLIENT_WAIT_S = 200
V = FreeCAD.Vector

EYE = (5.0, 5.0, 120.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 400.0
VW, VH = 800, 600

VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")

state = {"doc": None, "talk": None, "done": False, "t0": clock(), "samples": {}}
# The conversation asks the GUI thread for a step and waits for it
wanted = {"name": None}
pending = threading.Event()
served = threading.Event()


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def gui(name):
    """Have the GUI thread do `name`, and wait until it has."""
    wanted["name"] = name
    served.clear()
    pending.set()
    if not served.wait(40.0):
        raise RuntimeError("the GUI thread never did " + name)


def told(pushes):
    """What a run of pushes says: the edit edges, and the visibility
    tables as (hidden, shown) counts."""
    edits, tables = [], []
    for text in pushes:
        try:
            msg = json.loads(text.decode("utf-8"))
        except Exception:
            continue
        if msg.get("cmd") == "edit":
            edits.append(bool(msg.get("editing")))
        elif msg.get("cmd") == "visibility":
            tables.append((len(msg.get("hidden", [])), len(msg.get("shown", []))))
    return {"edits": edits, "swaps": [t for t in tables if t != (0, 0)], "tables": tables}


def error_of(raw):
    """The error code of a refused op's reply, "" when it was accepted."""
    if raw is None:
        return "no reply"
    try:
        msg = json.loads(raw.decode("utf-8"))
    except Exception:
        return "unreadable"
    if msg.get("ok"):
        return ""
    return str(msg.get("code", msg))


class Talk(threading.Thread):
    """Both clients' conversations, in order, off the GUI thread."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.got = {}

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()

    def connect(self, label):
        ws = WS(self.port)
        ws.hello(label)
        if ws.next_binary(20.0) is None:
            raise RuntimeError(label + ": no snapshot")
        ws.next_binary(0.5)
        ws.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH))
        ws.drain(0.5)
        return ws

    def both(self, a, b, key, wait=1.5):
        """What each was told since the last reading."""
        a.drain(wait)
        b.drain(0.3)
        self.got[key] = (told(a.pushes[self.mark_a:]), told(b.pushes[self.mark_b:]))
        self.mark_a, self.mark_b = len(a.pushes), len(b.pushes)

    def talk(self):
        a = self.connect("serve-per-view-edit-a")
        b = self.connect("serve-per-view-edit-b")
        self.mark_a, self.mark_b = len(a.pushes), len(b.pushes)
        edit = '{"id":%d,"op":"edit","obj":"Pad","mode":0}'
        reset = '{"id":%d,"op":"resetEdit"}'

        # 1. The desktop's own session
        gui("per-view")
        gui("desktop-enter")
        self.both(a, b, "desktop-in")
        self.got["a-reset-desktop"] = error_of(a.op(reset % 11))
        self.got["a-edit-desktop"] = error_of(a.op(edit % 12))
        gui("sample:desktop")
        gui("desktop-leave")
        self.both(a, b, "desktop-out")

        # 2. Client A's own session
        self.got["a-edit"] = error_of(a.op(edit % 21))
        self.both(a, b, "a-in")
        self.got["b-reset-a"] = error_of(b.op(reset % 22))
        self.got["b-edit-a"] = error_of(b.op(edit % 23))
        gui("sample:client")
        self.got["a-reset"] = error_of(a.op(reset % 24))
        self.both(a, b, "a-out")
        gui("sample:between")

        # 3. The control: a shared session
        gui("shared")
        gui("desktop-enter")
        self.both(a, b, "shared-in")
        gui("desktop-leave")
        self.both(a, b, "shared-out")
        a.close()
        b.close()


def gdoc():
    return FreeCADGui.getDocument(DOC)


def window():
    return gdoc().mdiViewsOfType("Gui::View3DInventor")[0]


def edit_roots():
    """How many times the session's editing root is in the window's graph."""
    from pivy import coin

    graph = window().getViewer().getSoRenderManager().getSceneGraph()
    search = coin.SoSearchAction()
    search.setName("EditingRoot")
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(graph)
    return search.getPaths().getLength()


def do(name):
    doc = state["doc"]
    if name == "per-view":
        VIEW.SetBool("PerViewEdit", True)
    elif name == "shared":
        VIEW.SetBool("PerViewEdit", False)
    elif name == "desktop-enter":
        FreeCADGui.getMainWindow().setActiveWindow(window())
        state["samples"]["entered"] = gdoc().setEdit(doc.Pad, 0)
    elif name == "desktop-leave":
        gdoc().resetEdit()
    elif name.startswith("sample:"):
        FreeCADGui.getMainWindow().setActiveWindow(window())
        state["samples"][name[7:]] = {
            "asked": gdoc().getInEdit() is not None,
            "roots": edit_roots(),
            "pad": doc.Pad.Visibility,
            "box": doc.Box.Visibility,
            "views": len(gdoc().mdiViewsOfType("Gui::View3DInventor")),
        }


def build():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        VIEW.SetInt("RenderCache", 3)
        VIEW.SetBool("PerViewEdit", False)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        part = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Part")
        part.SetBool("PreviewOnEdit", True)
        part.SetBool("EditOnTop", False)
        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
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
        box.Visibility = False
        sketch.Visibility = False

        port = free_port()
        ok = FreeCADGui.serveDocument(doc, port)
        if not check("the document is served beside its window", ok, "port %d" % port):
            finish()
            return
        state["talk"] = Talk(port)
        state["talk"].start()
        QtCore.QTimer.singleShot(50, poll)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def poll():
    talk = state["talk"]
    if pending.is_set():
        pending.clear()
        try:
            do(wanted["name"])
        except Exception:
            note("ABORT step %s:\n%s" % (wanted["name"], traceback.format_exc()))
        # Let what the step raised settle before the conversation reads
        QtCore.QTimer.singleShot(500, served.set)
    if talk.is_alive():
        if clock() - state["t0"] > CLIENT_WAIT_S:
            check("the conversation finished", False, "still talking after %ds" % CLIENT_WAIT_S)
            finish()
            return
        QtCore.QTimer.singleShot(20, poll)
        return
    QtCore.QTimer.singleShot(500, verify)


def verify():
    talk = state["talk"]
    got, s = talk.got, state["samples"]
    nothing = {"edits": [], "swaps": [], "tables": []}
    try:
        check("the conversation ran without error", talk.error is None, talk.error or "")

        # 1. The desktop's session, per view
        a, b = got.get("desktop-in", (None, None))
        check("the desktop entered the pad's edit", s.get("entered") is True, s.get("entered"))
        check("per view: client A is told nothing of the desktop's edit",
              a is not None and not a["edits"] and not a["swaps"], a)
        check("per view: nor is client B",
              b is not None and not b["edits"] and not b["swaps"], b)
        check("per view: a client's resetEdit is refused",
              got.get("a-reset-desktop") == "NotInSession", got.get("a-reset-desktop"))
        check("per view: and so is its edit",
              got.get("a-edit-desktop") == "EditInProgress", got.get("a-edit-desktop"))
        desk = s.get("desktop", {})
        check("per view: the desktop's session survived both",
              desk.get("asked") is True and desk.get("roots") == 1, desk)
        a, b = got.get("desktop-out", (None, None))
        check("per view: neither client is told the desktop left",
              a is not None and not a["edits"] and b is not None and not b["edits"], (a, b))

        # 2. Client A's session, per view
        a, b = got.get("a-in", (nothing, nothing))
        check("client A's edit is accepted", got.get("a-edit") == "", got.get("a-edit"))
        check("per view: A is told its edit began, and the preview's swap",
              a["edits"] == [True] and bool(a["swaps"]) and a["swaps"][-1] == (1, 1), a)
        check("per view: B is told neither", not b["edits"] and not b["swaps"], b)
        check("per view: B cannot leave A's edit",
              got.get("b-reset-a") == "NotInSession", got.get("b-reset-a"))
        check("per view: nor replace it",
              got.get("b-edit-a") == "EditInProgress", got.get("b-edit-a"))
        own = s.get("client", {})
        check("per view: the desktop window did not join A's session",
              own.get("roots") == 0 and own.get("asked") is False, own)
        check("per view: no 3D view was created, no Visibility written",
              own.get("views") == 1 and own.get("pad") is True and own.get("box") is False,
              own)
        a, b = got.get("a-out", (nothing, nothing))
        check("A's resetEdit is accepted", got.get("a-reset") == "", got.get("a-reset"))
        check("per view: A is told its edit ended, and its table is empty again",
              a["edits"] == [False] and bool(a["tables"]) and a["tables"][-1] == (0, 0), a)
        check("per view: B is told nothing of it", not b["edits"] and not b["swaps"], b)
        between = s.get("between", {})
        check("the document is out of edit", between.get("asked") is False
              and between.get("roots") == 0, between)

        # 3. The control
        a, b = got.get("shared-in", (nothing, nothing))
        check("shared: both clients are told the desktop's edit began",
              a["edits"] == [True] and b["edits"] == [True], (a["edits"], b["edits"]))
        check("shared: and both the swap",
              bool(a["swaps"]) and a["swaps"][-1] == (1, 1)
              and bool(b["swaps"]) and b["swaps"][-1] == (1, 1), (a["swaps"], b["swaps"]))
        a, b = got.get("shared-out", (nothing, nothing))
        check("shared: and both that it ended",
              a["edits"] == [False] and b["edits"] == [False], (a["edits"], b["edits"]))
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    served.set()
    VIEW.SetBool("PerViewEdit", False)
    try:
        gdoc().resetEdit()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, build)
