"""A sandbox guest's bridge ops reach the served document over the socket.

docs/Sandbox.md 7.20, C2 and C3. A guest in a viewer's page reaches back through its
own WebSocket: 'S' frames carry the bridge requests exactly as the guest writes
them, the host's per-connection endpoint (src/Gui/SandboxRemote.cpp) dispatches
them into the same bridge ops the desktop's guest reaches, and the answers come
back as 'FCSB' frames (SceneServer.h, SceneBridgeRequest). This drives that
wire from a plain socket client on a worker thread -- no browser, no guest --
against a headless serve with three token grants, two editing and one view-only;
tests/gui/sandbox-bridge-browser.py is the leg with a real guest.

What is asserted:

  - active_doc answers the served document as a handle, and the document is
    the table's owner: a fixed-layout get_attr of its Name is answered in the
    fixed layout;
  - a call on the document returns an object; write_prop on it lands and
    read_prop reads it back; ops sent back to back are answered in order;
  - the end of a statement releases its handles (a stale id is refused), and
    a durable key re-resolves afterwards;
  - the client principal holds (C3): app.new_doc is refused, not promptable,
    another open document is out of reach, a command by name is refused, not
    promptable, saveAs to a path is refused, an undecodable request is a
    ProtocolError; Part.makeBox bound to a new object lands;
  - a view-only connection gets a read-only bridge: it reads, its write is
    refused, and an error on a fixed-layout op is shaped fixed; a connection
    joined to no served document is answered empty;
  - two clients admitted by different grants, their ops interleaved, are each
    refused under their own principal;
  - audit.log names the client -- client:grant:<id> and the connection's
    context, view-only marked, and client:id:<identity> for a connection a
    trusted front door vouched for (FC_SERVE_TRUST_PROXY, loopback);
  - on the desktop, the document carries the write and the new object, and a
    host call after the run is not held to any client's scope.
"""

import json
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
EDIT2 = "c3-bridge-edit-two"
IDENTITY = "carol@example.com"
# The door believes a front door's identity header only with trust on and
# a loopback peer; the server reads this when it is first made.
os.environ["FC_SERVE_TRUST_PROXY"] = "1"
VIEW = "c2-bridge-view"
RUN_WAIT_S = 120

state = {
    "done": False,
    "thread": None,
    "results": [],
    "t0": time.perf_counter(),
    "grants": {},
    "audit_at": 0,
}


def audit_path():
    return os.path.join(FreeCAD.getUserAppDataDir(), "security", "audit.log")


def denied(reply, perm):
    """Refused, not promptable, naming the permission and the client."""
    msg = reply.get("msg", "") if isinstance(reply, dict) else ""
    return (
        refused(reply, "PermissionError")
        and ("Permission denied: " + perm) in msg
        and "this client" in msg
    )


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
    def __init__(self, port, token, doc, headers=""):
        self.ws = wsclient.WS(port, "/scene?token=%s&doc=%s" % (token, doc), headers)
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
        add("app.new_doc is refused to the client, not promptable", denied(nd, "app.write"), nd)
        gr = b.op({"op": "gui.cmd.run", "a": ["Std_RecentMacros", 0]})
        add("a command by name is refused to the client, not promptable", denied(gr, "gui"), gr)
        ad2 = b.op({"op": "active_doc"})
        if is_handle(ad2):
            doc2 = ad2["val"]["id"]
            sa = b.op(
                {"op": "call", "h": doc2, "m": "saveAs", "a": [os.path.join(OUT, "stolen.FCStd")]}
            )
            add(
                "saveAs to a path is refused to the client",
                refused(sa, "PermissionError") and "remote client" in sa.get("msg", ""),
                sa,
            )
            mb = b.op({"op": "mod_call", "m": "Part.makeBox", "a": [10, 10, 10]})
            add("Part.makeBox answers a shape handle", is_handle(mb), mb)
            ao = b.op(
                {"op": "call", "h": doc2, "m": "addObject", "a": ["Part::Feature", "ClientBox"]}
            )
            add("the client adds an object", is_handle(ao), ao)
            if is_handle(mb) and is_handle(ao):
                ws = b.op({"op": "write_prop", "h": ao["val"]["id"], "a": "Shape", "v": mb["val"]})
                add("the box is bound to the new object", ws.get("ok"), ws)
        else:
            add("active_doc answers again", False, ad2)
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
        add("a view-only connection gets a bridge", is_handle(va), va)
        if is_handle(va):
            vb = v.op({"op": "call", "h": va["val"]["id"], "m": "getObject", "a": ["Box"]})
            vr = (
                v.op({"op": "read_prop", "h": vb["val"]["id"], "a": "Length"})
                if is_handle(vb)
                else vb
            )
            add("it reads", vr.get("ok"), vr)
            if is_handle(vb):
                vw = v.op({"op": "write_prop", "h": vb["val"]["id"], "a": "Length", "v": 99.0})
                add("its write is refused, not promptable", denied(vw, "doc.write.self"), vw)
        vf = v.raw(fixed_get_attr(987654321, "Name"))
        vfr = cbor_decode(vf[2:]) if vf and len(vf) > 2 else None
        add(
            "an error on a fixed-layout op is shaped fixed",
            vf is not None and vf[:2] == b"\xf2\x00" and refused(vfr or {}, "ReferenceError"),
            vf,
        )
    finally:
        v.close()

    # Two clients admitted by different grants, ops interleaved on the
    # wire: each is answered, and each refusal is its own principal's.
    one = Bridge(port, EDIT, DOC)
    two = Bridge(port, EDIT2, DOC)
    try:
        s1 = one.send(cbor_encode({"op": "app.new_doc", "a": ["SandboxBridgeOne", "", True]}))
        s2 = two.send(cbor_encode({"op": "app.new_doc", "a": ["SandboxBridgeTwo", "", True]}))
        t1 = one.send(cbor_encode({"op": "active_doc"}))
        t2 = two.send(cbor_encode({"op": "active_doc"}))
        r = [
            cbor_decode(x) if x else {}
            for x in (one.answer(s1), two.answer(s2), one.answer(t1), two.answer(t2))
        ]
        add(
            "interleaved clients are each answered",
            denied(r[0], "app.write")
            and denied(r[1], "app.write")
            and is_handle(r[2])
            and is_handle(r[3]),
            r,
        )
    finally:
        one.close()
        two.close()

    who = Bridge(port, EDIT, DOC, "X-Forwarded-Email: %s\r\n" % IDENTITY)
    try:
        wr = who.op({"op": "gui.cmd.run", "a": ["Std_RecentFiles", 0]})
        add("a vouched-for client is refused a command too", denied(wr, "gui"), wr)
    finally:
        who.close()

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
        FreeCADGui.serveSetGrants([{"token": EDIT}, {"token": EDIT2}, {"token": VIEW, "access": 1}])
        state["grants"] = {g["token"]: g["id"] for g in FreeCADGui.serveGrants()}
        try:
            state["audit_at"] = os.path.getsize(audit_path())
        except OSError:
            state["audit_at"] = 0
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
    cb = doc.getObject("ClientBox")
    check(
        "the desktop's document carries the client's box",
        cb is not None and abs(cb.Shape.Volume - 1000) < 1e-6,
        cb.Shape.Volume if cb else None,
    )
    check("no client created a document", "SandboxBridgeNope" not in FreeCAD.listDocuments())

    lines = []
    try:
        with open(audit_path(), "rb") as f:
            f.seek(state["audit_at"])
            for raw in f.read().decode("utf-8", "replace").splitlines():
                try:
                    lines.append(json.loads(raw))
                except ValueError:
                    pass
    except OSError as e:
        check("audit.log is readable", False, e)
    g = state["grants"]

    def logged(principal, perm, decision, context=""):
        return any(
            l.get("principal") == principal
            and l.get("permission") == perm
            and l.get("decision") == decision
            and context in l.get("context", "")
            for l in lines
        )

    edit, edit2, view = ("client:grant:%s" % g.get(t) for t in (EDIT, EDIT2, VIEW))
    check(
        "audit.log: the view-only write under its client principal",
        logged(view, "doc.write.self", "deny", "view-only")
        and logged(view, "doc.write.self", "deny", DOC),
        [l for l in lines if l.get("principal") == view],
    )
    check(
        "audit.log: the command refused under the client", logged(edit, "gui", "deny", " client #")
    )
    check(
        "audit.log: the interleaved refusals under each client's own principal",
        logged(edit, "app.write", "deny") and logged(edit2, "app.write", "deny") and edit != edit2,
        [l.get("principal") for l in lines],
    )
    check(
        "audit.log: a vouched-for client under client:id:<identity>",
        logged("client:id:" + IDENTITY, "gui", "deny", " client #"),
        [l.get("principal") for l in lines],
    )
    # the desktop's own calls are host code again: no scope left behind
    try:
        FreeCAD.newDocument("SandboxBridgeAfter", hidden=True)
        after = "SandboxBridgeAfter" in FreeCAD.listDocuments()
    except Exception as e:
        after = e
    check("a host call after the run is under no client's scope", after is True, after)
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
