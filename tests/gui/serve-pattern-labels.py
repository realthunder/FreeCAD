"""A pattern's on-view spacing labels are clicked and typed into over the wire.

The served half of upstream 6fa9125919 on the fork's EditableDatumLabel
(docs/ThinClient.md sec 8.7). A pattern panel shows its extent, or each
gap, as a dimension in the view, and a click on one opens its entry box.
On a served view nothing about that is special: the dimension reaches the
client as scene, the click comes back as a replayed pointer event through
the scene, and the box that opens streams as the sketcher's do.

What is asserted:

  - editing a pattern from the client opens no entry box by itself: the
    labels are shown, not in edit, and a label that is not in edit is
    not in the feed;
  - a replayed click where the label's number is opens its box, found by
    walking a column between the occurrences, where nothing selectable
    lies -- so a click that opened one opened it by landing on the label,
    and the clicks before it, beside the label, opened nothing;
  - the box carries the label's point size, which the client sizes its
    own box by;
  - key frames type into it, and Enter commits: the pattern's Length is
    what was typed, and the box goes away.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set and this
directory on PYTHONPATH.
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
DOC = "ServePatternLabels"
CLIENT_WAIT_S = 120

# Straight down onto the gap between the two occurrences, whose centres
# are (5,5,5) and (105,5,5): the label runs between them, its number over
# x = 55 and some way towards +y, by the label's distance from its line.
EYE = (55.0, 5.0, 200.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 400.0
VW, VH = 800, 600

state = {"doc": None, "client": None, "done": False, "t0": clock()}


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


def last_push(ws, cmd, since, settle):
    """The newest push of \a cmd after \a since, once the wire has settled:
    the set is stated whole each time, so the last one is what is shown."""
    ws.drain(settle)
    for text in reversed(ws.pushes[since:]):
        if b'"cmd":"' + cmd.encode() + b'"' in text:
            return text
    return None


def key(ws, keysym, char, time_ms):
    for kind in (wsclient.KEY_DOWN, wsclient.KEY_UP):
        ws.send(2, wsclient.input_frame(kind, VW // 2, VH // 2, code=keysym,
                                        delta=char, time_ms=time_ms))
        time_ms += 10
    return time_ms


class Client(threading.Thread):
    """The whole wire conversation, off the GUI thread."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.edit = None
        self.on_entry = None
        self.misses = 0
        self.hit = None
        self.opened = None
        self.typed = None
        self.after_enter = None
        self.all_pushes = []

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()

    def talk(self):
        ws = WS(self.port)
        ws.hello("serve-pattern-labels")
        self.snapshot = ws.next_binary(20.0) is not None
        if not self.snapshot:
            return
        ws.next_binary(0.5)
        ws.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH))
        ws.drain(0.3)

        mark = len(ws.pushes)
        self.edit = parsed(ws.op('{"id":1,"op":"edit","obj":"LinearPattern","mode":0}'))
        if not (self.edit or {}).get("ok"):
            ws.close()
            return
        # The panel places its labels once the edit has started
        ws.drain(1.5)
        entry = ws.next_push("onview", 0.5, since=mark)
        self.on_entry = params_of(entry) if entry is not None else None

        # Up the column over the gap, a click at a time, until one opens
        # a box. Nothing selectable lies on this column.
        t = 1000
        for y in range(VH // 2, 60, -6):
            mark = len(ws.pushes)
            ws.send(2, wsclient.input_frame(wsclient.PRESS, VW // 2, y, code=1, time_ms=t))
            ws.send(2, wsclient.input_frame(wsclient.RELEASE, VW // 2, y, code=1,
                                            time_ms=t + 30))
            t += 100
            opened = params_of(ws.next_push("onview", 0.4, since=mark))
            if opened:
                self.hit = (VW // 2, y)
                self.opened = opened
                break
            self.misses += 1
        if not self.opened:
            self.all_pushes = [p[:200] for p in ws.pushes]
            ws.close()
            return

        # The number is selected when the box opens, so digits replace it
        mark = len(ws.pushes)
        for ch in "150":
            t = key(ws, ord(ch), ord(ch), t)
        self.typed = params_of(last_push(ws, "onview", mark, 0.8))

        mark = len(ws.pushes)
        t = key(ws, 0xff0d, 13, t)
        self.after_enter = params_of(last_push(ws, "onview", mark, 1.5))
        ws.op('{"id":2,"op":"resetEdit"}')
        ws.drain(0.5)
        self.all_pushes = [p[:200] for p in ws.pushes]
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
    try:
        check("the client ran without error", client.error is None, client.error or "")
        check("the hello was answered with a snapshot", client.snapshot)
        check("the pattern is edited from the client",
              (client.edit or {}).get("ok") is True, client.edit)
        check("its labels open no entry box by themselves",
              not client.on_entry, client.on_entry)
        check("a click on the label's number opens its box",
              bool(client.opened),
              client.opened if client.opened else client.all_pushes)
        if client.opened:
            check("and the clicks beside it did not", client.misses > 0,
                  "first hit at %s" % (client.hit,))
            box = client.opened[0]
            check("the box says the label's point size",
                  (box.get("pt") or 0) > 0, box)
            check("the box has the keys", box.get("focus") is True, box)
            typed = (client.typed or [{}])[0].get("text", "")
            check("key frames type into it", "150" in typed, client.typed)
            check("Enter commits what was typed",
                  abs(state["doc"].LinearPattern.Length.Value - 150.0) < 1e-6,
                  state["doc"].LinearPattern.Length)
            check("and the box goes away", client.after_enter == [], client.after_enter)
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
