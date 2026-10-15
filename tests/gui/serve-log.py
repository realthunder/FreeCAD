"""A client of a served document reads its transaction log, and is shown a
row's change.

docs/TransactionLog.md sec 32.21. The desktop's panel of the log is a dock
of the host's window; a client has a panel of its own and asks the host
for what it lists, with three ops (Gui/SceneLog.cpp):

  log.rows   the rows, newest first, a page at a time
  log.ops    the ops of one row
  log.value  one value by its ref

and is told `log.changed` when the log has other rows. Seeing the log needs
the right to edit (ruled 2026-10-13). What a row made of an object is shown
as the edit it is, Gui::ViewProvider::ValueDiff: the `edit` op with the row
named.

A document served with no window. Client A may edit and states a camera;
client B came by a link that may only look. Asserted:

  - A is listed the rows the host has, newest first, with who wrote them
    and how many ops each has; a page at a time they are the same rows,
    none twice and none missed;
  - the ops of a row: the property set, with the refs of its value before
    and after; the shape that followed, which its view can show; an object
    that is gone since, by the name the log has;
  - a value by its ref, whole, and the start of one that is long;
  - a row the host commits is said to A, who asks for what came after;
  - B is refused all three, ViewOnly, and is told of no row;
  - the `edit` op with a row named shows that row's change, with none the
    last one, and of a row that did not change the object it is refused --
    and the row named is spent by it;
  - none of the asking wrote a row.

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
from PySide import QtCore, QtWidgets

import wsclient
from wsclient import WS, free_port

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ServeLog"
MODE = 6  # Gui::ViewProvider::ValueDiff
VIEW_TOKEN = "serve-log-look"

EYE = (5.0, 5.0, 120.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 400.0
VW, VH = 800, 600

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
    """Run `fn` on the GUI thread, from a client's thread; its result."""
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


def host_rows():
    """The rows as the host has them: (seq, name, kind, ops), oldest first."""
    doc = FreeCAD.getDocument(DOC)
    return [(r["seq"], r["name"], r["kind"], len(doc.getTransactionOps(r["seq"])))
            for r in doc.getTransactionLog()]


def shown():
    """What the document is in the edit of, and what its panel says."""
    gdoc = FreeCADGui.getDocument(DOC)
    cut = FreeCAD.getDocument(DOC).getObject("Cut")
    what = None
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "PartGui::ShapeDiffPanel":
            label = w.findChild(QtWidgets.QLabel, "diffWhat")
            what = label.text() if label else None
    return {"edit": gdoc.getInEdit() is not None, "what": what,
            "found": {k: v for k, v in dict(cut.ViewObject.getValueDiff()).items()
                      if "." not in k}}


def deeper():
    doc = FreeCAD.getDocument(DOC)
    doc.openTransaction("deeper")
    doc.getObject("Box").Width = 12
    doc.recompute()
    doc.commitTransaction()
    return host_rows()


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


class Watcher(threading.Thread):
    """Client B: came by the link that may only look."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.ready = threading.Event()
        self.pushes = []
        self.got = {}
        self.over = threading.Event()

    def run(self):
        try:
            ws = WS(self.port, "/scene?token=%s" % VIEW_TOKEN, headers="")
            ws.hello("serve-log-b")
            ws.next_binary(20.0)
            self.got["rows"] = ask(ws, {"id": 1, "op": "log.rows"})
            self.got["ops"] = ask(ws, {"id": 2, "op": "log.ops", "row": 1})
            self.got["value"] = ask(ws, {"id": 3, "op": "log.value", "ref": "x"})
            self.ready.set()
            while not finished.is_set():
                ws.drain(0.2)
            ws.drain(0.3)
            self.pushes = list(ws.pushes)
            ws.close()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()
        self.over.set()


class Client(threading.Thread):
    """Client A: may edit, states a camera, reads the log."""

    def __init__(self, port, watcher):
        super().__init__(daemon=True)
        self.port = port
        self.watcher = watcher
        self.error = None
        self.got = {}

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()
        client_done.set()

    def talk(self):
        got = self.got
        ws = WS(self.port)
        ws.hello("serve-log-a")
        if ws.next_binary(20.0) is None:
            raise RuntimeError("no snapshot")
        ws.next_binary(0.5)
        ws.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH))
        ws.drain(0.5)
        self.watcher.ready.wait(30.0)
        # Both are in: a connection that may edit wrote its login
        got["host"] = on_host(host_rows)

        got["rows"] = ask(ws, {"id": 10, "op": "log.rows"})
        # A page at a time, to the end
        pages, before = [], None
        for n in range(40):
            req = {"id": 20 + n, "op": "log.rows", "limit": 2}
            if before is not None:
                req["before"] = before
            page = ask(ws, req) or {}
            pages.append(page)
            if not page.get("ok") or not page.get("rows") or not page.get("more"):
                break
            before = page["rows"][-1]["seq"]
        got["pages"] = pages

        by_name = {r["name"]: r["seq"] for r in (got["rows"] or {}).get("rows", [])}
        got["taller"] = ask(ws, {"id": 70, "op": "log.ops", "row": by_name.get("taller", 0)})
        got["gone"] = ask(ws, {"id": 71, "op": "log.ops", "row": by_name.get("gone", 0)})
        got["norow"] = ask(ws, {"id": 72, "op": "log.ops", "row": 999999})
        height = [o for o in (got["taller"] or {}).get("ops", [])
                  if o.get("obj") == "Box" and o.get("prop") == "Height"]
        shape = [o for o in (got["taller"] or {}).get("ops", [])
                 if o.get("obj") == "Cut" and o.get("prop") == "Shape"]
        if height:
            got["value"] = ask(ws, {"id": 73, "op": "log.value", "ref": height[0].get("after")})
            got["value_before"] = ask(ws, {"id": 74, "op": "log.value",
                                           "ref": height[0].get("before")})
        if shape:
            got["long"] = ask(ws, {"id": 75, "op": "log.value", "ref": shape[0].get("after"),
                                   "max": 200})
        got["novalue"] = ask(ws, {"id": 76, "op": "log.value", "ref": "no-such-value"})
        got["host_read"] = on_host(host_rows)

        # A row of the host's, said and asked for
        mark = len(ws.pushes)
        got["host_deeper"] = on_host(deeper)
        end = time.perf_counter() + 5.0
        while time.perf_counter() < end and not log_pushes(ws.pushes[mark:]):
            text = ws.next_text(0.3)
            if text is not None:
                ws.pushes.append(text)
        got["told"] = log_pushes(ws.pushes[mark:])
        last = (got["rows"] or {}).get("last", 0)
        got["after"] = ask(ws, {"id": 80, "op": "log.rows", "after": last})

        # A row's change, as the edit it is
        got["host_before_edit"] = on_host(host_rows)
        got["edit_gone"] = ask(ws, {"id": 90, "op": "edit", "obj": "Cut", "mode": MODE,
                                    "row": by_name.get("gone", 0)})
        got["shown_gone"] = on_host(shown)
        got["edit_last"] = ask(ws, {"id": 91, "op": "edit", "obj": "Cut", "mode": MODE})
        ws.drain(1.0)
        got["shown_last"] = on_host(shown)
        ask(ws, {"id": 92, "op": "resetEdit"})
        ws.drain(0.5)
        got["edit_row"] = ask(ws, {"id": 93, "op": "edit", "obj": "Cut", "mode": MODE,
                                   "row": by_name.get("taller", 0)})
        ws.drain(1.0)
        got["shown_row"] = on_host(shown)
        got["reset"] = ask(ws, {"id": 94, "op": "resetEdit"})
        ws.drain(0.5)
        got["shown_after"] = on_host(shown)
        got["host_after"] = on_host(host_rows)
        ws.close()


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
        cyl.Radius, cyl.Height = 2, 30
        cyl.Placement.Base = FreeCAD.Vector(5, 5, -10)
        cut = doc.addObject("Part::Cut", "Cut")
        cut.Base, cut.Tool = box, cyl
        doc.recompute()
        doc.commitTransaction()
        doc.saveAs(os.path.join(tempfile.mkdtemp(prefix="fc-serve-log-"), "log.FCStd"))
        doc.openTransaction("taller")
        box.Height = 16
        doc.recompute()
        doc.commitTransaction()
        doc.openTransaction("ball")
        doc.addObject("Part::Sphere", "Ball")
        doc.recompute()
        doc.commitTransaction()
        doc.openTransaction("gone")
        doc.removeObject("Ball")
        doc.recompute()
        doc.commitTransaction()
        doc.openTransaction("wider")
        box.Length = 14
        doc.recompute()
        doc.commitTransaction()

        port = free_port()
        if not check("the document is served with no window",
                     FreeCADGui.serveDocument(doc, port), "port %d" % port):
            finish()
            return
        # A link that may only look, beside the door that may edit
        FreeCADGui.serveSetGrants([{"token": VIEW_TOKEN, "access": 1}, {"access": 0}])
        watcher = Watcher(port)
        client = Client(port, watcher)
        state["watcher"], state["client"] = watcher, client
        QtCore.QTimer.singleShot(20, serve_asked)
        watcher.start()
        client.start()
        QtCore.QTimer.singleShot(150000, lambda: (check("the run ended", state["done"]), finish()))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
        finish()


def verify():
    if state["done"]:
        return
    try:
        client, watcher = state["client"], state["watcher"]
        finished.set()
        watcher.over.wait(5.0)
        got = client.got
        if client.error or watcher.error:
            check("the clients ran", False, client.error or watcher.error)
        host = got.get("host", [])
        rows = got.get("rows") or {}
        listed = rows.get("rows", [])

        check("the client that may edit is listed the rows the host has, newest first",
              rows.get("ok") is True and [r["seq"] for r in listed] == [h[0] for h in host][::-1]
              and rows.get("last") == (host[-1][0] if host else None) and rows.get("more") is False,
              ([r.get("seq") for r in listed], [h[0] for h in host], rows.get("code")))
        check("each with its name, its kind and how many ops it has",
              [(r["seq"], r["name"], r["kind"], r["ops"]) for r in listed] == host[::-1],
              [(r.get("name"), r.get("kind"), r.get("ops")) for r in listed][:6])
        named = {r["name"]: r for r in listed}
        check("and who wrote it: the desktop's user the rows made here",
              all(named.get(n, {}).get("authorKind") == "local" and named[n].get("author")
                  for n in ("base", "taller", "wider")),
              [(n, named.get(n, {}).get("author"), named.get(n, {}).get("authorKind"))
               for n in ("base", "taller", "wider")])
        check("all of them the history the document is at the head of, on its branch",
              all(r.get("here") is True for r in listed) and rows.get("branchName")
              and rows.get("head") == rows.get("last"),
              (rows.get("branchName"), rows.get("head"), rows.get("last")))

        pages = got.get("pages", [])
        paged = [r["seq"] for page in pages for r in page.get("rows", [])]
        check("a page at a time they are the same rows, none twice and none missed",
              paged == [h[0] for h in host][::-1] and all(len(p.get("rows", [])) <= 2
                                                           for p in pages)
              and len(pages) > 2 and pages[-1].get("more") is False,
              (paged, [p.get("more") for p in pages]))

        taller = got.get("taller") or {}
        ops = taller.get("ops", [])
        height = [o for o in ops if o.get("obj") == "Box" and o.get("prop") == "Height"]
        shape = [o for o in ops if o.get("obj") == "Cut" and o.get("prop") == "Shape"]
        check("the ops of a row: the property set, with the refs of its value before and after",
              taller.get("ok") is True and taller.get("name") == "taller" and len(height) == 1
              and height[0].get("op") == "set" and height[0].get("before")
              and height[0].get("after") and height[0].get("here") is True
              and not height[0].get("shows"),
              height[:1] or taller)
        check("and the shape that followed, which its view can show",
              len(shape) >= 1 and shape[0].get("shows") is True and shape[0].get("after"),
              shape[:1])
        gone = [o for o in (got.get("gone") or {}).get("ops", [])
                if o.get("op") == "remove" and o.get("kind") == "obj"]
        check("an object that is gone since is known by the name the log has",
              len(gone) == 1 and gone[0].get("obj") == "Ball" and gone[0].get("here") is False,
              gone)
        check("a row there is none of is said so",
              (got.get("norow") or {}).get("code") == "UnknownRow", got.get("norow"))

        value = got.get("value") or {}
        check("a value by its ref: what the property was set to, whole",
              value.get("ok") is True and "16" in value.get("text", "")
              and value.get("truncated") is False and value.get("bytes") == len(value["text"]),
              value)
        before = got.get("value_before") or {}
        check("and what it was before", before.get("ok") is True and "10" in before.get("text", "")
              and before.get("text") != value.get("text"), before.get("text"))
        long = got.get("long") or {}
        check("of a long one the start of it, and how long it is",
              long.get("ok") is True and long.get("truncated") is True
              and 0 < len(long.get("text", "")) <= 200 and long.get("bytes", 0) > 200,
              (len(long.get("text", "")), long.get("bytes"), long.get("truncated")))
        # Sec 32.35: a shape's text is the start of its geometry's file, and
        # it says what it is: the cut, 10 x 10 x 16 with the hole through it
        facts = long.get("facts") or {}
        check("a shape says what it is: its kind, its counts, its volume, its size",
              long.get("summary", "").startswith("Compound: 1 solid, 1 shell, 7 faces")
              and facts.get("faces") == 7 and facts.get("solids") == 1
              and abs(facts.get("volume", 0) - (1600 - math.pi * 4 * 16)) < 1e-6
              and abs(facts.get("dz", 0) - 16) < 1e-6,
              (long.get("summary"), facts))
        check("a length says nothing more than its text", "summary" not in value, value)
        check("a ref the log has no value of is said so",
              (got.get("novalue") or {}).get("code") == "UnknownValue", got.get("novalue"))
        check("none of the reading wrote a row", got.get("host_read") == host,
              (len(got.get("host_read", [])), len(host)))

        after = got.get("after") or {}
        new = [r["name"] for r in after.get("rows", [])]
        check("a row the host commits is said to the client",
              got.get("told") and got["told"][0] == DOC, got.get("told"))
        check("who asks for what came after, and is listed it and nothing older",
              after.get("ok") is True and "deeper" in new
              and all(r["seq"] > rows.get("last", 0) for r in after.get("rows", []))
              and [r["seq"] for r in after.get("rows", [])]
              == [h[0] for h in got.get("host_deeper", []) if h[0] > rows.get("last", 0)][::-1],
              (new, after.get("last")))

        b = watcher.got
        check("the client that may only look is refused all three",
              all((b.get(k) or {}).get("code") == "ViewOnly" for k in ("rows", "ops", "value")),
              {k: (b.get(k) or {}).get("code") for k in ("rows", "ops", "value")})
        check("and is told of no row", log_pushes(watcher.pushes) == [],
              log_pushes(watcher.pushes))

        check("the change of a row that did not change the object is refused",
              (got.get("edit_gone") or {}).get("ok") is False
              and got.get("shown_gone", {}).get("edit") is False,
              (got.get("edit_gone"), got.get("shown_gone")))
        last = got.get("shown_last", {})
        check("with no row named the edit is of the last change -- the row named before is spent",
              (got.get("edit_last") or {}).get("ok") is True and last.get("edit") is True
              and str(last.get("what", "")).startswith("deeper (row "),
              (got.get("edit_last"), last.get("what")))
        row = got.get("shown_row", {})
        check("with a row named it is of that row's",
              (got.get("edit_row") or {}).get("ok") is True and row.get("edit") is True
              and str(row.get("what", "")).startswith("taller (row %d)" % named["taller"]["seq"]),
              (got.get("edit_row"), row.get("what")))
        check("the taller box: its bottom as it was, the six others changed",
              row.get("found", {}).get("same") == 1
              and row.get("found", {}).get("changed", 0) + row.get("found", {}).get("moved", 0)
              == 6, row.get("found"))
        check("and it is left as any edit is",
              (got.get("reset") or {}).get("ok") is True
              and got.get("shown_after", {}).get("edit") is False
              and got.get("shown_after", {}).get("what") is None, got.get("shown_after"))
        check("none of the showing wrote a row",
              got.get("host_after") == got.get("host_before_edit"),
              (len(got.get("host_after", [])), len(got.get("host_before_edit", []))))
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
