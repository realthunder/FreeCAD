#!/usr/bin/env python3
"""Drive the thin client's control channel from the shell.

The semantic control channel (docs/ThinClient.md sec 3) is id-correlated
JSON over the scene WebSocket's text lane -- the same lane the browser
viewer's DOM layer uses for getProperties/setProperty and, since
docs/SpreadsheetRemote.md sec 3, sheet.get/sheet.set.  This is that lane
without a browser: it sends ops and prints the answers, plus any
unsolicited push (sheet.changed and friends) that arrives meanwhile.

A minimal RFC 6455 client is inline rather than a dependency: the conda
dev env ships no websocket library, and what is needed here is a
client-masked text frame and an unmasked read.  Binary frames -- the
scene payload itself -- are counted and dropped; this tool is about the
text lane.

Usage:
    scripts/control-client.py [--port 8077] [--host 127.0.0.1]
                              [--doc NAME] [--listen SECONDS]
                              OP [key=value ...]  [-- OP [key=value ...]]

    # one op
    scripts/control-client.py --port 8077 sheet.get obj=Spreadsheet

    # several in sequence, then listen for pushes for 3 s
    scripts/control-client.py --port 8077 --listen 3 \\
        sheet.get obj=Spreadsheet -- \\
        sheet.set obj=Spreadsheet cell=A1 content=42

A value that parses as JSON is sent as JSON (so `content=42` is a
number, `content='"42"'` a string); anything else is sent as a string.
"""

import argparse
import base64
import json
import os
import socket
import struct
import sys
import time


class WebSocket:
    """Barely enough of RFC 6455 to talk to the scene server."""

    def __init__(self, host, port, path, timeout=10.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        request = (
            "GET %s HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n" % (path, host, port, key)
        )
        self.sock.sendall(request.encode())
        self.buf = b""
        while b"\r\n\r\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise RuntimeError("server closed during handshake")
            self.buf += chunk
        head, self.buf = self.buf.split(b"\r\n\r\n", 1)
        status = head.split(b"\r\n", 1)[0].decode()
        if "101" not in status:
            raise RuntimeError("upgrade refused: " + status)
        # Announce ourselves as a viewer.  Without this the server answers
        # control requests but never BROADCASTS to the connection --
        # conn->viewer gates the unsolicited push lane, and it is the hello
        # that sets it.  A client that only wants replies works without one;
        # a client that wants sheet.changed does not.
        # Compact separators are REQUIRED: the server sniffs the literal
        # '"cmd":"hello"' out of the frame, so json.dumps' default ", "
        # spacing silently fails to register the connection as a viewer.
        self.send_text(json.dumps({"cmd": "hello", "snapshot": 0, "page": 0,
                                   "client": "control-client.py"},
                                  separators=(",", ":")))

    def send_text(self, text):
        payload = text.encode()
        header = bytearray([0x81])              # FIN + text
        mask = os.urandom(4)
        n = len(payload)
        if n < 126:
            header.append(0x80 | n)
        elif n < 0x10000:
            header.append(0x80 | 126)
            header += struct.pack(">H", n)
        else:
            header.append(0x80 | 127)
            header += struct.pack(">Q", n)
        header += mask
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(bytes(header) + masked)

    def _fill(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("closed")
            self.buf += chunk

    def recv_frame(self):
        """Return (opcode, payload); raises socket.timeout when idle."""
        self._fill(2)
        b0, b1 = self.buf[0], self.buf[1]
        opcode = b0 & 0x0F
        length = b1 & 0x7F
        offset = 2
        if length == 126:
            self._fill(4)
            length = struct.unpack(">H", self.buf[2:4])[0]
            offset = 4
        elif length == 127:
            self._fill(10)
            length = struct.unpack(">Q", self.buf[2:10])[0]
            offset = 10
        # The server never masks (RFC 6455: only clients do).
        self._fill(offset + length)
        payload = self.buf[offset:offset + length]
        self.buf = self.buf[offset + length:]
        return opcode, payload

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def parse_fields(pairs):
    fields = {}
    for pair in pairs:
        if "=" not in pair:
            raise SystemExit("expected key=value, got %r" % pair)
        key, value = pair.split("=", 1)
        try:
            fields[key] = json.loads(value)
        except ValueError:
            fields[key] = value
    return fields


def pump(ws, deadline, want_id=None, quiet=False):
    """Read frames until `want_id` is answered or the deadline passes."""
    answer = None
    while time.time() < deadline:
        ws.sock.settimeout(max(0.05, deadline - time.time()))
        try:
            opcode, payload = ws.recv_frame()
        except socket.timeout:
            break
        except ConnectionError:
            break
        if opcode == 0x8:                       # close
            break
        if opcode == 0x9:                       # ping -> pong
            continue
        if opcode != 0x1:                       # not text: the scene lane
            continue
        try:
            message = json.loads(payload.decode())
        except ValueError:
            continue
        if want_id is not None and message.get("id") == want_id:
            answer = message
            if not quiet:
                print("<- " + json.dumps(message)[:4000])
            return answer
        if not quiet:
            kind = "push" if "id" not in message else "other"
            print("<- (%s) %s" % (kind, json.dumps(message)[:2000]))
    return answer


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8077)
    ap.add_argument("--doc", default="")
    ap.add_argument("--listen", type=float, default=0.0,
                    help="after the ops, print pushes for this many seconds")
    ap.add_argument("--timeout", type=float, default=10.0)
    ap.add_argument("rest", nargs=argparse.REMAINDER)
    args = ap.parse_args()

    # Split the trailing arguments into ops on '--'.
    ops, current = [], []
    for token in args.rest:
        if token == "--":
            if current:
                ops.append(current)
            current = []
        else:
            current.append(token)
    if current:
        ops.append(current)
    if not ops and args.listen <= 0:
        ap.error("give an op, or --listen to only watch")

    path = "/scene?v=0&s=0"
    if args.doc:
        path += "&doc=" + args.doc
    ws = WebSocket(args.host, args.port, path, timeout=args.timeout)

    failures = 0
    try:
        for index, op in enumerate(ops):
            request = {"id": index + 1, "op": op[0]}
            request.update(parse_fields(op[1:]))
            if args.doc and "doc" not in request:
                request["doc"] = args.doc
            print("-> " + json.dumps(request))
            ws.send_text(json.dumps(request))
            answer = pump(ws, time.time() + args.timeout, want_id=index + 1)
            if answer is None:
                print("!! no answer to %s" % op[0])
                failures += 1
            elif not answer.get("ok", False):
                failures += 1
        if args.listen > 0:
            print("... listening %.1fs for pushes" % args.listen)
            pump(ws, time.time() + args.listen)
    finally:
        ws.close()
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
