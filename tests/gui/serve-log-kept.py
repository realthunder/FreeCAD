"""Clients of a served document keep in its file the values a recompute
wrote, and let them go -- each within what it may of the branch.

docs/TransactionLog.md sec 32.27, the fourth stage of the log's panel in a
browser. A value a recompute wrote -- a shape -- is left out of the history
a save writes unless a kept version reaches it; one kept is written
(sec 32.11). The ops (Gui/SceneLog.cpp):

  log.kept        each value kept by itself, each object kept
  log.keep        the value of an object at a row kept, or let go
  log.keepObject  every value of an object kept, or no longer
  log.release     a value kept by itself let go, by its ref

Keeping takes nothing, and is anyone's who is listed the row. Letting a
value go is the host's, and of one who writes the branch of the row it was
kept at; one whose row is gone, the host's. Stopping an object's being kept
lets its values go on every branch: the host's.

A document served with no window. Client A may edit, and reads the branch
the document is on; client B came by a link that may only look; client H
is the host. Asserted:

  - nothing is kept at first; an op says nothing of being kept;
  - A keeps the cut's shape at a row: one value; its op then says so, the
    host's document lists it, and A is told why it may not let it go --
    and is refused letting it go both ways;
  - A keeps every value of the box: listed with each value the log has of
    it; a second time changes nothing; A may not stop it;
  - A keeps the two objects as they are at the first row, as a version's
    "the selected objects" does; an object, a row or a ref there is none
    of is said so;
  - H's keep is told to A (`log.changed`);
  - H lets go of the value by its ref, stops keeping the box, and lets go
    of the two objects at the first row: nothing is kept;
  - given to write the branch, A lets go of a value it kept;
  - a value kept whose row a trim took is listed to A as gone, and is the
    host's to let go: A is refused, H lets it go;
  - B is refused all four.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set and this
directory on PYTHONPATH.
"""
import json
import math
import os
import queue
import tempfile
import threading
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

import wsclient
from wsclient import WS, free_port

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ServeLogKept"
VIEW_TOKEN = "serve-log-kept-look"
HOST = "host@example.com"

EYE = (5.0, 5.0, 120.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 400.0
VW, VH = 800, 600

ACTS = ("log.kept", "log.keep", "log.keepObject", "log.release")

state = {"done": False}
asked = queue.Queue()
finished = threading.Event()
client_done = threading.Event()


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def on_host(fn, timeout=30.0):
    """Run `fn` on the GUI thread, from the client's thread; its result."""
    box = {"done": threading.Event()}
    asked.put((fn, box))
    if not box["done"].wait(timeout):
        raise RuntimeError("the host did not answer")
    if "error" in box:
        raise RuntimeError(box["error"])
    return box.get("result")


def serve_asked():
    while True:
        try:
            fn, box = asked.get_nowait()
        except queue.Empty:
            break
        try:
            box["result"] = fn()
        except Exception:
            box["error"] = traceback.format_exc()
        box["done"].set()
    if client_done.is_set():
        verify()
    elif not state["done"]:
        QtCore.QTimer.singleShot(20, serve_asked)


def kept_on_host():
    """What the host's document says is kept."""
    doc = FreeCAD.getDocument(DOC)
    return {"values": sorted(k["ref"] for k in doc.getKeptTransactionValues()),
            "objects": list(doc.getKeptTransactionObjects())}


def ids():
    doc = FreeCAD.getDocument(DOC)
    return {name: doc.getObject(name).ID for name in ("Box", "Cut")}


def grant_write():
    doc = FreeCAD.getDocument(DOC)
    doc.setTransactionAccess("verified", wsclient.IDENTITY, "main", True)
    return [(a["name"], a["branch"], a["write"]) for a in doc.getTransactionAccess()]


def save_and_trim():
    """A version at the head, and the history before it taken out."""
    doc = FreeCAD.getDocument(DOC)
    doc.save()
    trimmed = doc.trimTransactionBranch("main")
    return {"trimmed": trimmed, "rows": [r["name"] for r in doc.getTransactionLog()]}


def log_pushes(pushes):
    out = []
    for raw in pushes:
        try:
            msg = json.loads(raw.decode("utf-8"))
        except Exception:
            continue
        if msg.get("op") == "log.changed":
            out.append(msg.get("doc"))
    return out


def ask(ws, payload):
    raw = ws.op(json.dumps(payload))
    return json.loads(raw.decode("utf-8")) if raw else None


class Client(threading.Thread):
    """A, who may edit; H, the host; B, who may only look."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.got = {}
        self.n = 100

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()
        client_done.set()

    def do(self, ws, op, **fields):
        self.n += 1
        return ask(ws, dict({"id": self.n, "op": op}, **fields))

    def join(self, label, identity=None, token=None):
        headers = None if identity is None else "%s: %s\r\n" % (wsclient.IDENTITY_HEADER,
                                                                 identity)
        if token:
            ws = WS(self.port, "/scene?token=%s" % token, headers="")
        else:
            ws = WS(self.port, headers=headers)
        ws.hello(label)
        if ws.next_binary(20.0) is None:
            raise RuntimeError("no snapshot for %s" % label)
        ws.next_binary(0.5)
        ws.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH))
        ws.drain(0.5)
        return ws

    def told(self, ws, mark, seconds=3.0):
        end = time.perf_counter() + seconds
        while time.perf_counter() < end and not log_pushes(ws.pushes[mark:]):
            ws.drain(0.2)
        return log_pushes(ws.pushes[mark:])

    def talk(self):
        got = self.got
        ws = self.join("serve-log-kept-a")
        hs = self.join("serve-log-kept-h", identity=HOST)
        bs = self.join("serve-log-kept-b", token=VIEW_TOKEN)
        got["ids"] = on_host(ids)

        # What is listed, and what nothing is kept of yet
        got["rows"] = self.do(ws, "log.rows")
        by_name = {r["name"]: r["seq"] for r in (got["rows"] or {}).get("rows", [])}
        got["by_name"] = by_name
        taller, base = by_name.get("taller", 0), by_name.get("base", 0)
        got["kept0"] = self.do(ws, "log.kept")
        got["ops0"] = self.do(ws, "log.ops", row=taller)

        # A keeps the cut's shape at a row, and cannot let it go
        got["keep"] = self.do(ws, "log.keep", obj="Cut", prop="Shape", row=taller)
        got["ops1"] = self.do(ws, "log.ops", row=taller)
        got["kept1"] = self.do(ws, "log.kept")
        got["host1"] = on_host(kept_on_host)
        got["letgo_a"] = self.do(ws, "log.keep", obj="Cut", prop="Shape", row=taller,
                                 keep=False)
        ref = next((v["ref"] for v in (got["kept1"] or {}).get("values", [])), "")
        got["ref"] = ref
        got["release_a"] = self.do(ws, "log.release", ref=ref)

        # Every value of the box; twice; not A's to stop
        got["keepobj"] = self.do(ws, "log.keepObject", obj="Box")
        got["keepobj2"] = self.do(ws, "log.keepObject", obj="Box")
        got["kept2"] = self.do(ws, "log.kept")
        got["ops2"] = self.do(ws, "log.ops", row=taller)
        got["stopobj_a"] = self.do(ws, "log.keepObject", obj="Box", keep=False)

        # The two as they are at the first row, as a version's menu does
        got["keepsel"] = self.do(ws, "log.keep", objects=["Box", "Cut"], row=base)
        got["kept3"] = self.do(ws, "log.kept")
        got["unknown_obj"] = self.do(ws, "log.keep", objects=["Box", "Nothing"], row=base)
        got["unknown_obj1"] = self.do(ws, "log.keep", obj="Nothing", row=base)
        got["unknown_row"] = self.do(ws, "log.keep", obj="Cut", row=999999)
        got["unknown_ref"] = self.do(ws, "log.release", ref="0" * 40)

        # The host's acts: told to A; and what A could not
        mark = len(ws.pushes)
        got["h_release"] = self.do(hs, "log.release", ref=ref)
        got["told"] = self.told(ws, mark)
        got["h_stopobj"] = self.do(hs, "log.keepObject", cid=got["ids"]["Box"], keep=False)
        got["h_letgosel"] = self.do(hs, "log.keep", objects=["Box", "Cut"], row=base,
                                    keep=False)
        got["host2"] = on_host(kept_on_host)
        got["kept4"] = self.do(ws, "log.kept")

        # Given to write the branch, A lets go of what it kept
        got["granted"] = on_host(grant_write)
        got["keep_w"] = self.do(ws, "log.keep", obj="Cut", prop="Shape", row=taller)
        got["kept5"] = self.do(ws, "log.kept")
        got["letgo_w"] = self.do(ws, "log.keep", obj="Cut", prop="Shape", row=taller,
                                 keep=False)
        got["host3"] = on_host(kept_on_host)

        # A value past its row: listed as gone, the host's to let go
        got["keep_t"] = self.do(ws, "log.keep", obj="Cut", prop="Shape", row=taller)
        got["trim"] = on_host(save_and_trim)
        got["kept6"] = self.do(ws, "log.kept")
        got["release_t_a"] = self.do(ws, "log.release", ref=ref)
        got["release_t_h"] = self.do(hs, "log.release", ref=ref)
        got["host4"] = on_host(kept_on_host)

        # One who may only look
        for n, op in enumerate(ACTS):
            got["b:" + op] = self.do(bs, op, obj="Cut", ref=ref, row=taller)
        for s in (ws, hs, bs):
            s.close()


def build():
    try:
        p = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
        state["log"] = p.GetInt("TransactionLog", 2)
        p.SetInt("TransactionLog", 2)
        p.SetInt("AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        doc = FreeCAD.newDocument(DOC, hidden=True)
        doc.UndoMode = 1
        doc.openTransaction("base")
        box = doc.addObject("Part::Box", "Box")
        cyl = doc.addObject("Part::Cylinder", "Cyl")
        cyl.Radius, cyl.Height = 2, 60
        cyl.Placement.Base = FreeCAD.Vector(5, 5, -10)
        cut = doc.addObject("Part::Cut", "Cut")
        cut.Base, cut.Tool = box, cyl
        doc.recompute()
        doc.commitTransaction()
        doc.saveAs(os.path.join(tempfile.mkdtemp(prefix="fc-serve-log-kept-"), "log.FCStd"))
        for name, prop, value in (("taller", "Height", 16), ("wider", "Length", 14)):
            doc.openTransaction(name)
            setattr(box, prop, value)
            doc.recompute()
            doc.commitTransaction()

        port = free_port()
        if not check("the document is served with no window",
                     FreeCADGui.serveDocument(doc, port), "port %d" % port):
            finish()
            return
        FreeCADGui.serveSetGrants([{"token": VIEW_TOKEN, "access": 1},
                                   {"identity": HOST, "access": 3}, {"access": 0}])
        client = Client(port)
        state["client"] = client
        QtCore.QTimer.singleShot(20, serve_asked)
        client.start()
        QtCore.QTimer.singleShot(180000, lambda: (check("the run ended", state["done"]), finish()))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
        finish()


def code(reply):
    return (reply or {}).get("code")


def ok(reply):
    return (reply or {}).get("ok") is True


def op_of(reply, obj, prop="Shape"):
    return next((o for o in (reply or {}).get("ops", [])
                 if o.get("obj") == obj and o.get("prop") == prop), {})


def check_all(got):
    ids_ = got.get("ids") or {}
    by_name = got.get("by_name") or {}
    taller = by_name.get("taller", 0)
    check("the rows are listed", taller > 0 and by_name.get("base", 0) > 0, by_name)

    k0 = got.get("kept0") or {}
    check("nothing is kept at first",
          ok(k0) and k0.get("count") == 0 and k0.get("values") == [] and k0.get("objects") == [],
          k0)
    cut0 = op_of(got.get("ops0"), "Cut")
    check("the cut's shape op says nothing of being kept",
          cut0.get("derived") is True and "keptValue" not in cut0 and "keptObject" not in cut0,
          cut0)

    keep = got.get("keep") or {}
    check("A keeps the cut's shape at a row: one value",
          ok(keep) and keep.get("values") == 1 and keep.get("row") == taller, keep)
    cut1 = op_of(got.get("ops1"), "Cut")
    check("its op then says it is kept", cut1.get("keptValue") is True, cut1)
    k1 = got.get("kept1") or {}
    v1 = (k1.get("values") or [{}])[0]
    check("it is listed: the object, the property, the row",
          k1.get("count") == 1 and v1.get("obj") == "Cut" and v1.get("prop") == "Shape"
          and v1.get("seq") == taller and v1.get("row") is True and v1.get("here") is True
          and v1.get("ref") == cut1.get("after"), k1)
    check("and A is told it may not let it go, and why",
          v1.get("release") is False and "write" in (v1.get("why") or ""), v1)
    check("the host's document keeps it", (got.get("host1") or {}).get("values") == [v1.get("ref")],
          got.get("host1"))
    check("A is refused letting it go by its row",
          code(got.get("letgo_a")) == "Forbidden", got.get("letgo_a"))
    check("and by its ref", code(got.get("release_a")) == "Forbidden", got.get("release_a"))

    ko, ko2 = got.get("keepobj") or {}, got.get("keepobj2") or {}
    check("A keeps every value of the box; a second time changes nothing",
          ok(ko) and ko.get("changed") is True and ko.get("cid") == ids_.get("Box")
          and ok(ko2) and ko2.get("changed") is False, (ko, ko2))
    k2 = got.get("kept2") or {}
    o2 = (k2.get("objects") or [{}])[0]
    check("the box is listed with each value the log has of it",
          k2.get("count") == 2 and o2.get("obj") == "Box" and o2.get("cid") == ids_.get("Box")
          and len(o2.get("values") or []) >= 3
          and all(v.get("prop") for v in o2.get("values") or []), k2)
    check("and not A's to stop", o2.get("release") is False and o2.get("why"), o2)
    box2 = op_of(got.get("ops2"), "Box")
    check("an op of the box says all of it is kept", box2.get("keptObject") is True, box2)
    check("A is refused stopping it", code(got.get("stopobj_a")) == "Forbidden",
          got.get("stopobj_a"))

    ks = got.get("keepsel") or {}
    check("A keeps the two objects as they are at the first row",
          ok(ks) and ks.get("values", 0) >= 2, ks)
    k3 = got.get("kept3") or {}
    seqs = sorted(v.get("seq") for v in k3.get("values") or [])
    check("listed by themselves at that row, beside the first",
          len(seqs) >= 3 and seqs.count(by_name.get("base")) >= 2 and taller in seqs, k3)
    under = [v for v in ((k3.get("objects") or [{}])[0].get("values") or [])
             if v.get("seq") == by_name.get("base")]
    check("under the box, its value at that row is kept by itself too, not A's to let go",
          len(under) == 1 and under[0].get("alone") is True and under[0].get("release") is False
          and "write" in (under[0].get("why") or ""), under)
    check("an object, a row or a ref there is none of is said so",
          code(got.get("unknown_obj")) == "UnknownObject"
          and code(got.get("unknown_obj1")) == "UnknownObject"
          and code(got.get("unknown_row")) == "UnknownRow"
          and code(got.get("unknown_ref")) == "UnknownValue",
          [code(got.get(k)) for k in ("unknown_obj", "unknown_obj1", "unknown_row",
                                      "unknown_ref")])

    check("the host lets go of the value by its ref", ok(got.get("h_release")),
          got.get("h_release"))
    check("and A is told", DOC in (got.get("told") or []), got.get("told"))
    hs = got.get("h_stopobj") or {}
    check("the host stops keeping the box", ok(hs) and hs.get("changed") is True, hs)
    hl = got.get("h_letgosel") or {}
    check("and lets go of the two at the first row",
          ok(hl) and hl.get("values") == ks.get("values"), (hl, ks))
    check("nothing is kept now", got.get("host2") == {"values": [], "objects": []}
          and (got.get("kept4") or {}).get("count") == 0, (got.get("host2"), got.get("kept4")))

    check("A is given to write the branch",
          (wsclient.IDENTITY, "main", True) in [tuple(a) for a in got.get("granted") or []],
          got.get("granted"))
    v5 = ((got.get("kept5") or {}).get("values") or [{}])[0]
    check("and may let go of what it keeps", ok(got.get("keep_w")) and v5.get("release") is True,
          v5)
    lw = got.get("letgo_w") or {}
    check("and does", ok(lw) and lw.get("values") == 1
          and (got.get("host3") or {}).get("values") == [], (lw, got.get("host3")))

    trim = got.get("trim") or {}
    check("kept, saved, and its row trimmed away",
          ok(got.get("keep_t")) and trim.get("trimmed", 0) > 0
          and "taller" not in (trim.get("rows") or []), trim)
    v6 = ((got.get("kept6") or {}).get("values") or [{}])[0]
    check("it is listed to A as gone, the host's to let go",
          v6.get("ref") == got.get("ref") and v6.get("row") is False
          and v6.get("release") is False and "host" in (v6.get("why") or ""), v6)
    check("A is refused letting it go", code(got.get("release_t_a")) == "Forbidden",
          got.get("release_t_a"))
    check("the host lets it go", ok(got.get("release_t_h"))
          and (got.get("host4") or {}).get("values") == [],
          (got.get("release_t_h"), got.get("host4")))

    check("B is refused all four",
          all(code(got.get("b:" + op)) == "ViewOnly" for op in ACTS),
          [code(got.get("b:" + op)) for op in ACTS])


def verify():
    if state["done"]:
        return
    try:
        client = state["client"]
        finished.set()
        if client.error:
            check("the client ran", False, client.error)
        check_all(client.got)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    finished.set()
    try:
        if "log" in state:
            FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
                "TransactionLog", state["log"])
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, build)
