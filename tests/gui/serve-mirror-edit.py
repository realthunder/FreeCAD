"""An edit mode entered from a browser runs in that client's own view.

Stage 4 of docs/ThinClient.md sec 8.9, end to end over a real socket.
Everything the desktop reaches for when it starts an edit -- the active
window, the active view, the application's pointer -- names nothing in a
serving process, or names somebody else's. So the session is bound to
the connection's own mirror instead, and this is where that is checked
against a real sketch rather than against the classes.

What is asserted:

  - the `edit` control op is refused before the client has stated a
    camera. There is no view to bind to then, and setEdit's own fallback
    would CREATE a 3D view in a process that exists not to have one;
  - after a 'C' frame it is accepted, the document reports it is in edit,
    and the session is bound to that connection's mirror -- not to some
    view left over from anywhere else;
  - entering edit MOVES the sketch's scene graph under the editing root,
    which is in the served graph exactly so the change-driven traversal
    publishes it. Read as the view provider's own child count going to
    zero and coming back, the same reading the desktop probe takes;
  - an 'E' input event is replayed in that view: a pointer move over the
    sketch reaches the edit path and the server pushes the result;
  - the `resetEdit` op ends the session and gives the graph back;
  - and a client that drops mid-edit does not leave the document holding
    a pointer to a mirror that no longer exists. That is the case the
    process would not survive, so surviving it is the assertion.

The camera is a real one, stated the way the browser viewer states its
own: eye above the sketch plane looking straight down.

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
DOC = "ServeMirrorEdit"
OBJ = "Sketch"
CLIENT_WAIT_S = 120

# Eye straight above the sketch plane, looking down -Z with +Y up --
# the identity rotation in Coin's convention.
EYE = (0.0, 0.0, 60.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 200.0
VW, VH = 800, 600

state = {"doc": None, "client": None, "done": False, "t0": clock(), "seen": []}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def camera_frame():
    return wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH)


def reply_of(raw):
    if raw is None:
        return None
    try:
        return jsonlib.loads(raw.decode("utf-8"))
    except Exception:
        return None


class Client(threading.Thread):
    """The whole wire conversation, off the GUI thread."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.edit_without_camera = None
        self.edit_with_camera = None
        self.input_pushed = None
        self.reset = None

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()

    def talk(self):
        ws = WS(self.port)
        ws.hello("serve-mirror-edit")
        self.snapshot = ws.next_binary(20.0) is not None
        if not self.snapshot:
            return
        ws.next_binary(0.5)

        # 1. No camera stated yet, so no view to bind the session to.
        self.edit_without_camera = reply_of(ws.op(
            '{"id":1,"op":"edit","obj":"%s","mode":0}' % OBJ))

        # 2. With a camera, the connection has a mirror.
        ws.send(2, camera_frame())
        self.edit_with_camera = reply_of(ws.op(
            '{"id":2,"op":"edit","obj":"%s","mode":0}' % OBJ))
        ws.drain(0.5)

        # 3. A pointer move over the middle of the canvas, replayed in
        # that view. The edit path is what reads it; the publish that
        # follows is what says the server did something with it.
        ws.send(2, wsclient.input_frame(wsclient.MOVE, VW // 2, VH // 2,
                                        time_ms=1000))
        self.input_pushed = ws.next_binary(5.0) is not None
        ws.drain(0.5)

        # 4. Out of edit, on the client's word.
        self.reset = reply_of(ws.op('{"id":3,"op":"resetEdit"}'))

        # 5. Back in, and then the socket simply goes away. The document
        # is left in edit with its viewer about to be destroyed, which
        # is the case that used to leave a dangling pointer behind.
        reply_of(ws.op('{"id":4,"op":"edit","obj":"%s","mode":0}' % OBJ))
        ws.close()


def in_edit():
    gdoc = FreeCADGui.getDocument(DOC)
    return gdoc.getInEdit() is not None


def root_children():
    doc = state["doc"]
    return doc.getObject(OBJ).ViewObject.RootNode.getNumChildren()


def views_3d():
    """3D views this document has. Zero, throughout -- and that is the
    discriminating reading for the binding. Gui::Document::setEdit, given
    no view to bind to, does not fail: it CREATES one (setActiveView).
    So a session that had not been told which view it belongs to would
    still enter edit here, and would leave a GL context and a window
    behind in a process whose whole point is not to have either."""
    return len(FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor"))


def build():
    try:
        import Part

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        doc = FreeCAD.newDocument(DOC, hidden=True)
        state["doc"] = doc
        sketch = doc.addObject("Sketcher::SketchObject", OBJ)
        sketch.addGeometry(Part.LineSegment(FreeCAD.Vector(-5, 0, 0),
                                            FreeCAD.Vector(5, 0, 0)), False)
        doc.recompute()
        state["children_before"] = root_children()

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
    # Sampled as the conversation runs: the end state alone would show
    # only the last step, and every claim here is about a transition.
    state["seen"].append((in_edit(), root_children(), views_3d()))
    if client.is_alive():
        if clock() - state["t0"] > CLIENT_WAIT_S:
            check("the client finished", False,
                  "still talking after %ds" % CLIENT_WAIT_S)
            finish()
            return
        QtCore.QTimer.singleShot(20, poll)
        return
    # The connection is gone; the close handler runs on the GUI thread,
    # so give it a turn before reading what it left behind.
    QtCore.QTimer.singleShot(500, verify)


def verify():
    client = state["client"]
    try:
        check("the client ran without error", client.error is None, client.error or "")
        check("the hello was answered with a snapshot", client.snapshot)

        refused = client.edit_without_camera or {}
        check("an edit before any camera is refused",
              refused.get("ok") is False and refused.get("code") == "NoView",
              refused)

        accepted = client.edit_with_camera or {}
        check("an edit with a camera stated is accepted",
              accepted.get("ok") is True, accepted)

        before = state["children_before"]
        seen = state["seen"]
        check("the sketch had a scene graph to begin with", before > 0, before)
        check("the document entered edit", any(s[0] for s in seen),
              [s[0] for s in seen])
        check("entering edit emptied the view provider's root",
              any(s[0] and s[1] == 0 for s in seen), seen[:40])
        check("the session bound to the mirror rather than making a 3D view",
              all(s[2] == 0 for s in seen),
              sorted({s[2] for s in seen}))

        check("a replayed input event was answered with a push",
              client.input_pushed is True, "pushed: %s" % client.input_pushed)

        reset = client.reset or {}
        check("resetEdit is accepted", reset.get("ok") is True, reset)
        check("leaving edit gave the children back",
              any(not s[0] and s[1] >= before for s in seen[1:]), seen[-40:])

        # The last word: the client dropped while still in edit. Its
        # mirror is destroyed by the close handler, and the document must
        # not be left bound to it.
        check("the dropped client left no edit session behind", not in_edit(),
              "still in edit")
        check("and the view provider has its graph", root_children() >= before,
              (root_children(), before))
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
