"""A served document's scene follows the claims its objects make.

A 3D view keeps an object at its top level only while no other object
claims it (Gui::Document::handleChildren3D -> toggleViewProvider). A
served document with no view has its own root, and that root must obey
the same rule when the claim changes WHILE it is served -- an object
created and then put into a group, or taken out of one -- or the object
draws twice: once where its parent puts it, once at the top level where
no parent transform applies.

What is asserted, over a real socket against a headless serve:

  - a Box put into an App::Part that sits at x = 100 is picked at x = 100
    through the Part (obj Part, sub Box.Face6);
  - nothing is picked at the origin, where a top-level copy of the Box
    would sit with the Part's placement missing;
  - once the Box is taken back out of the Part, it is picked at the
    origin on its own (obj Box), and nothing is picked at x = 100.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt.
"""
import json
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
DOC = "ServeClaimChildren"
OFFSET = 100.0
CLIENT_WAIT_S = 90

# A camera over both places the box can be. A pick is resolved through
# the client's camera (the ray projected to a pixel, then picked from the
# eye), so each ray leaves the eye for the centre of the box's top face.
EYE = (55.0, 5.0, 250.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 500.0
VW, VH = 800, 600
TOP_Z = 10.0


def ray_to(x, y):
    direction = (x - EYE[0], y - EYE[1], TOP_Z - EYE[2])
    length = math.sqrt(sum(c * c for c in direction))
    return EYE, tuple(c / length for c in direction)


AT_ORIGIN = ray_to(5.0, 5.0)
AT_OFFSET = ray_to(OFFSET + 5.0, 5.0)

state = {"doc": None, "client": None, "done": False, "t0": clock(), "phase": "start"}
claimed = threading.Event()
released = threading.Event()


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


class Client(threading.Thread):
    """The wire conversation, off the GUI thread. Each document change is
    made on the GUI thread and waited for before the picks that read it."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.joined = threading.Event()
        self.picked = threading.Event()
        self.told = {}

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()
        finally:
            self.joined.set()
            self.picked.set()

    def pick(self, ws, tag, ray):
        """The selection a REPLACE pick leaves this client with, or None
        when nothing was pushed. A pick that leaves the selection as it
        was pushes nothing; every pick below changes it or misses on an
        empty one."""
        mark = len(ws.pushes)
        ws.send(2, wsclient.pick_frame(*ray, 0))
        raw = ws.next_push("selection", 3.0, since=mark)
        items = []
        if raw is not None:
            items = [(i.get("obj"), i.get("sub"))
                     for i in json.loads(raw.decode("utf-8")).get("items", [])]
        self.told[tag] = items if raw is not None else None
        ws.drain(0.2)

    def talk(self):
        ws = WS(self.port)
        ws.hello("serve-claim-children")
        self.snapshot = ws.next_binary(20.0) is not None
        if not self.snapshot:
            return
        ws.next_binary(0.5)
        ws.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH))
        ws.drain(0.5)

        self.joined.set()
        claimed.wait(30.0)
        ws.drain(0.5)
        self.pick(ws, "claimed_offset", AT_OFFSET)
        self.pick(ws, "claimed_origin", AT_ORIGIN)

        self.picked.set()
        released.wait(30.0)
        ws.drain(0.5)
        self.pick(ws, "released_origin", AT_ORIGIN)
        self.pick(ws, "released_offset", AT_OFFSET)
        ws.close()


def build():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        doc = FreeCAD.newDocument(DOC, hidden=True)
        state["doc"] = doc
        part = doc.addObject("App::Part", "Part")
        part.Placement.Base = FreeCAD.Vector(OFFSET, 0, 0)
        # Something to publish at the join: an empty part is no scene,
        # and a join with no scene gets no snapshot. Clear of every ray.
        anchor = doc.addObject("Part::Box", "Anchor")
        anchor.Placement.Base = FreeCAD.Vector(-OFFSET, 0, 0)
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


def claim():
    """Created at the top level, then claimed: the order a command that
    makes an object inside the active group goes through."""
    doc = state["doc"]
    box = doc.addObject("Part::Box", "Box")
    box.Length = box.Width = box.Height = 10
    doc.getObject("Part").addObject(box)
    doc.recompute()


def release():
    doc = state["doc"]
    doc.getObject("Part").removeObject(doc.getObject("Box"))
    doc.recompute()


def poll():
    client = state["client"]
    phase = state["phase"]
    try:
        if phase == "start" and client.joined.is_set():
            state["phase"] = "claimed"
            claim()
            claimed.set()
        elif phase == "claimed" and client.picked.is_set():
            state["phase"] = "released"
            release()
            released.set()
    except Exception:
        note("ABORT poll:\n" + traceback.format_exc())
        claimed.set()
        released.set()
        QtCore.QTimer.singleShot(200, verify)
        return
    if client.is_alive():
        if clock() - state["t0"] > CLIENT_WAIT_S:
            check("the client finished", False,
                  "still talking after %ds in phase %s" % (CLIENT_WAIT_S, phase))
            finish()
            return
        QtCore.QTimer.singleShot(20, poll)
        return
    QtCore.QTimer.singleShot(300, verify)


def hits_box(items):
    return [it for it in (items or []) if it[0] == "Box" or (it[1] or "").startswith("Box.")]


def verify():
    client = state["client"]
    try:
        check("the client ran without error", client.error is None, client.error or "")
        check("the hello was answered with a snapshot", client.snapshot)
        told = client.told

        check("the claimed box is picked through its part",
              told.get("claimed_offset") == [("Part", "Box.Face6")], repr(told.get("claimed_offset")))
        check("nothing of the claimed box is at the top level",
              not hits_box(told.get("claimed_origin")), repr(told.get("claimed_origin")))
        check("the released box is picked at the top level",
              told.get("released_origin") == [("Box", "Face6")], repr(told.get("released_origin")))
        check("nothing of the released box is left in its part",
              not hits_box(told.get("released_offset")), repr(told.get("released_offset")))
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    claimed.set()
    released.set()
    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(0, build)
