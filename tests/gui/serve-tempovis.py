"""On a served document, TempoVis reaches the clients in the edit only.

The serving half of tests/gui/tempovis-per-view.py. An edit's visibility
automation used to write Visibility, so every client of a served
document saw what one sketch edit hid and showed. As transient entries
of the session views' own tables it reaches a client through that
client's `visibility` push, and a client outside the session not at all.

A document with a real 3D window and two served clients: A states a
camera, so it has a view and joins the session; B only says hello. A
body of a pad, a sketch on its top face and a second pad from that
sketch; the second sketch is edited from the desktop, so TempoVis hides
the second pad and shows the first.

Asserted:
  - A is told a table that hides a draw and shows one, and B nothing;
  - neither pad's Visibility is written on the host;
  - when the desktop leaves, A's table is empty again.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set and this
directory on PYTHONPATH.
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
DOC = "ServeTempoVis"
CLIENT_WAIT_S = 150
V = FreeCAD.Vector

EYE = (5.0, 5.0, 120.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 400.0
VW, VH = 800, 600

state = {"doc": None, "a": None, "b": None, "done": False, "t0": clock(),
         "phase": "start", "samples": {}}
desktop_entered = threading.Event()
desktop_left = threading.Event()
finished = threading.Event()


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def tables(pushes):
    """The (hidden, shown) counts of every `visibility` push, in order."""
    out = []
    for text in pushes:
        if b'"cmd":"visibility"' not in text:
            continue
        try:
            msg = json.loads(text.decode("utf-8"))
        except Exception:
            continue
        out.append((len(msg.get("hidden", [])), len(msg.get("shown", []))))
    return out


class Watcher(threading.Thread):
    """Client B: says hello and listens. No camera, so no view."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.ready = threading.Event()
        self.pushes = []

    def run(self):
        try:
            ws = WS(self.port)
            ws.hello("serve-tempovis-b")
            ws.next_binary(20.0)
            self.ready.set()
            while not finished.is_set():
                ws.drain(0.2)
            ws.drain(0.3)
            self.pushes = list(ws.pushes)
            ws.close()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()


class Client(threading.Thread):
    """Client A: states a camera and watches the desktop's session."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.ready = threading.Event()
        self.watched = threading.Event()
        self.in_edit = []
        self.after = []

    def run(self):
        try:
            ws = WS(self.port)
            ws.hello("serve-tempovis-a")
            if ws.next_binary(20.0) is None:
                raise RuntimeError("no snapshot")
            ws.next_binary(0.5)
            ws.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH))
            ws.drain(0.5)
            mark = len(ws.pushes)
            self.ready.set()
            desktop_entered.wait(30.0)
            ws.drain(2.0)
            self.in_edit = tables(ws.pushes[mark:])
            mark = len(ws.pushes)
            self.watched.set()
            desktop_left.wait(30.0)
            ws.drain(2.0)
            self.after = tables(ws.pushes[mark:])
            ws.close()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()
            self.watched.set()


def gdoc():
    return FreeCADGui.getDocument(DOC)


def rectangle(sketch, a, b):
    corners = [V(a, a, 0), V(b, a, 0), V(b, b, 0), V(a, b, 0)]
    for i in range(4):
        sketch.addGeometry(Part.LineSegment(corners[i], corners[(i + 1) % 4]))


def top_face(feature):
    faces = feature.Shape.Faces
    index = max(range(len(faces)), key=lambda i: faces[i].CenterOfMass.z)
    return "Face%d" % (index + 1)


def sample(tag):
    doc = state["doc"]
    state["samples"][tag] = {
        "edit": gdoc().getInEdit() is not None,
        "pad1": doc.Pad1.Visibility,
        "pad2": doc.Pad2.Visibility,
        "held": (gdoc().getEditVisibility(doc.Pad2), gdoc().getEditVisibility(doc.Pad1)),
    }


def build():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Sketcher/General").SetBool(
            "FitSketchOnEdit", False)
        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        body = doc.addObject("PartDesign::Body", "Body")
        sketch1 = body.newObject("Sketcher::SketchObject", "Sketch1")
        sketch1.Support = (doc.getObject("XY_Plane"), [""])
        sketch1.MapMode = "FlatFace"
        rectangle(sketch1, 0, 10)
        pad1 = body.newObject("PartDesign::Pad", "Pad1")
        pad1.Profile = sketch1
        pad1.Length = 10
        doc.recompute()
        sketch2 = body.newObject("Sketcher::SketchObject", "Sketch2")
        sketch2.Support = (pad1, [top_face(pad1)])
        sketch2.MapMode = "FlatFace"
        rectangle(sketch2, 3, 7)
        pad2 = body.newObject("PartDesign::Pad", "Pad2")
        pad2.Profile = sketch2
        pad2.Length = 10
        doc.recompute()
        pad1.Visibility = False
        sketch1.Visibility = False
        sketch2.Visibility = False
        vo = sketch2.ViewObject
        for prop, value in (("HideDependent", True), ("ShowSupport", True),
                            ("ShowLinks", False), ("ShowGrid", False),
                            ("RestoreCamera", False)):
            if hasattr(vo, prop):
                setattr(vo, prop, value)
        sample("idle")

        port = free_port()
        ok = FreeCADGui.serveDocument(doc, port)
        if not check("the document is served beside its window", ok, "port %d" % port):
            finish()
            return
        state["a"] = Client(port)
        state["b"] = Watcher(port)
        state["a"].start()
        state["b"].start()
        QtCore.QTimer.singleShot(50, poll)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def poll():
    a, b = state["a"], state["b"]
    if a.error or b.error:
        QtCore.QTimer.singleShot(200, verify)
        return
    phase = state["phase"]
    try:
        if phase == "start" and a.ready.is_set() and b.ready.is_set():
            state["phase"] = "desktop-edit"
            opened = gdoc().setEdit(state["doc"].Body, 0, "Sketch2.")
            check("the desktop entered the sketch's edit", opened, opened)
            QtCore.QTimer.singleShot(800, lambda: (sample("desktop"),
                                                   desktop_entered.set()))
        elif phase == "desktop-edit" and a.watched.is_set():
            state["phase"] = "desktop-leave"
            gdoc().resetEdit()
            QtCore.QTimer.singleShot(800, lambda: (sample("after"), desktop_left.set()))
        elif phase == "desktop-leave" and not a.is_alive():
            state["phase"] = "end"
            finished.set()
    except Exception:
        note("ABORT poll:\n" + traceback.format_exc())
        QtCore.QTimer.singleShot(200, verify)
        return
    if a.is_alive() or b.is_alive():
        if clock() - state["t0"] > CLIENT_WAIT_S:
            check("the clients finished", False,
                  "still talking after %ds in phase %s" % (CLIENT_WAIT_S, state["phase"]))
            finish()
            return
        QtCore.QTimer.singleShot(20, poll)
        return
    QtCore.QTimer.singleShot(500, verify)


def verify():
    a, b = state["a"], state["b"]
    s = state["samples"]
    try:
        check("client A ran without error", a.error is None, a.error or "")
        check("client B ran without error", b.error is None, b.error or "")
        desk, after = s.get("desktop", {}), s.get("after", {})
        check("the desktop's session is on", desk.get("edit") is True, desk)
        check("TempoVis's hide and show are entries of the session",
              desk.get("held") == (False, True), desk)
        check("and neither Visibility is written",
              desk.get("pad2") is True and desk.get("pad1") is False, desk)
        told = a.in_edit
        check("a client with a view is told a table that hides a draw and shows one",
              bool(told) and told[-1][0] >= 1 and told[-1][1] >= 1, told)
        check("and an empty one when the desktop leaves",
              bool(a.after) and a.after[-1] == (0, 0), a.after)
        check("the edit is left, with the Visibility it found",
              after.get("edit") is False and after.get("pad2") is True
              and after.get("pad1") is False and after.get("held") == (None, None), after)
        seen = tables(b.pushes)
        check("a client with no view is told nothing of it",
              all(t == (0, 0) for t in seen), seen)
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    desktop_entered.set()
    desktop_left.set()
    finished.set()
    try:
        gdoc().resetEdit()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, build)
