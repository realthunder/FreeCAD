"""A served document with nothing to draw still answers its clients.

A served document is published by its serve source alone (acebbf772d), and
a publish was refused while the publisher had neither a draw nor an
overlay. A desktop view's renderer, which published before, always has an
overlay; the serve source of a document with nothing drawable in it -- a
new document, or one holding an empty sketch -- has none once the
navigation cube is off. So the first publish never happened, a client's
hello went unanswered, and the client waited: an empty document could not
be opened from a browser at all. The same refusal kept the last publish
standing when a document's last object was deleted: its clients went on
showing the object, and a client arriving afterwards was handed it.

One document, served empty, then given a box, then emptied again. Claims:
  - a client's hello to the empty document is answered with a snapshot;
  - adding the box is published (control);
  - a client arriving then is handed more than the empty scene (control);
  - deleting the box, the last object, is published;
  - a client arriving after that is handed the empty scene again.

Scored against the tree before the fix: the first, fourth and fifth fail.
"""
import os
import threading
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

from wsclient import WS, free_port

clock = time.perf_counter

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ServeEmpty"
CLIENT_WAIT_S = 120
ANSWER_S = 10.0

state = {"done": False, "t0": clock(), "want": None, "out": {}, "talked": False}
added = threading.Event()
deleted = threading.Event()


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def size(frame):
    return None if frame is None else len(frame)


def arrive(port, label):
    """What a client connecting now is handed first, as a byte count."""
    ws = WS(port)
    ws.hello(label)
    got = size(ws.next_binary(ANSWER_S))
    ws.close()
    return got


def conversation(port):
    """Off the GUI thread: the host publishes on its own."""
    out = state["out"]
    try:
        a = WS(port)
        a.hello("empty-a")
        out["a0"] = size(a.next_binary(ANSWER_S))
        a.drain(1.0)

        state["want"] = "add"
        added.wait(30.0)
        out["a_add"] = size(a.next_binary(ANSWER_S))
        a.drain(1.0)
        out["b0"] = arrive(port, "empty-b")

        state["want"] = "delete"
        deleted.wait(30.0)
        out["a_del"] = size(a.next_binary(ANSWER_S))
        a.drain(1.0)
        out["c0"] = arrive(port, "empty-c")
        a.close()
    except Exception:
        out["error"] = traceback.format_exc()
    state["talked"] = True


def build():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        # The cube is an overlay of the serve source's own: with it on the
        # source always has something to state.
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)

        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        doc.recompute()
        port = free_port()
        ok = FreeCADGui.serveDocument(doc, port)
        if not check("the empty document is served", ok, "port %d" % port):
            finish()
            return
        threading.Thread(target=conversation, args=(port,), daemon=True).start()
        QtCore.QTimer.singleShot(50, poll)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def poll():
    try:
        want, state["want"] = state["want"], None
        doc = state["doc"]
        if want == "add":
            box = doc.addObject("Part::Box", "Box")
            box.Length = box.Width = box.Height = 10
            doc.recompute()
            added.set()
        elif want == "delete":
            doc.removeObject("Box")
            doc.recompute()
            deleted.set()
    except Exception:
        note("ABORT poll:\n" + traceback.format_exc())
        finish()
        return
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
    check("the conversation ran without error", "error" not in out, out.get("error", ""))
    note("bytes handed over: %s" % out)
    a0, b0, c0 = out.get("a0"), out.get("b0"), out.get("c0")
    check("a hello to an empty document is answered with a snapshot", a0 is not None, a0)
    check("adding a box is published", out.get("a_add") is not None, out.get("a_add"))
    check("a client arriving then is handed more than the empty scene",
          b0 is not None and (a0 is None or b0 > a0), (a0, b0))
    check("deleting the last object is published", out.get("a_del") is not None,
          out.get("a_del"))
    check("a client arriving after that is handed the empty scene again",
          c0 is not None and b0 is not None and c0 < b0, (b0, c0))
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(0, build)
