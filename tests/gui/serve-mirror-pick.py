"""A served click is resolved through the mirror of the client that sent it.

Stage 3 of docs/ThinClient.md sec 8.9. A viewer states its camera and its
canvas over the wire (the `'C'` frame of sec 8.5); the serving process keeps
a Gui::MirrorViewer per connection and resolves that connection's picks
against it instead of against the one synthetic framing nobody is looking
through.

What that buys is measured here directly. Coin gives a ray set with
SoRayPickAction::setRay a radius of essentially zero and ignores setRadius()
altogether -- so a served click had to hit geometry dead-on, which for an
edge or a vertex means it never hit at all. Through a mirror the ray goes
back to the viewport point it was computed from and the client's pick radius
in pixels applies, exactly as on the desktop.

What is asserted:
  - a ray three pixels clear of the box silhouette picks NOTHING while the
    connection has stated no camera (the old behaviour, still the fallback);
  - the same ray, after a 'C' frame, picks an edge of that box;
  - a ray through the middle of the top face picks that face either way, so
    the mirror did not simply widen everything;
  - a malformed camera frame is refused rather than adopted: a NaN in it
    would poison the view volume, and the mirror must still be the one it
    was before;
  - the click-to-scene-push time through the mirror -- the first
    user-visible number for the whole loop (sec 8.8's budget).

The camera is stated as a real one and the rays are built from it here, the
way the browser viewer builds them from its own: eye above the box looking
straight down, so the arithmetic below is legible.

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
DOC = "ServeMirrorPick"
ECHO_BOUND_MS = 1000.0
CLIENT_WAIT_S = 90

# The camera this client states. Eye straight above the box, looking down
# -Z with +Y up -- which in Coin's convention (the camera looks down its own
# -Z with +Y up) is the identity rotation.
EYE = (5.0, 5.0, 60.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 200.0
VW, VH = 800, 600
ASPECT = float(VW) / float(VH)
PICK_RADIUS = 5.0

# The box is 10 on a side at the origin, so its top face is z = 10 and the
# eye is 50 above it.
TOP_Z = 10.0
DEPTH = EYE[2] - TOP_Z
TH = math.tan(0.5 * HEIGHT_ANGLE)


def units_per_px(eye=None):
    """World units per pixel where the geometry is: the half-width of the
    frustum there, over half the canvas."""
    depth = (eye or EYE)[2] - TOP_Z
    return (depth * TH * ASPECT) / (0.5 * VW)


UNITS_PER_PX = units_per_px()

# A second, much more distant camera, for the combined 'Q' frame of sec
# 8.10a. Five times as far, so a pixel there is five times as much world
# -- which is what makes the case discriminating: a ray built three of
# THESE pixels clear of the silhouette is fifteen of the near camera's,
# and its origin sits behind the near camera entirely. If the camera half
# of the 'Q' were dropped, the pick could not land on the edge by luck.
FAR_EYE = (5.0, 5.0, 260.0)
FAR_NEAR, FAR_FAR = 10.0, 400.0

state = {"doc": None, "port": 0, "client": None, "done": False, "t0": clock()}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def camera_frame(height_angle=HEIGHT_ANGLE, near=NEAR, far=FAR, eye=EYE):
    """This client's camera, as the 'C' frame of sec 8.5."""
    return wsclient.camera_frame(eye, QUAT, height_angle, near, far, VW, VH,
                                 pick_radius=PICK_RADIUS)


def pick(origin, direction, modifiers=0):
    return wsclient.pick_frame(origin, direction, modifiers)


def ray_to(x, y, eye=EYE):
    """The world ray \a eye casts through the pixel that shows world
    (x, y) on the top face -- built the way the browser viewer builds it,
    from the camera's own frame rather than from a matrix."""
    depth = eye[2] - TOP_Z
    direction = ((x - eye[0]) / depth, (y - eye[1]) / depth, -1.0)
    length = math.sqrt(sum(c * c for c in direction))
    return eye, tuple(c / length for c in direction)


class Client(threading.Thread):
    """The whole wire conversation, off the GUI thread."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.no_camera_pushed = None
        self.mirror_pushed = None
        self.mirror_ms = None
        self.restated_pushed = None
        self.combined_pushed = None
        self.face_pushed = None
        self.bad_frame_pushed = None

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()

    def talk(self):
        ws = WS(self.port)
        ws.send(1, b'{"cmd":"hello","client":"serve-mirror-pick","snapshot":0}')
        self.snapshot = ws.next_binary(20.0) is not None
        if not self.snapshot:
            return
        ws.next_binary(0.5)   # whatever else the join volunteers

        # Three pixels clear of the box's +x silhouette, at mid height.
        outside = ray_to(10.0 + 3.0 * UNITS_PER_PX, 5.0)

        # 1. No camera stated: the fallback zero-radius ray, which misses.
        ws.send(2, pick(*outside))
        self.no_camera_pushed = ws.next_binary(1.0) is not None

        # 2. The same ray, once this connection has a mirror.
        ws.send(2, camera_frame())
        t0 = clock()
        ws.send(2, pick(*outside))
        data = ws.next_binary(5.0)
        self.mirror_pushed = data is not None
        if data is not None:
            self.mirror_ms = (clock() - t0) * 1000.0
        ws.next_binary(0.3)

        # 2b. The premise the lazy camera uplink rests on (sec 8.10a):
        # the mirror is per connection and OUTLIVES the click that built
        # it, so a client whose camera has not moved need not restate it.
        # No camera frame here at all -- if the mirror were gone the
        # fallback would be the zero-radius ray of case 1, which case 1
        # showed picks nothing. Another edge, so the selection changes
        # and the push is a push.
        upp = units_per_px()
        ws.send(2, pick(*ray_to(0.0 - 3.0 * upp, 5.0)))
        self.restated_pushed = ws.next_binary(5.0) is not None
        ws.next_binary(0.3)

        # 2c. The combined frame of sec 8.10a: a camera and a pick in one
        # message. The camera is the distant one and the ray is built
        # three of ITS pixels clear of the +y silhouette, so the pick can
        # only land if the camera half of this very message was adopted
        # before the pick half ran.
        far_upp = units_per_px(FAR_EYE)
        ws.send(2, wsclient.camera_and_pick(
            camera_frame(near=FAR_NEAR, far=FAR_FAR, eye=FAR_EYE),
            pick(*ray_to(5.0, 10.0 + 3.0 * far_upp, FAR_EYE))))
        self.combined_pushed = ws.next_binary(5.0) is not None
        ws.next_binary(0.3)

        # 3. A camera frame that is not usable must be refused, not
        # adopted: the mirror keeps the one it had, so the middle of the
        # top face still picks that face.
        ws.send(2, camera_frame(height_angle=float("nan"), near=FAR_NEAR,
                                far=FAR_FAR, eye=FAR_EYE))
        ws.send(2, pick(*ray_to(5.0, 5.0, FAR_EYE)))
        self.face_pushed = ws.next_binary(5.0) is not None
        ws.sock.close()


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
        box = doc.addObject("Part::Box", "Box")
        box.Length = box.Width = box.Height = 10
        doc.recompute()
        port = free_port()
        state["port"] = port
        ok = FreeCADGui.serveDocument(doc, port)
        if not check("the document is served headless", ok, "port %d" % port):
            finish()
            return
        state["client"] = Client(port)
        state["client"].start()
        state["seen"] = []
        QtCore.QTimer.singleShot(50, poll)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def selection_now():
    return sorted((s.ObjectName, tuple(s.SubElementNames))
                  for s in FreeCADGui.Selection.getSelectionEx(DOC))


def poll():
    client = state["client"]
    # The in-process selection is sampled as the conversation runs: each
    # step replaces it, so the end state alone would only show the last.
    state["seen"].append(selection_now())
    if client.is_alive():
        if clock() - state["t0"] > CLIENT_WAIT_S:
            check("the client finished", False, "still talking after %ds" % CLIENT_WAIT_S)
            finish()
            return
        QtCore.QTimer.singleShot(20, poll)
        return
    verify()


def verify():
    client = state["client"]
    check("the client ran without error", client.error is None, client.error or "")
    check("the hello was answered with a snapshot", client.snapshot)

    check("a ray clear of the silhouette picks nothing without a camera",
          client.no_camera_pushed is False,
          "pushed: %s" % client.no_camera_pushed)
    check("the same ray picks through the mirror", client.mirror_pushed is True,
          "pushed: %s" % client.mirror_pushed)
    check("the mirrored click echoed within %d ms" % ECHO_BOUND_MS,
          client.mirror_ms is not None and client.mirror_ms < ECHO_BOUND_MS,
          "%s ms" % ("%.1f" % client.mirror_ms
                     if client.mirror_ms is not None else None))

    # What it picked: an edge of the box, which is the element a
    # zero-radius ray could never land on.
    seen = state["seen"]
    edges = [s for s in seen
             if s and s[0][0] == "Box" and s[0][1]
             and s[0][1][0].startswith("Edge")]
    distinct = []
    for sel in seen:
        if not distinct or distinct[-1] != sel:
            distinct.append(sel)
    check("the mirrored pick landed on an edge", bool(edges),
          "selections seen: %s" % distinct)

    check("a pick with no camera restated still goes through the mirror",
          client.restated_pushed is True,
          "pushed: %s" % client.restated_pushed)
    check("a camera and a pick in one 'Q' message both land",
          client.combined_pushed is True,
          "pushed: %s" % client.combined_pushed)
    # Three edges, from three different clicks: the mirrored one, the one
    # that restated no camera, and the one whose camera rode inside it.
    # Distinct, because a repeat of the same edge would prove only that
    # something was still selected.
    picked_edges = sorted({s[0][1][0] for s in seen
                           if s and s[0][0] == "Box" and s[0][1]
                           and s[0][1][0].startswith("Edge")})
    check("each of the three mirrored clicks landed on its own edge",
          len(picked_edges) >= 3,
          "edges seen: %s" % (picked_edges,))

    check("a face pick still works after a refused camera frame",
          client.face_pushed is True, "pushed: %s" % client.face_pushed)
    check("the refused frame left the mirror usable",
          selection_now() == [("Box", ("Face6",))], str(selection_now()))

    if client.mirror_ms is not None:
        note("click-to-push through the mirror: %.1f ms" % client.mirror_ms)
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(0, build)
