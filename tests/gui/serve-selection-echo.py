"""A remote pick on a headless serve source comes back as a scene push.

The measurement docs/ThinClient.md sec 8.9 step 0 asked for -- click,
let the selection come back as a delta, log click-to-delta -- found
that on a headless source (Gui.serveDocument, no 3D view) it never came
back at all. The pick reached the GUI thread and Gui::Selection in a
millisecond, and then nothing: the selection root only fed the render
cache through its viewer (SoFCUnifiedSelection::Private::checkSelection
returned before feeding when it had none), and the source was not a
SelectionObserver in the first place, so the root never heard of the
selection (docs/HeadlessServe.md). Fixed 2026-09-06 on both counts; this
test is the loop the fix closes, over a real socket.

What is asserted:
  - the hello is answered with a snapshot;
  - a 'P' pick on a box's top face is followed by a binary scene push,
    and the in-process selection holds that face;
  - a pick that changes nothing (ctrl-click on empty space) pushes
    nothing;
  - a 'B' batch of two ctrl-picks pushes ONE frame, not two;
  - the echo lands within a generous bound (the loopback figure is
    single-digit milliseconds; the bound is for a loaded ctest box).

The client is a minimal WebSocket implementation on a raw socket in a
thread, so the GUI thread stays free to run the pick the server hands it.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt.
"""
import base64
import os
import socket
import struct
import threading
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

# time.monotonic() is GetTickCount64() on Windows through CPython 3.12,
# 15.6 ms of resolution -- which reads every loopback echo below one
# tick as 0.0 ms and makes the figure this test exists to report
# unmeasurable there. perf_counter is QueryPerformanceCounter on
# Windows and clock_gettime(MONOTONIC) on Linux: monotonic on both,
# and the resolution the numbers need.
clock = time.perf_counter

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ServeSelectionEcho"
ECHO_BOUND_MS = 1000.0
CLIENT_WAIT_S = 90

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
    """Just enough of RFC 6455 for one client: the upgrade, masked
    frames out, unmasked frames in."""

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
        """(opcode, payload) or None when nothing whole arrives in time."""
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


def pick(origin, direction, modifiers=0):
    return b"P" + bytes([modifiers]) + struct.pack("<6f", *origin, *direction)


def batch(picks):
    out = b"B" + bytes([len(picks)])
    for origin, direction, modifiers in picks:
        out += bytes([modifiers]) + struct.pack("<6f", *origin, *direction)
    return out


def ray(i):
    # Box i occupies x in [20i, 20i+10]; straight down onto its top face.
    return (20.0 * i + 5.0, 5.0, 50.0), (0.0, 0.0, -1.0)


class Client(threading.Thread):
    """The whole wire conversation, off the GUI thread."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.echo_ms = []
        self.no_change_frames = None
        self.batch_frames = None
        self.batch_ms = None

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()

    def talk(self):
        ws = WS(self.port)
        ws.send(1, b'{"cmd":"hello","client":"serve-selection-echo","snapshot":0}')
        self.snapshot = ws.next_binary(20.0) is not None
        if not self.snapshot:
            return
        # Whatever else the join volunteers (a docs listing, a reload
        # hint for the stale snapshot number) is text and skipped; a
        # second binary frame here would be a spurious publish.
        ws.next_binary(0.5)
        for i in (1, 2):
            t0 = clock()
            ws.send(2, pick(*ray(i)))
            data = ws.next_binary(5.0)
            if data is None:
                self.echo_ms.append(None)
            else:
                self.echo_ms.append((clock() - t0) * 1000.0)
            ws.next_binary(0.2)  # a second push would be a second publish
        ws.send(2, pick((-100.0, -100.0, 50.0), (0.0, 0.0, -1.0), 1))
        self.no_change_frames = 0 if ws.next_binary(0.7) is None else 1
        t0 = clock()
        ws.send(2, batch([(ray(0)[0], ray(0)[1], 1), (ray(1)[0], ray(1)[1], 1)]))
        frames = 0
        while True:
            data = ws.next_binary(0.5 if frames else 5.0)
            if data is None:
                break
            frames += 1
            if frames == 1:
                self.batch_ms = (clock() - t0) * 1000.0
        self.batch_frames = frames
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
        for i in range(3):
            box = doc.addObject("Part::Box", "Box%d" % i)
            box.Length = box.Width = box.Height = 10
            box.Placement.Base.x = i * 20
        doc.recompute()
        port = free_port()
        state["port"] = port
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
        QtCore.QTimer.singleShot(50, poll)
        return
    verify()


def verify():
    client = state["client"]
    check("the client ran without error", client.error is None, client.error or "")
    check("the hello was answered with a snapshot", client.snapshot)
    echoes = client.echo_ms
    check("every pick was followed by a scene push",
          len(echoes) == 2 and all(e is not None for e in echoes),
          "echo ms: %s" % ["%.1f" % e if e is not None else None for e in echoes])
    good = [e for e in echoes if e is not None]
    check("the echo landed within %d ms" % ECHO_BOUND_MS,
          bool(good) and max(good) < ECHO_BOUND_MS,
          "max %.1f ms" % max(good) if good else "no echo")
    check("a pick that changed nothing pushed nothing", client.no_change_frames == 0,
          "frames: %s" % client.no_change_frames)
    check("a batch of two ctrl-picks pushed one frame", client.batch_frames == 1,
          "frames: %s, first after %s ms"
          % (client.batch_frames,
             "%.1f" % client.batch_ms if client.batch_ms is not None else None))
    # Plain pick Box1, plain pick Box2, then ctrl-add Box0 and Box1: the
    # in-process selection is what the wire said it should be.
    sel = sorted((s.ObjectName, tuple(s.SubElementNames))
                 for s in FreeCADGui.Selection.getSelectionEx(DOC))
    want = [("Box0", ("Face6",)), ("Box1", ("Face6",)), ("Box2", ("Face6",))]
    check("the in-process selection matches the picks", sel == want, str(sel))
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
