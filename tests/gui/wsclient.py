"""Just enough of RFC 6455 to be a served viewer, plus the frames one sends.

Shared by the serving-tier GUI tests and by the camera-uplink bench
(docs/ThinClient.md sec 8.10a), which have to speak the same wire: the
bench measures what a policy puts on the link, and it would measure
nothing if its frames were not byte-for-byte the ones the viewer sends.

Deliberately hand-rolled rather than a library: these tests run inside the
FreeCAD process, which has no websocket client, and a client that mirrors
our own parser is the point -- the independent implementation the wire is
checked against is Boost.Beast, in tests/src/Gui/SceneServerWire.cpp.
"""
import base64
import os
import socket
import struct
import time

clock = time.perf_counter

# The two viewer event frames (docs/ThinClient.md sec 8.5) and the
# combined one of sec 8.10a.
CAMERA_SIZE = 6 + 13 * 4
PICK_SIZE = 2 + 6 * 4


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def camera_frame(eye, quat, height_angle, near, far, width, height,
                 dpr=1.0, pick_radius=5.0, perspective=True):
    """'C', a type byte, the canvas as two u16, then thirteen floats."""
    return (b"C" + bytes([1 if perspective else 0])
            + struct.pack("<HH", width, height)
            + struct.pack("<13f", eye[0], eye[1], eye[2],
                          quat[0], quat[1], quat[2], quat[3],
                          height_angle, near, far,
                          float(width) / float(height), dpr, pick_radius))


def pick_frame(origin, direction, modifiers=0):
    """'P', a flags byte, then the world ray as six floats."""
    return (b"P" + bytes([modifiers])
            + struct.pack("<6f", *origin, *direction))


def camera_and_pick(camera, pick):
    """'Q' (sec 8.10a): a camera frame and a pick in one message, each
    verbatim -- one frame instead of two, and the pairing atomic rather
    than merely ordered."""
    return b"Q" + camera + pick


def wire_bytes(payload):
    """What a client message of \a payload bytes costs on the link: the
    payload plus the header a client masks it under."""
    n = len(payload) if isinstance(payload, (bytes, bytearray)) else payload
    return n + 2 + 4 + (0 if n < 126 else (2 if n < 65536 else 8))


class WS:
    """One client connection: the upgrade, masked frames out, unmasked
    frames in."""

    def __init__(self, port, path="/scene"):
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
        # As a browser's socket is, and as the server sets its own
        # accepted sockets (tcp::no_delay). It matters here and not only
        # for tidiness: a policy that sends a camera and then a pick
        # writes two small segments back to back, and on a socket that
        # has been idle Nagle holds the second until the first is
        # acknowledged -- which the peer delays. Measured at ~43 ms of
        # pure artefact before this line went in (docs/ThinClient.md sec
        # 8.10b). A bench must not measure its own transport.
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((
            "GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n" % (path, port, key)).encode())
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

    def hello(self, label, extra=""):
        self.send(1, ('{"cmd":"hello","client":"%s","snapshot":0%s}'
                      % (label, extra)).encode())

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

    def drain(self, timeout=0.0):
        """Read and discard whatever is already waiting."""
        while self.next_binary(timeout) is not None:
            pass

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass
