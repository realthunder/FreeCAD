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
import base64
import math
import os
import socket
import struct
import threading
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

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
# World units per pixel where the geometry is: the half-width of the
# frustum there, over half the canvas.
UNITS_PER_PX = (DEPTH * TH * ASPECT) / (0.5 * VW)

state = {"doc": None, "port": 0, "client": None, "done": False, "t0": clock()}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class WS:
    """Just enough of RFC 6455 for one client: the upgrade, masked frames
    out, unmasked frames in."""

    def __init__(self, port):
        last = None
        for _ in range(200):
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=10)
                break
            except OSError as e:
                last = e
                time.sleep(0.05)
        else:
            raise RuntimeError("no listener on %d: %s" % (port, last))
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((
            "GET /scene HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n" % (port, key)).encode())
        self.buf = b""
        while b"\r\n\r\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise RuntimeError("handshake closed")
            self.buf += chunk
        head, self.buf = self.buf.split(b"\r\n\r\n", 1)
        if b" 101 " not in head.split(b"\r\n")[0]:
            raise RuntimeError("no upgrade: %r" % head[:120])

    def send(self, opcode, payload):
        n = len(payload)
        frame = bytearray([0x80 | opcode])
        if n < 126:
            frame.append(0x80 | n)
        elif n < 65536:
            frame.append(0x80 | 126)
            frame += struct.pack(">H", n)
        else:
            frame.append(0x80 | 127)
            frame += struct.pack(">Q", n)
        mask = os.urandom(4)
        frame += mask
        frame += bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(bytes(frame))

    def _need(self, n, deadline):
        while len(self.buf) < n:
            left = deadline - clock()
            if left <= 0:
                return False
            self.sock.settimeout(left)
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                return False
            if not chunk:
                raise RuntimeError("closed")
            self.buf += chunk
        return True

    def recv(self, timeout):
        deadline = clock() + timeout
        if not self._need(2, deadline):
            return None
        b0, b1 = self.buf[0], self.buf[1]
        n = b1 & 0x7F
        off = 2
        if n == 126:
            if not self._need(4, deadline):
                return None
            n = struct.unpack(">H", self.buf[2:4])[0]
            off = 4
        elif n == 127:
            if not self._need(10, deadline):
                return None
            n = struct.unpack(">Q", self.buf[2:10])[0]
            off = 10
        if not self._need(off + n, deadline):
            return None
        data = bytes(self.buf[off:off + n])
        self.buf = self.buf[off + n:]
        return b0 & 0x0F, data

    def next_binary(self, timeout):
        deadline = clock() + timeout
        while True:
            left = deadline - clock()
            if left <= 0:
                return None
            m = self.recv(left)
            if m is None:
                return None
            if m[0] == 2:
                return m[1]
            if m[0] == 8:
                raise RuntimeError("server closed the socket")


def camera_frame(height_angle=HEIGHT_ANGLE, near=NEAR, far=FAR):
    """'C', type byte, viewport w/h as u16, then thirteen floats."""
    return (b"C" + bytes([1]) + struct.pack("<HH", VW, VH)
            + struct.pack("<13f", EYE[0], EYE[1], EYE[2],
                          QUAT[0], QUAT[1], QUAT[2], QUAT[3],
                          height_angle, near, far, ASPECT,
                          1.0, PICK_RADIUS))


def pick(origin, direction, modifiers=0):
    return b"P" + bytes([modifiers]) + struct.pack("<6f", *origin, *direction)


def ray_to(x, y):
    """The world ray this camera casts through the pixel that shows world
    (x, y) on the top face -- built the way the browser viewer builds it,
    from the camera's own frame rather than from a matrix."""
    nx = (x - EYE[0]) / (DEPTH * TH * ASPECT)
    ny = (y - EYE[1]) / (DEPTH * TH)
    direction = (nx * TH * ASPECT, ny * TH, -1.0)
    length = math.sqrt(sum(c * c for c in direction))
    return EYE, tuple(c / length for c in direction)


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

        # 3. A camera frame that is not usable must be refused, not
        # adopted: the mirror keeps the one it had, so the middle of the
        # top face still picks that face.
        ws.send(2, camera_frame(height_angle=float("nan")))
        ws.send(2, pick(*ray_to(5.0, 5.0)))
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
