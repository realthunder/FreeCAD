"""A pattern's instance toggles are clicked over the wire.

The served half of upstream e22e537c4b on the fork's marker node
(Gui::SoToggleMarker, Gui::PatternInstanceMarkers). A pattern panel shows
a toggle at the middle of each instance; on a served view the toggle is
scene like any other, and a click on it comes back as a replayed pointer
event through the scene, picked over the instance it sits in.

What is asserted:

  - a replayed click where the second instance's marker is leaves that
    instance out: SuppressedIndices is [1];
  - a second click there, on the marker now showing a plus, brings it
    back -- so the markers were shown again where they were, in their new
    state, after the first;
  - a click on the first instance's marker leaves the first one out;
  - while the client edits, the markers hang under the session's on-view
    root (EditingRoot::onViewNode, "EditingOnViewRoot"), which the serving
    source publishes as the session's tagged overlay, not in the served
    root every client shares (docs/ThinClient.md 8.12 item J); once the
    session is over nothing is left there.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set and this
directory on PYTHONPATH.
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
DOC = "ServePatternMarkers"
CLIENT_WAIT_S = 120

# Straight down onto the pattern, whose instances' centres are (5,5,5)
# and (105,5,5): each is 50 to a side of the eye, 195 below it.
EYE = (55.0, 5.0, 200.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 400.0
VW, VH = 800, 600


def canvas_x(x, depth=195.0):
    return int(round(VW / 2 + (x - EYE[0]) / (depth * math.tan(HEIGHT_ANGLE / 2)) * VH / 2))


state = {"doc": None, "client": None, "done": False, "t0": clock(), "seen": []}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def session_markers():
    """SoToggleMarker nodes under the session's on-view root, -1 when
    there is no such root."""
    from pivy import coin
    node = coin.SoNode.getByName("EditingOnViewRoot")
    if node is None:
        return -1
    sa = coin.SoSearchAction()
    sa.setType(coin.SoType.fromName("SoToggleMarker"))
    sa.setInterest(coin.SoSearchAction.ALL)
    sa.setSearchingAll(True)
    sa.apply(node)
    return sa.getPaths().getLength()


class Client(threading.Thread):
    """The whole wire conversation, off the GUI thread. After each click it
    waits for the GUI thread to read the pattern."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.edit = None
        self.clicks = 0
        self.read = threading.Event()

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()

    def click(self, ws, x, y, t):
        self.read.clear()
        ws.send(2, wsclient.input_frame(wsclient.PRESS, x, y, code=1, time_ms=t))
        ws.send(2, wsclient.input_frame(wsclient.RELEASE, x, y, code=1, time_ms=t + 30))
        # The panel recomputes, and shows its markers again
        ws.drain(1.5)
        self.clicks += 1
        self.read.wait(30.0)

    def talk(self):
        ws = WS(self.port)
        ws.hello("serve-pattern-markers")
        self.snapshot = ws.next_binary(20.0) is not None
        if not self.snapshot:
            return
        ws.next_binary(0.5)
        ws.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH))
        ws.drain(0.3)

        import json
        raw = ws.op('{"id":1,"op":"edit","obj":"LinearPattern","mode":0}')
        self.edit = json.loads(raw.decode("utf-8")) if raw else None
        if not (self.edit or {}).get("ok"):
            ws.close()
            return
        # The panel places its markers once the edit has started
        ws.drain(1.5)

        self.click(ws, canvas_x(105.0), VH // 2, 1000)
        self.click(ws, canvas_x(105.0), VH // 2, 2000)
        self.click(ws, canvas_x(5.0), VH // 2, 3000)
        ws.op('{"id":2,"op":"resetEdit"}')
        ws.drain(0.5)
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
        body = doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = 10
        box.Width = 10
        box.Height = 10
        pattern = body.newObject("PartDesign::LinearPattern", "LinearPattern")
        pattern.Originals = [box]
        pattern.Direction = (doc.getObject("X_Axis"), [""])
        pattern.Length = 100
        pattern.Occurrences = 2
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


def poll():
    client = state["client"]
    if client.clicks > len(state["seen"]):
        if not state["seen"]:
            state["markers_in_session"] = session_markers()
        state["seen"].append(list(state["doc"].LinearPattern.SuppressedIndices))
        client.read.set()
    if client.is_alive():
        if clock() - state["t0"] > CLIENT_WAIT_S:
            check("the client finished", False, "still talking after %ds" % CLIENT_WAIT_S)
            finish()
            return
        QtCore.QTimer.singleShot(20, poll)
        return
    QtCore.QTimer.singleShot(300, verify)


def verify():
    client = state["client"]
    seen = state["seen"]
    try:
        check("the client ran without error", client.error is None, client.error or "")
        check("the hello was answered with a snapshot", client.snapshot)
        check("the pattern is edited from the client",
              (client.edit or {}).get("ok") is True, client.edit)
        check("all three clicks were read", len(seen) == 3, seen)
        check("a click on the second instance's marker leaves it out",
              len(seen) > 0 and seen[0] == [1], seen)
        check("a second click there brings it back",
              len(seen) > 1 and seen[1] == [], seen)
        check("a click on the first instance's marker leaves the first out",
              len(seen) > 2 and seen[2] == [0], seen)
        during = state.get("markers_in_session")
        check("the markers hang under the session's on-view root",
              during is not None and during >= 2, during)
        after = session_markers()
        check("nothing is left there once the session is over", after == 0, after)
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
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, build)
