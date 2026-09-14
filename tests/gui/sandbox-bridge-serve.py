"""A sandbox guest's bridge ops reach the served document over the socket.

docs/Sandbox.md 7.20, C2. A guest in a viewer's page reaches back through its
own WebSocket: 'S' frames carry the bridge requests exactly as the guest writes
them, the host's per-connection endpoint (src/Gui/SandboxRemote.cpp) dispatches
them into the same bridge ops the desktop's guest reaches, and the answers come
back as 'FCSB' frames (SceneServer.h, SceneBridgeRequest). This drives that
wire from a plain socket client on a worker thread -- no browser, no guest --
against a headless serve with two token grants, one editing and one view-only;
tests/gui/sandbox-bridge-browser.py is the leg with a real guest.

What is asserted:

  - active_doc answers the served document as a handle, and the document is
    the table's owner: a fixed-layout get_attr of its Name is answered in the
    fixed layout;
  - a call on the document returns an object; write_prop on it lands and
    read_prop reads it back; ops sent back to back are answered in order;
  - the end of a statement releases its handles (a stale id is refused), and
    a durable key re-resolves afterwards;
  - the document principal holds: app.new_doc is refused, another open
    document is out of reach, an undecodable request is a ProtocolError;
  - a view-only connection has no bridge, and its refusal of a fixed-layout
    op is shaped fixed; a connection joined to no served document is answered
    empty;
  - on the desktop, the document carries the write.
"""

import os
import struct
import sys
import threading
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wsclient  # noqa: E402

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SandboxBridgeServe"
OTHER = "SandboxBridgeOther"
EDIT = "c2-bridge-edit"
VIEW = "c2-bridge-view"
RUN_WAIT_S = 120

state = {"done": False, "thread": None, "results": [], "t0": time.perf_counter()}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail != "" else ""))
    return cond


# ---- just enough CBOR for the sandbox wire (FcxWire.h) ----


def cbor_encode(value):
    out = bytearray()

    def head(major, n):
        if n < 24:
            out.append(major << 5 | n)
        elif n < 0x100:
            out.extend((major << 5 | 24, n))
        elif n < 0x10000:
            out.append(major << 5 | 25)
            out.extend(struct.pack(">H", n))
        elif n < 1 << 32:
            out.append(major << 5 | 26)
            out.extend(struct.pack(">I", n))
        else:
            out.append(major << 5 | 27)
            out.extend(struct.pack(">Q", n))

    def enc(v):
        if v is None:
            out.append(0xF6)
        elif v is True:
            out.append(0xF5)
        elif v is False:
            out.append(0xF4)
        elif isinstance(v, int):
            if v >= 0:
                head(0, v)
            else:
                head(1, -v - 1)
        elif isinstance(v, float):
            out.append(0xFB)
            out.extend(struct.pack(">d", v))
        elif isinstance(v, str):
            b = v.encode()
            head(3, len(b))
            out.extend(b)
        elif isinstance(v, bytes):
            head(2, len(v))
            out.extend(v)
        elif isinstance(v, (list, tuple)):
            head(4, len(v))
            for item in v:
                enc(item)
        elif isinstance(v, dict):
            head(5, len(v))
            for k, item in v.items():
                enc(k)
                enc(item)
        else:
            raise TypeError(type(v))

    enc(value)
    return bytes(out)


def cbor_decode(data):
    pos = [0]

    def take(n):
        s = data[pos[0] : pos[0] + n]
        if len(s) != n:
            raise ValueError("truncated CBOR")
        pos[0] += n
        return s

    def arg(ai):
        if ai < 24:
            return ai
        size = {24: 1, 25: 2, 26: 4, 27: 8}[ai]
        return int.from_bytes(take(size), "big")

    def dec():
        ib = take(1)[0]
        major, ai = ib >> 5, ib & 31
        if major == 7:
            if ai == 20:
                return False
            if ai == 21:
                return True
            if ai in (22, 23):
                return None
            fmt = {25: ">e", 26: ">f", 27: ">d"}[ai]
            return struct.unpack(fmt, take(struct.calcsize(fmt)))[0]
        n = arg(ai)
        if major == 0:
            return n
        if major == 1:
            return -1 - n
        if major == 2:
            return take(n)
        if major == 3:
            return take(n).decode()
        if major == 4:
            return [dec() for _ in range(n)]
        if major == 5:
            return {dec(): dec() for _ in range(n)}
        return dec()  # a tag: its value

    return dec()


# ---- the bridge wire (SceneServer.h, SceneBridgeRequest) ----


def bridge_frame(kind, seq, payload=b""):
    return b"S" + bytes([kind]) + struct.pack("<I", seq) + payload


def fixed_get_attr(handle, name):
    b = name.encode()
    return bytes([0xF1, 2]) + struct.pack("<Q", handle) + struct.pack("<H", len(b)) + b


class Bridge:
    def __init__(self, port, token, doc):
        self.ws = wsclient.WS(port, "/scene?token=%s&doc=%s" % (token, doc))
        self.seq = 0
        self.stray = []

    def send(self, payload):
        self.seq += 1
        self.ws.send(2, bridge_frame(0, self.seq, payload))
        return self.seq

    def answer(self, seq, timeout=20):
        deadline = time.perf_counter() + timeout
        while True:
            left = deadline - time.perf_counter()
            if left <= 0:
                return None
            m = self.ws.recv(left)
            if m is None:
                return None
            opcode, data = m
            if opcode == 8:
                raise RuntimeError("the server closed the socket")
            if opcode == 2 and data[:4] == b"FCSB":
                got = struct.unpack("<I", data[4:8])[0]
                if got == seq:
                    return data[8:]
                self.stray.append(got)

    def raw(self, payload):
        return self.answer(self.send(payload))

    def op(self, request):
        reply = self.raw(cbor_encode(request))
        if reply is None:
            return {"ok": False, "exc": "Timeout", "msg": "no answer"}
        return cbor_decode(reply)

    def end(self):
        self.ws.send(2, bridge_frame(1, 0))

    def close(self):
        try:
            self.ws.sock.close()
        except OSError:
            pass


def is_handle(reply):
    val = reply.get("val") if isinstance(reply, dict) else None
    return bool(reply.get("ok")) and isinstance(val, dict) and val.get("t") == "h"


def refused(reply, exc):
    return isinstance(reply, dict) and not reply.get("ok") and reply.get("exc") == exc


def client(port):
    add = lambda name, cond, detail="": state["results"].append((name, bool(cond), detail))
    b = Bridge(port, EDIT, DOC)
    try:
        ad = b.op({"op": "active_doc"})
        add("active_doc answers the served document as a handle", is_handle(ad), ad)
        if not is_handle(ad):
            return
        doc_h = ad["val"]["id"]

        fixed = b.raw(fixed_get_attr(doc_h, "Name"))
        want = DOC.encode()
        add(
            "a fixed-layout get_attr is answered in the fixed layout",
            fixed is not None
            and fixed[:2] == b"\xf2\x04"
            and struct.unpack("<I", fixed[2:6])[0] == len(want)
            and fixed[6:] == want,
            fixed,
        )

        box = b.op({"op": "call", "h": doc_h, "m": "getObject", "a": ["Box"]})
        add("a call on the document returns an object", is_handle(box), box)
        if not is_handle(box):
            return
        box_h = box["val"]["id"]

        w = b.op({"op": "write_prop", "h": box_h, "a": "Length", "v": 25.0})
        add("write_prop lands on the served document's object", w.get("ok"), w)
        rd = b.op({"op": "read_prop", "h": box_h, "a": "Length"})
        val = rd.get("val") if rd.get("ok") else None
        add(
            "read_prop reads the write back",
            isinstance(val, dict) and val.get("t") == "quantity" and val.get("v") == 25,
            rd,
        )

        seqs = [b.send(cbor_encode({"op": "get_attr", "h": box_h, "a": "Name"})) for _ in range(3)]
        answers = [b.answer(s) for s in seqs]
        names = [cbor_decode(a).get("val") if a else None for a in answers]
        add(
            "ops sent back to back are answered in order",
            names == ["Box"] * 3 and not b.stray,
            "%s stray=%s" % (names, b.stray),
        )

        b.end()
        stale = b.op({"op": "get_attr", "h": box_h, "a": "Name"})
        add("the end of a statement releases its handles", not stale.get("ok"), stale)
        res = b.op({"op": "resolve", "a": [DOC, "Box"]})
        add(
            "a durable key re-resolves after the end",
            res.get("ok") and isinstance(res.get("val"), int),
            res,
        )
        if res.get("ok"):
            again = b.op({"op": "get_attr", "h": res["val"], "a": "Name"})
            add("the re-resolved handle answers", again.get("val") == "Box", again)

        nd = b.op({"op": "app.new_doc", "a": ["SandboxBridgeNope", "", True]})
        add("app.new_doc is refused to the document principal", refused(nd, "PermissionError"), nd)
        fo = b.op({"op": "app.doc", "a": OTHER})
        add("another open document is out of reach", refused(fo, "PermissionError"), fo)
        bad = b.raw(b"\xff\x00garbage")
        badr = cbor_decode(bad) if bad else None
        add("an undecodable request is a ProtocolError", refused(badr or {}, "ProtocolError"), badr)
    finally:
        b.close()

    v = Bridge(port, VIEW, DOC)
    try:
        va = v.op({"op": "active_doc"})
        add("a view-only connection has no bridge", refused(va, "PermissionError"), va)
        vf = v.raw(fixed_get_attr(1, "Name"))
        vfr = cbor_decode(vf[2:]) if vf and len(vf) > 2 else None
        add(
            "its refusal of a fixed-layout op is shaped fixed",
            vf is not None and vf[:2] == b"\xf2\x00" and refused(vfr or {}, "PermissionError"),
            vf,
        )
    finally:
        v.close()

    n = Bridge(port, EDIT, "NoSuchServedDocument")
    try:
        empty = n.raw(cbor_encode({"op": "active_doc"}))
        add("a connection joined to no served document is answered empty", empty == b"", empty)
    finally:
        n.close()


def run_client(port):
    try:
        client(port)
    except Exception:
        state["results"].append(("the client ran", False, traceback.format_exc()))


def build():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("AutoSaveTimeout", 0)
        doc = FreeCAD.newDocument(DOC, hidden=True)
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        FreeCAD.newDocument(OTHER, hidden=True)
        port = wsclient.free_port()
        if not check(
            "the document is served headless", FreeCADGui.serveDocument(doc, port), "port %d" % port
        ):
            finish()
            return
        FreeCADGui.serveSetGrants([{"token": EDIT}, {"token": VIEW, "access": 1}])
        # The client blocks on its socket; the bridge ops it sends run on
        # this (the GUI) thread, which must stay free to answer them.
        state["thread"] = threading.Thread(target=run_client, args=(port,), daemon=True)
        state["thread"].start()
        QtCore.QTimer.singleShot(100, poll)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def poll():
    if state["thread"].is_alive():
        if time.perf_counter() - state["t0"] > RUN_WAIT_S:
            check("the client finished", False, "still running after %ds" % RUN_WAIT_S)
            finish()
            return
        QtCore.QTimer.singleShot(100, poll)
        return
    verify()


def verify():
    for name, cond, detail in state["results"]:
        check(name, cond, detail)
    doc = FreeCAD.getDocument(DOC)
    box = doc.getObject("Box")
    check("the desktop's document carries the write", box.Length.Value == 25, box.Length)
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.serveStop()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(0, build)
