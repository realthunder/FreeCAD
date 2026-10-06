"""A task dialog belongs to the view it was opened for.

Milestone 1 of docs/TaskPanelPerView.md: the owner and the keyed Control,
with nothing visible changed -- the task view still shows the one dialog.
What is asserted is what a dialog REPORTS as its view and what the keyed
entry points answer for a view that is not its own.

Two documents: A with a box and two 3D views (a1, a2), B with a body and
a pad and one view (b1), which is also served to one client.

Asserted on the desktop:

  - a dialog shown while a1 is active reports a1, and goes on reporting
    it when a2 is activated; asked for a2 there is none, asked for nobody
    in particular there is one (one dialog at a time: THE dialog);
  - closing a2's dialog, or document B's, closes nothing; a second show
    is refused; closing a1's closes it;
  - a show that names a view, or a document (upstream's attachTo), is
    that view's whichever is active;
  - a DEFERRED show that carries the owner it was asked under reports
    that view although another is active when it runs; one that carries
    none reports the view active by then (the hazard, as a control);
  - a command's dialog (Std_Placement) reports the view it was run in;
  - an edit of B's pad started while a1 is active reports b1.

And served: a client's edit of the pad opens a dialog that belongs to
that client's view -- no view of the main window, not b1's -- and the
desktop's own edit, beside the client, is b1's.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set and this
directory on PYTHONPATH.

Scored against the tree before the change (0259b90df5), where Control
takes no view: every claim that names one fails there on the missing
keyword or method.
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
from PySide import QtCore, QtGui

import wsclient
from wsclient import WS, free_port

clock = time.perf_counter

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC_A = "TaskOwnerA"
DOC_B = "TaskOwnerB"
CLIENT_WAIT_S = 120
V = FreeCAD.Vector

EYE = (5.0, 5.0, 120.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 400.0
VW, VH = 800, 600

VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
Control = FreeCADGui.Control

state = {"done": False, "talk": None, "t0": clock(), "views": {}, "samples": {}}
steps = []
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


def claim(name, fn):
    """A check whose reading may raise: before the change most of them do."""
    try:
        got = fn()
    except Exception as e:
        return check(name, False, "%s: %s" % (type(e).__name__, e))
    if isinstance(got, tuple):
        return check(name, got[0], got[1])
    return check(name, bool(got))


def settle(ms=200):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


def activate(view):
    FreeCADGui.getMainWindow().setActiveWindow(view)
    settle(100)


def views3d(name):
    return FreeCADGui.getDocument(name).mdiViewsOfType("Gui::View3DInventor")


def same(a, b):
    return a is not None and b is not None and (a is b or a == b)


def which(view):
    """The name this test knows a view by."""
    for name, known in state["views"].items():
        if same(view, known):
            return name
    return "none" if view is None else "unknown"


def owner():
    """(kind, view name) of the open dialog, ("no dialog", "") without one."""
    dlg = Control.activeTaskDialog()
    if dlg is None:
        return ("no dialog", "")
    return (dlg.getOwnerKind(), which(dlg.getAssociatedView()))


def owner_or_error():
    """owner() for a check's detail, where a raise must not end the step."""
    try:
        return owner()
    except Exception as e:
        return "%s: %s" % (type(e).__name__, e)


def owned_by(name):
    """A claim's reading: the open dialog belongs to the view `name`."""
    got = owner()
    return (got == ("view", name), got)


class Panel:
    def __init__(self, title):
        self.form = QtGui.QWidget()
        self.form.setWindowTitle(title)

    def accept(self):
        return True

    def reject(self):
        return True


def close_any():
    """Leave no dialog behind, whatever the step before did."""
    if Control.activeDialog():
        Control.closeDialog()


def step(fn):
    steps.append(fn)
    return fn


# ---- the desktop -----------------------------------------------------------


@step
def build():
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("AutoSaveTimeout", 0)
    VIEW.SetInt("RenderCache", 3)
    VIEW.SetBool("PerViewEdit", True)
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
        "Type", "bgfx - OpenGL"
    )

    b = FreeCAD.newDocument(DOC_B)
    body = b.addObject("PartDesign::Body", "Body")
    box = body.newObject("PartDesign::AdditiveBox", "Box")
    box.Length = box.Width = box.Height = 10
    sketch = body.newObject("Sketcher::SketchObject", "Sketch")
    sketch.Support = (b.getObject("XY_Plane"), [""])
    sketch.MapMode = "FlatFace"
    corners = [V(3, 3, 0), V(7, 3, 0), V(7, 7, 0), V(3, 7, 0)]
    for i in range(4):
        sketch.addGeometry(Part.LineSegment(corners[i], corners[(i + 1) % 4]))
    pad = body.newObject("PartDesign::Pad", "Pad")
    pad.Profile = sketch
    pad.Length = 20
    b.recompute()
    box.Visibility = False
    sketch.Visibility = False

    a = FreeCAD.newDocument(DOC_A)
    a.addObject("Part::Box", "Box")
    a.recompute()
    settle()
    activate(views3d(DOC_A)[0])
    FreeCADGui.runCommand("Std_ViewCreate")
    settle()


@step
def name_views():
    va, vb = views3d(DOC_A), views3d(DOC_B)
    if len(va) != 2 or len(vb) != 1:
        note("ABORT views: %d of A, %d of B" % (len(va), len(vb)))
        del steps[:]
        return
    state["views"] = {"a1": va[0], "a2": va[1], "b1": vb[0]}
    check("the views are told apart", not same(va[0], va[1]) and not same(va[0], vb[0]))
    activate(va[0])
    Control.showDialog(Panel("one"))


@step
def shown_in_a1():
    a1, a2 = state["views"]["a1"], state["views"]["a2"]
    doc_a, doc_b = FreeCAD.getDocument(DOC_A), FreeCAD.getDocument(DOC_B)
    claim("a dialog shown while a1 is active is a1's", lambda: owned_by("a1"))
    activate(a2)
    claim("it is still a1's when a2 is activated", lambda: owned_by("a1"))
    check("asked for nobody in particular there is a dialog", Control.activeDialog())
    claim("asked for a1 there is one", lambda: Control.activeDialog(view=a1))
    claim("asked for a2 there is none", lambda: not Control.activeDialog(view=a2))
    claim("a2 has no task dialog object", lambda: Control.activeTaskDialog(view=a2) is None)
    claim("asked for document A there is one", lambda: Control.activeDialog(attachTo=doc_a))
    claim(
        "asked for A's Gui document there is one",
        lambda: Control.activeDialog(attachTo=FreeCADGui.getDocument(DOC_A)),
    )
    claim("asked for document B there is none", lambda: not Control.activeDialog(attachTo=doc_b))

    def closes_nothing(**kw):
        Control.closeDialog(**kw)
        settle(50)
        return bool(Control.activeDialog())

    claim("closing a2's dialog closes nothing", lambda: closes_nothing(view=a2))
    claim("closing document B's closes nothing", lambda: closes_nothing(attachTo=doc_b))

    def refused():
        try:
            Control.showDialog(Panel("two"), view=a2)
        except RuntimeError:
            return (owner() == ("view", "a1"), owner())
        return (False, "shown")

    claim("a second dialog, for a2, is refused", refused)

    def closes():
        Control.closeDialog(view=a1)
        settle(50)
        return not Control.activeDialog()

    claim("closing a1's closes it", closes)
    close_any()


@step
def show_for_a_named_view():
    activate(state["views"]["a1"])
    claim(
        "a show that names a2 while a1 is active is a2's",
        lambda: (Control.showDialog(Panel("named"), view=state["views"]["a2"]), owned_by("a2"))[1],
    )


@step
def show_for_a_document():
    close_any()
    activate(state["views"]["a1"])
    claim(
        "a show attached to document B while a1 is active is b1's",
        lambda: (
            Control.showDialog(Panel("attached"), attachTo=FreeCAD.getDocument(DOC_B)),
            owned_by("b1"),
        )[1],
    )


@step
def defer_with_the_owner():
    close_any()
    activate(state["views"]["a1"])
    try:
        asked_under = Control.currentOwner()
    except Exception as e:
        check("the current owner can be taken", False, "%s: %s" % (type(e).__name__, e))
        return
    QtCore.QTimer.singleShot(
        150, lambda: Control.showDialog(Panel("deferred"), view=asked_under)
    )
    # ... and by the time it runs another view is the active one
    activate(state["views"]["a2"])


@step
def deferred_with_the_owner():
    claim("a deferred show carrying its owner is a1's, a2 active", lambda: owned_by("a1"))


@step
def defer_without():
    close_any()
    activate(state["views"]["a1"])
    QtCore.QTimer.singleShot(150, lambda: Control.showDialog(Panel("deferred bare")))
    activate(state["views"]["a2"])


@step
def deferred_without():
    claim("control: one carrying none is the then-active view's, a2", lambda: owned_by("a2"))


@step
def a_command():
    close_any()
    activate(state["views"]["a2"])
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(DOC_A, "Box")
    settle(100)
    FreeCADGui.runCommand("Std_Placement")


@step
def a_commands_dialog():
    claim("a command's dialog (Std_Placement) run in a2 is a2's", lambda: owned_by("a2"))
    close_any()
    FreeCADGui.Selection.clearSelection()


@step
def edit_in_another_document():
    close_any()
    activate(state["views"]["a1"])
    # Document B's view is not the active one: setEdit activates it
    entered = FreeCADGui.getDocument(DOC_B).setEdit(FreeCAD.getDocument(DOC_B).Pad, 0)
    check("the pad is in edit", entered)


@step
def edit_dialog_is_the_edit_views():
    a1 = state["views"]["a1"]
    claim("an edit of B's pad started while a1 was active is b1's", lambda: owned_by("b1"))
    claim("a1 has no dialog during it", lambda: not Control.activeDialog(view=a1))
    claim(
        "document B has one",
        lambda: Control.activeDialog(attachTo=FreeCAD.getDocument(DOC_B)),
    )
    FreeCADGui.getDocument(DOC_B).resetEdit()


@step
def edit_left():
    check("leaving the edit closes its dialog", not Control.activeDialog(), owner_or_error())
    close_any()


# ---- served ----------------------------------------------------------------


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


def gui(name):
    """Have the GUI thread do `name`, and wait until it has."""
    wanted["name"] = name
    served.clear()
    pending.set()
    if not served.wait(40.0):
        raise RuntimeError("the GUI thread never did " + name)


class Talk(threading.Thread):
    """The client's conversation, off the GUI thread."""

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

    def talk(self):
        ws = WS(self.port)
        ws.hello("task-panel-owner")
        if ws.next_binary(20.0) is None:
            raise RuntimeError("no snapshot")
        ws.next_binary(0.5)
        ws.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH))
        ws.drain(0.5)

        self.got["edit"] = error_of(ws.op('{"id":1,"op":"edit","obj":"Pad","mode":0}'))
        ws.drain(1.0)
        gui("sample:client")
        self.got["reset"] = error_of(ws.op('{"id":2,"op":"resetEdit"}'))
        ws.drain(1.0)
        gui("sample:client-left")
        gui("desktop-enter")
        gui("sample:desktop")
        gui("desktop-leave")
        gui("sample:desktop-left")
        ws.close()


def do(name):
    b1 = state["views"]["b1"]
    gdoc = FreeCADGui.getDocument(DOC_B)
    if name == "desktop-enter":
        FreeCADGui.getMainWindow().setActiveWindow(b1)
        gdoc.setEdit(FreeCAD.getDocument(DOC_B).Pad, 0)
    elif name == "desktop-leave":
        gdoc.resetEdit()
    elif name.startswith("sample:"):
        sample = {"open": bool(Control.activeDialog())}
        try:
            sample["owner"] = owner()
            sample["b1"] = bool(Control.activeDialog(view=b1))
            sample["docB"] = bool(Control.activeDialog(attachTo=FreeCAD.getDocument(DOC_B)))
            sample["docA"] = bool(Control.activeDialog(attachTo=FreeCAD.getDocument(DOC_A)))
        except Exception as e:
            sample["error"] = "%s: %s" % (type(e).__name__, e)
        state["samples"][name[7:]] = sample


@step
def serve():
    close_any()
    port = free_port()
    ok = FreeCADGui.serveDocument(FreeCAD.getDocument(DOC_B), port)
    if not check("document B is served beside its window", ok, "port %d" % port):
        return
    state["talk"] = Talk(port)
    state["talk"].start()
    state["t0"] = clock()


def poll():
    talk = state["talk"]
    if pending.is_set():
        pending.clear()
        try:
            do(wanted["name"])
        except Exception:
            note("ABORT step %s:\n%s" % (wanted["name"], traceback.format_exc()))
        # Let what the step raised settle before the conversation reads
        QtCore.QTimer.singleShot(400, served.set)
    if talk.is_alive():
        if clock() - state["t0"] > CLIENT_WAIT_S:
            check("the conversation finished", False, "still talking after %ds" % CLIENT_WAIT_S)
            finish()
            return
        QtCore.QTimer.singleShot(20, poll)
        return
    QtCore.QTimer.singleShot(300, verify)


def verify():
    try:
        talk = state["talk"]
        if talk.error:
            note("FAIL conversation:\n" + talk.error)
        got, s = talk.got, state["samples"]
        check("the client's edit is accepted", got.get("edit") == "", got.get("edit"))
        c = s.get("client", {})
        check("a client's edit opens a dialog", c.get("open"), c)
        check(
            "it belongs to that client's view: no view of the main window",
            c.get("owner") == ("client", "none"),
            c,
        )
        check("b1, the same document's window, has none", c.get("b1") is False, c)
        check(
            "document B has one, document A none",
            (c.get("docB"), c.get("docA")) == (True, False),
            c,
        )
        check("the client's resetEdit is accepted", got.get("reset") == "", got.get("reset"))
        check(
            "it closes the dialog",
            s.get("client-left", {}).get("open") is False,
            s.get("client-left"),
        )
        d = s.get("desktop", {})
        check(
            "the desktop's own edit, beside the client, is b1's",
            d.get("owner") == ("view", "b1") and d.get("b1") is True,
            d,
        )
        check(
            "leaving it closes that one",
            s.get("desktop-left", {}).get("open") is False,
            s.get("desktop-left"),
        )
    except Exception:
        note("FAIL verify:\n" + traceback.format_exc())
    finish()


# ---- the run ---------------------------------------------------------------


def advance():
    if state["done"]:
        return
    if not steps:
        if state["talk"] is not None:
            QtCore.QTimer.singleShot(50, poll)
        else:
            finish()
        return
    fn = steps.pop(0)
    try:
        fn()
    except Exception:
        note("ABORT step %s:\n%s" % (fn.__name__, traceback.format_exc()))
        finish()
        return
    # Back to the main loop between steps: a closed dialog is deleted from
    # there, not from an event loop nested in the step that closed it.
    QtCore.QTimer.singleShot(400, advance)


def finish():
    if state["done"]:
        return
    state["done"] = True
    VIEW.SetBool("PerViewEdit", False)
    try:
        for name in list(FreeCAD.listDocuments().keys()):
            FreeCADGui.getDocument(name).resetEdit()
        close_any()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, advance)
