"""One edit session, every view: the desktop's window and a client's mirror.

The oracle of docs/ThinClient.md 8.11 order item 1. A served document is
one session with N views, so an edit entered on the desktop is the edit a
browser is in, and the reverse. Before this, a desktop sketch edit was
invisible to every client (its geometry moved under one window's editing
root, hung under that window's aux root) and a browser's was invisible to
the desktop (under its mirror's root, in the served graph). Now the root
is the document's, every view of the document hangs the same node, and
every view's events reach the one tool.

So this is a document that has BOTH: a real 3D window under xvfb, and a
served connection with a mirror. Each side enters edit in turn, and the
readings are taken on the other side.

What is asserted, the desktop entering first:

  - the client that did nothing is TOLD: the `edit` push with editing
    true arrives on its connection, not only on an initiator's;
  - the sketch's graph moved under the editing root (the view provider's
    child count reads zero), and that root is in the desktop window's
    scene graph, with content;
  - a pointer move from the client is answered with a push -- the event
    was replayed in the client's view, which is in the session;
  - the desktop leaving is announced too, and the graph comes back.

Then the client entering:

  - the document reports the edit THROUGH THE DESKTOP WINDOW: getInEdit
    asks the active 3D view whether it is editing, and a window that
    joined answers yes. This is the reading that was None before;
  - no 3D view was created for it: the session bound to the mirror and
    the window joined, so the count stays at one throughout;
  - the editing root is in the desktop window's graph with the sketch's
    geometry under it, which is what "the desktop draws in it" comes to
    without a mouse: what its events would pick against is there;
  - the desktop's task panel is up for a session it did not start;
  - the client's resetEdit takes the desktop window out again: the root
    leaves its graph, the geometry goes back, getInEdit is None.

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
DOC = "ServeSharedEdit"
OBJ = "Sketch"
CLIENT_WAIT_S = 150

EYE = (0.0, 0.0, 60.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 200.0
VW, VH = 800, 600

state = {"doc": None, "client": None, "done": False, "t0": clock(),
         "samples": [], "phase": "start"}
# GUI thread -> client thread: the desktop did its part.
desktop_entered = threading.Event()
desktop_left = threading.Event()
desktop_sampled = threading.Event()


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
    """The wire conversation, off the GUI thread. Each phase waits for
    the desktop's event before reading what the desktop caused."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.ready = threading.Event()
        self.moved_in_desktop_edit = threading.Event()
        self.entered = threading.Event()
        self.told_entered = None
        self.told_left = None
        self.input_pushed_desktop = None
        self.edit_reply = None
        self.input_pushed_own = None
        self.reset = None

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()
            self.moved_in_desktop_edit.set()
            self.entered.set()

    def talk(self):
        ws = WS(self.port)
        ws.hello("serve-shared-edit")
        self.snapshot = ws.next_binary(20.0) is not None
        if not self.snapshot:
            self.ready.set()
            return
        ws.next_binary(0.5)
        ws.send(2, camera_frame())
        ws.drain(0.5)
        self.ready.set()

        # Phase A: the desktop enters. This client only watches.
        desktop_entered.wait(30.0)
        self.told_entered = ws.next_push("edit", 10.0)
        ws.drain(0.3)
        ws.send(2, wsclient.input_frame(wsclient.MOVE, VW // 2, VH // 2,
                                        time_ms=1000))
        self.input_pushed_desktop = ws.next_binary(5.0) is not None
        ws.drain(0.3)
        self.moved_in_desktop_edit.set()

        desktop_left.wait(30.0)
        self.told_left = ws.next_push("edit", 10.0, since=len(ws.pushes))
        ws.drain(0.5)

        # Phase B: this client enters; the desktop is expected to follow.
        self.edit_reply = ws.op('{"id":2,"op":"edit","obj":"%s","mode":0}' % OBJ)
        ws.drain(0.5)
        self.entered.set()
        desktop_sampled.wait(30.0)
        ws.send(2, wsclient.input_frame(wsclient.MOVE, VW // 2, VH // 2,
                                        time_ms=2000))
        self.input_pushed_own = ws.next_binary(5.0) is not None
        ws.drain(0.3)
        self.reset = ws.op('{"id":3,"op":"resetEdit"}')
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


def root_children():
    return state["doc"].getObject(OBJ).ViewObject.RootNode.getNumChildren()


def views_3d():
    return len(gdoc().mdiViewsOfType("Gui::View3DInventor"))


def desktop_edit_root():
    """(how many times, children of the first) the node named
    EditingRoot appears in the desktop window's scene graph. Idle, a
    window shows through a private root that is in no graph: zero. In
    a session the document's root is under its aux root: one, and its
    children are the transform plus the sketch's moved geometry."""
    from pivy import coin

    views = gdoc().mdiViewsOfType("Gui::View3DInventor")
    if not views:
        return (-1, -1)
    viewer = views[0].getViewer()
    graph = viewer.getSoRenderManager().getSceneGraph()
    search = coin.SoSearchAction()
    search.setName("EditingRoot")
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(graph)
    paths = search.getPaths()
    count = paths.getLength()
    children = paths[0].getTail().getNumChildren() if count else 0
    return (count, children)


def panel_up():
    try:
        return FreeCADGui.Control.activeDialog() is not None
    except Exception:
        return None


def sample():
    s = (state["phase"], in_edit(), root_children(), views_3d(),
         desktop_edit_root(), panel_up())
    state["samples"].append(s)
    return s


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
        # Not hidden: this document has a real 3D window, which is the
        # point.
        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        sketch = doc.addObject("Sketcher::SketchObject", OBJ)
        sketch.addGeometry(Part.LineSegment(FreeCAD.Vector(-5, 0, 0),
                                            FreeCAD.Vector(5, 0, 0)), False)
        doc.recompute()
        state["children_before"] = root_children()
        check("the document has one 3D window", views_3d() == 1, views_3d())

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
    sample()
    if client.error:
        QtCore.QTimer.singleShot(200, verify)
        return
    phase = state["phase"]
    try:
        if phase == "start" and client.ready.is_set():
            # A. The desktop enters.
            state["phase"] = "desktop-edit"
            opened = gdoc().setEdit(state["doc"].getObject(OBJ), 0)
            check("the desktop entered edit", opened, opened)
            QtCore.QTimer.singleShot(400, lambda: desktop_entered.set())
        elif phase == "desktop-edit" and client.moved_in_desktop_edit.is_set():
            state["phase"] = "desktop-leave"
            gdoc().resetEdit()
            QtCore.QTimer.singleShot(400, lambda: desktop_left.set())
        elif phase == "desktop-leave" and client.entered.is_set():
            # B. The client entered; the desktop window should have
            # joined by the time the op was answered.
            state["phase"] = "client-edit"
            QtCore.QTimer.singleShot(400, lambda: (sample(), desktop_sampled.set()))
        elif phase == "client-edit" and not client.is_alive():
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
        before = state["children_before"]
        samples = state["samples"]
        a = [s for s in samples if s[0] == "desktop-edit"]
        b = [s for s in samples if s[0] == "client-edit"]
        idle = [s for s in samples if s[0] in ("start", "end", "desktop-leave")]

        # A. The desktop's session, read from the client and the window.
        told = client.told_entered or b""
        check("a watching client is told the desktop's edit began",
              b'"editing":true' in told, told[:120])
        check("the desktop's edit moved the sketch under the editing root",
              any(s[1] and s[2] == 0 for s in a), [(s[1], s[2]) for s in a[:10]])
        check("the editing root is in the desktop window's graph, with content",
              any(s[4][0] == 1 and s[4][1] > 1 for s in a), [s[4] for s in a[:10]])
        check("a pointer move from the client is replayed into the desktop's session",
              client.input_pushed_desktop is True,
              "pushed: %s" % client.input_pushed_desktop)
        told = client.told_left or b""
        check("the client is told the desktop's edit ended",
              b'"editing":false' in told, told[:120])

        # B. The client's session, read from the window.
        reply = reply_of(client.edit_reply)
        check("the client's edit is accepted", reply.get("ok") is True, reply)
        check("the window reports the client's edit through its own viewer",
              any(s[1] for s in b), [s[1] for s in b[:10]])
        check("no 3D view was created for the client's session",
              all(s[3] == 1 for s in samples), sorted({s[3] for s in samples}))
        check("the client's edit put the editing root in the window's graph, with content",
              any(s[4][0] == 1 and s[4][1] > 1 for s in b), [s[4] for s in b[:10]])
        check("and moved the sketch under it",
              any(s[2] == 0 for s in b), [s[2] for s in b[:10]])
        panels = [s[5] for s in b]
        if any(p is None for p in panels):
            note("INFO the task panel state is not readable from Python here")
        else:
            check("the desktop's task panel is up for the client's session",
                  any(panels), panels[:10])
        check("a pointer move from the initiating client is answered",
              client.input_pushed_own is True, "pushed: %s" % client.input_pushed_own)
        reset = reply_of(client.reset)
        check("the client's resetEdit is accepted", reset.get("ok") is True, reset)

        # Idle again: the root is out of the window's graph, and the
        # geometry is back.
        check("the window is out of the session afterwards", not in_edit(), "still in edit")
        check("the editing root left the window's graph",
              desktop_edit_root()[0] == 0, desktop_edit_root())
        check("and the view provider has its graph", root_children() >= before,
              (root_children(), before))
        check("the root was never in the window's graph while idle",
              all(s[4][0] == 0 for s in idle if not s[1]),
              [s[4] for s in idle if not s[1]][:10])
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    desktop_entered.set()
    desktop_left.set()
    desktop_sampled.set()
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
