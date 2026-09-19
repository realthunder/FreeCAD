"""Undo and redo are two control ops on the served document.

docs/ThinClient.md 8.11 order item 2, the undo half. A served document is
one session with N views and one undo stack, so a client's undo is the
desktop's Ctrl+Z: Gui::Document::undo, the room's selection cleared as
it is there, and -- because a client's view-mode selection is its own
instance (item 1) -- every client's instance cleared too, and each told
through its selection push.

What is asserted, over a real socket against a headless serve:

  - a pick on the box's top face is told back to the client (the
    baseline: it holds a selection to lose);
  - `steps` below one is refused (BadRequest), a redo with nothing to
    redo is refused (NothingToRedo), neither touching the document;
  - `undo` answers ok with the stacks after it, by transaction name:
    the undone step now under redos, nothing under undos;
  - the client is told its selection is empty, and on the GUI thread
    the box's Length is back to what it was and the room is empty;
  - `redo` answers ok with the step back under undos, and the Length is
    forward again;
  - an undo deeper than the stack is refused (NothingToUndo).

What is NOT read here: the grouped-transaction refusal, which needs two
documents sharing a transaction id with another step in front; the
unit of that is Gui::Document::undoRedoWouldPrompt, and the op refuses
on its answer.

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
DOC = "ServeUndoRedo"
OBJ = "Box"
CLIENT_WAIT_S = 90

# Straight down onto the box's top face (the box is 10 x 10 x 10 at the
# origin, stretched to 20 along x by the transaction).
EYE = (5.0, 5.0, 60.0)
DOWN = (0.0, 0.0, -1.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 200.0
VW, VH = 800, 600

state = {"doc": None, "client": None, "done": False, "t0": clock(),
         "samples": {}, "phase": "start"}
undone_sampled = threading.Event()
redone_sampled = threading.Event()


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def camera_frame():
    return wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH)


class Client(threading.Thread):
    """The wire conversation, off the GUI thread. The two samples the
    GUI thread takes are waited for, so a reply never races the
    document state it changed."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.undone = threading.Event()
        self.redone = threading.Event()
        self.told_pick = None
        self.bad_steps = None
        self.nothing_redo = None
        self.undo_reply = None
        self.told_clear = None
        self.redo_reply = None
        self.too_many = None

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()
            self.undone.set()
            self.redone.set()

    def talk(self):
        ws = WS(self.port)
        ws.hello("serve-undo-redo")
        self.snapshot = ws.next_binary(20.0) is not None
        if not self.snapshot:
            return
        ws.next_binary(0.5)
        ws.send(2, camera_frame())
        ws.drain(0.5)

        # The baseline: a selection of this client's own to lose.
        ws.send(2, wsclient.pick_frame(EYE, DOWN, 0))
        self.told_pick = ws.next_push("selection", 5.0)
        ws.drain(0.3)

        # Two refusals that touch nothing.
        self.bad_steps = ws.op('{"id":2,"op":"undo","steps":0}')
        self.nothing_redo = ws.op('{"id":3,"op":"redo"}')

        # The undo. The clear is pushed on the publish timer, which may
        # land before or after the reply: the mark is taken before the
        # op so next_push finds it either way.
        mark = len(ws.pushes)
        self.undo_reply = ws.op('{"id":4,"op":"undo"}')
        self.told_clear = ws.next_push("selection", 5.0, since=mark)
        ws.drain(0.5)
        self.undone.set()
        undone_sampled.wait(30.0)

        self.redo_reply = ws.op('{"id":5,"op":"redo"}')
        ws.drain(0.5)
        self.redone.set()
        redone_sampled.wait(30.0)

        self.too_many = ws.op('{"id":6,"op":"undo","steps":5}')
        ws.drain(0.3)
        ws.close()


def reply_of(raw):
    if raw is None:
        return {}
    try:
        return json.loads(raw.decode("utf-8"))
    except Exception:
        return {}


def items_of(raw):
    return [(i.get("obj"), i.get("sub")) for i in reply_of(raw).get("items", [])]


def sample(tag):
    box = state["doc"].getObject(OBJ)
    state["samples"][tag] = {
        "length": float(box.Length) if box else None,
        "room": len(FreeCADGui.Selection.getSelectionEx(DOC)),
        "undos": list(FreeCAD.getDocument(DOC).UndoNames),
        "redos": list(FreeCAD.getDocument(DOC).RedoNames),
    }


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
        doc.UndoMode = 1
        box = doc.addObject("Part::Box", OBJ)
        box.Length = box.Width = box.Height = 10
        doc.recompute()
        # The one step on the stack, recomputed inside the transaction as
        # a command's would be, so the shape travels with the length.
        doc.openTransaction("Stretch")
        box.Length = 20
        doc.recompute()
        doc.commitTransaction()
        check("the transaction is on the stack", list(doc.UndoNames) == ["Stretch"],
              list(doc.UndoNames))

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
    if client.error:
        QtCore.QTimer.singleShot(200, verify)
        return
    phase = state["phase"]
    try:
        if phase == "start" and client.undone.is_set():
            state["phase"] = "undone"
            sample("undone")
            undone_sampled.set()
        elif phase == "undone" and client.redone.is_set():
            state["phase"] = "redone"
            sample("redone")
            redone_sampled.set()
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
    QtCore.QTimer.singleShot(300, verify)


def verify():
    client = state["client"]
    try:
        check("the client ran without error", client.error is None, client.error or "")
        check("the hello was answered with a snapshot", client.snapshot)
        check("the pick was told back to the client",
              items_of(client.told_pick) == [(OBJ, "Face6")], items_of(client.told_pick))

        bad = reply_of(client.bad_steps)
        check("steps below one is refused", bad.get("ok") is False
              and bad.get("code") == "BadRequest", bad)
        nothing = reply_of(client.nothing_redo)
        check("a redo with nothing to redo is refused", nothing.get("ok") is False
              and nothing.get("code") == "NothingToRedo", nothing)

        undo = reply_of(client.undo_reply)
        check("the undo is answered ok with the stacks after it",
              undo.get("ok") is True and undo.get("undos") == []
              and undo.get("redos") == ["Stretch"], undo)
        check("the client is told its selection is empty",
              client.told_clear is not None and items_of(client.told_clear) == [],
              items_of(client.told_clear) if client.told_clear else None)
        undone = state["samples"].get("undone", {})
        check("the length is back after the undo", undone.get("length") == 10.0, undone)
        check("the room is empty after the undo", undone.get("room") == 0, undone)
        check("the document's stacks agree with the reply",
              undone.get("undos") == [] and undone.get("redos") == ["Stretch"], undone)

        redo = reply_of(client.redo_reply)
        check("the redo is answered ok with the step back under undos",
              redo.get("ok") is True and redo.get("undos") == ["Stretch"]
              and redo.get("redos") == [], redo)
        redone = state["samples"].get("redone", {})
        check("the length is forward again after the redo",
              redone.get("length") == 20.0, redone)

        many = reply_of(client.too_many)
        check("an undo deeper than the stack is refused", many.get("ok") is False
              and many.get("code") == "NothingToUndo", many)
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    undone_sampled.set()
    redone_sampled.set()
    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(0, build)
