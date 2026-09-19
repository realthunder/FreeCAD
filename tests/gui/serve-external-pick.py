"""The External and CarbonCopy pick from a browser (docs/ThinClient.md
8.11 item 3).

Both tools work by the view's own selection: the unified selection root
picks the OTHER object under the pointer, preselects it through the
tool's gate, and on release selects it into the session's instance,
where the tool's observer turns the selection into geometry. A served
document's root has no viewer, so until item 3 it could not pick, and a
browser's click in either tool selected nothing. Now the pick resolves
in the client's own mirror -- its camera, its pick radius -- ahead of
that client's edit callback (the desktop's order), and lands where the
tool listens.

A document with a real 3D window under xvfb AND a served connection
with a mirror, as serve-shared-edit.py has, a box beside the sketch,
and a second sketch on the same plane.

What is asserted, the desktop entering first:

  - the client starts Sketcher_External through the command op, which
    admits it now (it activates a handler and opens nothing);
  - a move onto the box's top edge, a press and a release from the
    client add ONE external geometry to the sketch, referring to the
    box -- read on the desktop, from the sketch itself;
  - a command outside the list is still refused.

Then the client entering:

  - Sketcher_CarbonCopy from the client, a click on the other sketch's
    line, and the sketch being edited gains one geometry;
  - the client's resetEdit takes the desktop window out again.

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
DOC = "ServeExternalPick"
OBJ = "Sketch"
OTHER = "Sketch2"
BOX = "Box"
CLIENT_WAIT_S = 150

EYE = (0.0, 0.0, 60.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 200.0
VW, VH = 800, 600

state = {"doc": None, "client": None, "done": False, "t0": clock(),
         "phase": "start", "ext_before": None, "ext_after": None,
         "geo_before": None, "geo_after": None, "in_edit": []}
# GUI thread -> client thread: the desktop did its part.
desktop_entered = threading.Event()
external_sampled = threading.Event()
desktop_left = threading.Event()
carbon_sampled = threading.Event()


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
        self.clicked_external = threading.Event()
        self.clicked_carbon = threading.Event()
        self.told_entered = None
        self.external_reply = None
        self.refused_reply = None
        self.pushed_after_external = None
        self.told_left = None
        self.edit_reply = None
        self.carbon_reply = None
        self.pushed_after_carbon = None
        self.reset = None

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()
            self.clicked_external.set()
            self.clicked_carbon.set()

    def talk(self):
        ws = WS(self.port)
        ws.hello("serve-external-pick")
        self.snapshot = ws.next_binary(20.0) is not None
        if not self.snapshot:
            self.ready.set()
            return
        ws.next_binary(0.5)
        ws.send(2, camera_frame())
        ws.drain(0.5)
        self.ready.set()

        # Phase A: the desktop enters; this client runs the External tool
        # in the desktop's session and clicks the box's top edge. The
        # pixel is a hair OUTSIDE the top face, so the only thing within
        # the pick radius is the edge at x = 10, z = 10.
        desktop_entered.wait(30.0)
        self.told_entered = ws.next_push("edit", 10.0)
        ws.drain(0.3)
        self.refused_reply = ws.op('{"id":2,"op":"command","name":"Sketcher_Detach"}')
        self.external_reply = ws.op('{"id":3,"op":"command","name":"Sketcher_External"}')
        ws.drain(0.5)
        px, py = pixel_of(10.2, 5.0, 10.0)
        click_at(ws, px, py, 1000)
        self.pushed_after_external = ws.next_binary(5.0) is not None
        ws.drain(0.5)
        self.clicked_external.set()

        external_sampled.wait(30.0)
        desktop_left.wait(30.0)
        self.told_left = ws.next_push("edit", 10.0, since=len(ws.pushes))
        ws.drain(0.5)

        # Phase B: this client enters and carbon-copies the other sketch's
        # line into the one it is editing.
        self.edit_reply = ws.op('{"id":4,"op":"edit","obj":"%s","mode":0}' % OBJ)
        ws.drain(0.5)
        self.carbon_reply = ws.op('{"id":5,"op":"command","name":"Sketcher_CarbonCopy"}')
        ws.drain(0.5)
        px, py = pixel_of(-5.0, -4.0, 0.0)
        click_at(ws, px, py, 2000)
        self.pushed_after_carbon = ws.next_binary(5.0) is not None
        ws.drain(0.5)
        self.clicked_carbon.set()

        carbon_sampled.wait(30.0)
        self.reset = ws.op('{"id":6,"op":"resetEdit"}')
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
        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        box = doc.addObject("Part::Box", BOX)
        sk = doc.addObject("Sketcher::SketchObject", OBJ)
        sk.addGeometry(Part.LineSegment(FreeCAD.Vector(-8, -8, 0),
                                        FreeCAD.Vector(-2, -8, 0)), False)
        other = doc.addObject("Sketcher::SketchObject", OTHER)
        other.addGeometry(Part.LineSegment(FreeCAD.Vector(-8, -4, 0),
                                           FreeCAD.Vector(-2, -4, 0)), False)
        doc.recompute()
        state["ext_before"] = external_geometry()
        state["geo_before"] = len(sk.Geometry)
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
            QtCore.QTimer.singleShot(400, lambda: desktop_entered.set())
        elif phase == "desktop-edit" and client.clicked_external.is_set():
            state["phase"] = "external-sampled"

            def sample_external():
                state["ext_after"] = external_geometry()
                state["in_edit"].append(in_edit())
                external_sampled.set()
                gdoc().resetEdit()
                QtCore.QTimer.singleShot(400, lambda: desktop_left.set())

            QtCore.QTimer.singleShot(400, sample_external)
        elif phase == "external-sampled" and client.clicked_carbon.is_set():
            state["phase"] = "carbon-sampled"

            def sample_carbon():
                state["geo_after"] = len(sketch().Geometry)
                state["in_edit"].append(in_edit())
                carbon_sampled.set()

            QtCore.QTimer.singleShot(400, sample_carbon)
        elif phase == "carbon-sampled" and not client.is_alive():
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

        # A. The External tool in the desktop's session.
        told = client.told_entered or b""
        check("the client is told the desktop's edit began",
              b'"editing":true' in told, told[:120])
        refused = reply_of(client.refused_reply)
        check("a command outside the list is still refused",
              refused.get("ok") is False and refused.get("code") == "CommandRefused",
              refused)
        reply = reply_of(client.external_reply)
        check("Sketcher_External is admitted by the command op",
              reply.get("ok") is True, reply)
        check("the click was answered with a scene push",
              client.pushed_after_external is True, client.pushed_after_external)
        check("the sketch had no external geometry before",
              state["ext_before"] == [], state["ext_before"])
        after = state["ext_after"]
        check("the client's click added one external geometry, the box's edge",
              after is not None and len(after) == 1 and after[0][0] == BOX
              and len(after[0][1]) == 1 and after[0][1][0].startswith("Edge"),
              after)
        check("the desktop was still in edit when it was read",
              state["in_edit"][:1] == [True], state["in_edit"])
        told = client.told_left or b""
        check("the client is told the desktop's edit ended",
              b'"editing":false' in told, told[:120])

        # B. CarbonCopy in the client's session.
        reply = reply_of(client.edit_reply)
        check("the client's edit is accepted", reply.get("ok") is True, reply)
        reply = reply_of(client.carbon_reply)
        check("Sketcher_CarbonCopy is admitted by the command op",
              reply.get("ok") is True, reply)
        check("the carbon copy click was answered with a scene push",
              client.pushed_after_carbon is True, client.pushed_after_carbon)
        check("the client's click carbon-copied the other sketch's line",
              state["geo_after"] == state["geo_before"] + 1,
              (state["geo_before"], state["geo_after"]))
        check("the window was in the client's session when it was read",
              state["in_edit"][1:2] == [True], state["in_edit"])
        reset = reply_of(client.reset)
        check("the client's resetEdit is accepted", reset.get("ok") is True, reset)
        check("the window is out of the session afterwards", not in_edit(), "still in edit")
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    desktop_entered.set()
    external_sampled.set()
    desktop_left.set()
    carbon_sampled.set()
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
