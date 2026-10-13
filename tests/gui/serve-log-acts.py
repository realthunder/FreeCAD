"""Clients of a served document act on its transaction log: rows taken
back, versions, branches -- each within what is theirs to do.

docs/TransactionLog.md sec 32.22, 32.23. The log's panel in a browser lists
what the host answers (sec 32.21, tests/gui/serve-log.py); this is what it
does with it, each an op the host does or refuses with the reason
(Gui/SceneLog.cpp):

  log.undo          rows taken back as one step, though none is the last
  log.versions      the versions, and which is the document as it stands
  log.snapshot      a version of the document as it stands, named or not
  log.nameVersion   a version named, or unnamed again
  log.restore       the document made what a version was
  log.branches      the branches, and which the document is on
  log.branch        a new branch, from here, a row or a version
  log.switch        the document made the head of another branch
  log.renameBranch
  log.deleteBranch

Whose it is to do (sec 32.23). A document served as it is, is shared: a
client takes back its own rows and takes versions, and where the document
is in its log is the host's. A document opened from a branch to be served
(Gui.serveBranch) is its clients' from the serving point on, and not
before it.

A document served with no window. Client A may edit and states a camera;
client B came by a link that may only look; client H is the host; client C
joins a document served from a branch of the first. Asserted, of the
shared document:

  - A is told the document is shared, and whose each row is;
  - A does not take back the host's row; it does its own, as one new row
    which is its own and inverts it; the same row again is refused with
    the reason; with another's among those chosen none is taken back;
  - the host takes back anyone's: one, and two as one step; a record, a
    row there is none of and no rows are each said so;
  - A takes a snapshot with a name, which is the document as it stands;
    naming a version, restoring, and making, switching, renaming and
    deleting a branch are refused A;
  - the host renames and unnames the version, and after a change restores
    the document to it; makes a branch, switches back, is refused a row of
    the other branch and a name taken, branches from a row, renames;
  - with an edit open the document is not switched nor branched from
    elsewhere, and is once the edit is left;
  - the host deletes a branch, but not the one the document is on;
  - B is refused all ten.

Of the document served from a branch:

  - it is a document of its own on a new branch, the first one staying
    where it is;
  - C is told it is its own, where it was served from, and which rows,
    versions and branches are from before;
  - C does not take back a row from before, though it made it; nor restore
    or name a version from before, nor branch from a row or a version from
    before, nor switch to, rename or delete a branch from before;
  - C writes, takes a version and names it, makes a branch, switches back,
    branches from the serving point itself, restores its own version,
    renames and deletes a branch of its own -- but not the one the
    document was opened on -- and takes back a row of its own;
  - none of it moved the first document.

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
DOC = "ServeLogActs"
MODE = 6  # Gui::ViewProvider::ValueDiff
VIEW_TOKEN = "serve-log-acts-look"
HOST = "host@example.com"

EYE = (5.0, 5.0, 120.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 400.0
VW, VH = 800, 600

ACTS = ("log.undo", "log.versions", "log.snapshot", "log.nameVersion", "log.restore",
        "log.branches", "log.branch", "log.switch", "log.renameBranch", "log.deleteBranch")

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


def host(name=None):
    """What the host has of a document: the box, the rows, the branches,
    the versions."""
    def read():
        doc = FreeCAD.getDocument(name or DOC)
        box = doc.getObject("Box")
        return {
            "box": (round(box.Length.Value), round(box.Width.Value), round(box.Height.Value)),
            "rows": [(r["seq"], r["name"], r["kind"]) for r in doc.getTransactionLog()],
            "branches": sorted((b["name"], bool(b["current"]))
                               for b in doc.getTransactionBranches()),
            "versions": {v["num"]: (v["kind"], v["name"])
                         for v in doc.getTransactionVersions()},
            "edit": FreeCADGui.getDocument(doc.Name).getInEdit() is not None,
        }
    return read


def change(name, **dims):
    def run():
        doc = FreeCAD.getDocument(DOC)
        doc.openTransaction(name)
        for prop, value in dims.items():
            setattr(doc.getObject("Box"), prop, value)
        doc.recompute()
        doc.commitTransaction()
        return host()()
    return run


def serve_branch():
    """The host serves a branch of the document to a client of its own."""
    doc = FreeCAD.getDocument(DOC)
    served = FreeCADGui.serveBranch(doc, "for-ann")
    state["branch_doc"] = served.Name
    return {"name": served.Name, "same": served.Name == doc.Name,
            # (named for its branch and the version it is at, sec 27.23)
            "file": os.path.normpath(served.FileName).startswith(
                os.path.normpath(doc.FileName) + "@for-ann@v"),
            "docs": sorted(FreeCAD.listDocuments())}


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
        self.got = {}

    def run(self):
        try:
            ws = WS(self.port, "/scene?token=%s" % VIEW_TOKEN, headers="")
            ws.hello("serve-log-acts-b")
            ws.next_binary(20.0)
            for n, op in enumerate(ACTS):
                self.got[op] = ask(ws, {"id": n + 1, "op": op, "rows": [1], "version": 1,
                                        "name": "x", "to": "y"})
            self.ready.set()
            while not finished.is_set():
                ws.drain(0.2)
            ws.close()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()


def mine(reply):
    """The asker's own rows that changed something, newest first."""
    return [r for r in (reply or {}).get("rows", []) if r.get("mine") and r.get("ops")
            and r.get("kind") in ("user", "implicit")]


class Client(threading.Thread):
    """Client A, who may edit; H, the host; and C, in the branch's document."""

    def __init__(self, port, watcher):
        super().__init__(daemon=True)
        self.port = port
        self.watcher = watcher
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

    def write(self, ws, name, value, doc=None):
        """A property of the box set by this connection: a row of its own."""
        fields = {"obj": "Box", "target": "object", "name": name, "value": value}
        if doc:
            fields["doc"] = doc
        reply = self.do(ws, "setProperty", **fields)
        ws.drain(0.5)
        return reply

    def join(self, label, doc=None, identity=None):
        headers = None if identity is None else "%s: %s\r\n" % (wsclient.IDENTITY_HEADER,
                                                                 identity)
        ws = WS(self.port, headers=headers)
        ws.hello(label, ',"doc":"%s"' % doc if doc else "")
        if ws.next_binary(20.0) is None:
            raise RuntimeError("no snapshot for %s" % label)
        ws.next_binary(0.5)
        ws.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH))
        ws.drain(0.5)
        return ws

    def talk(self):
        self.shared()
        self.own()

    def shared(self):
        got = self.got
        ws = self.join("serve-log-acts-a")
        hs = self.join("serve-log-acts-h", identity=HOST)
        self.watcher.ready.wait(30.0)

        got["rows"] = self.do(ws, "log.rows")
        by_name = {r["name"]: r["seq"] for r in (got["rows"] or {}).get("rows", [])}
        self.by_name = by_name
        got["host0"] = on_host(host())

        # A row of the host's is not A's to take back
        got["undo_hosts"] = self.do(ws, "log.undo", rows=[by_name.get("wider", 0)])
        got["host_hosts"] = on_host(host())
        # A's own rows
        got["write1"] = self.write(ws, "Length", 20.0)
        got["write2"] = self.write(ws, "Width", 22.0)
        got["rows_mine"] = self.do(ws, "log.rows")
        own = [r["seq"] for r in mine(got["rows_mine"])]
        mine2, mine1 = (own + [0, 0])[:2]
        self.mine1 = mine1
        got["host_written"] = on_host(host())
        mark = len(ws.pushes)
        got["undo"] = self.do(ws, "log.undo", rows=[mine1])
        end = time.perf_counter() + 5.0
        while time.perf_counter() < end and not log_pushes(ws.pushes[mark:]):
            text = ws.next_text(0.3)
            if text is not None:
                ws.pushes.append(text)
        got["told"] = log_pushes(ws.pushes[mark:])
        got["host_undo"] = on_host(host())
        got["rows_undo"] = self.do(ws, "log.rows", limit=4)
        got["undo_again"] = self.do(ws, "log.undo", rows=[mine1])
        got["undo_mixed"] = self.do(ws, "log.undo", rows=[mine2, by_name.get("deeper", 0)])
        got["host_again"] = on_host(host())

        # The host takes back anyone's
        got["h_rows"] = ask(hs, {"id": 1, "op": "log.rows"})
        record = [r["seq"] for r in (got["h_rows"] or {}).get("rows", []) if r["ops"] == 0]
        got["h_undo"] = ask(hs, {"id": 2, "op": "log.undo", "rows": [by_name.get("last", 0)]})
        got["host_h_undo"] = on_host(host())
        got["h_undo_two"] = ask(hs, {"id": 3, "op": "log.undo",
                                     "rows": [mine2, by_name.get("deeper", 0)]})
        got["host_two"] = on_host(host())
        got["undo_record"] = ask(hs, {"id": 4, "op": "log.undo", "rows": record[:1]})
        got["undo_norow"] = ask(hs, {"id": 5, "op": "log.undo", "rows": [999999]})
        got["undo_none"] = ask(hs, {"id": 6, "op": "log.undo", "rows": []})
        got["host_refused"] = on_host(host())

        # Versions: A takes one; the rest is the host's
        got["snapshot"] = self.do(ws, "log.snapshot", name="kept")
        num = (got["snapshot"] or {}).get("version", 0)
        self.kept = num
        got["versions"] = self.do(ws, "log.versions")
        got["a_name"] = self.do(ws, "log.nameVersion", version=num, name="mine now")
        got["a_restore"] = self.do(ws, "log.restore", version=num)
        got["a_branch"] = self.do(ws, "log.branch", name="a-try")
        got["host_a"] = on_host(host())
        got["rename"] = ask(hs, {"id": 10, "op": "log.nameVersion", "version": num,
                                 "name": "kept well"})
        got["host_rename"] = on_host(host())
        got["unname"] = ask(hs, {"id": 11, "op": "log.nameVersion", "version": num, "name": ""})
        got["host_unname"] = on_host(host())
        got["name"] = ask(hs, {"id": 12, "op": "log.nameVersion", "version": num,
                               "name": "kept"})
        got["name_none"] = ask(hs, {"id": 13, "op": "log.nameVersion", "version": 999999,
                                    "name": "x"})
        got["host_changed"] = on_host(change("taller still", Height=30))
        got["versions_changed"] = self.do(ws, "log.versions")
        got["restore"] = ask(hs, {"id": 14, "op": "log.restore", "version": num})
        got["host_restore"] = on_host(host())
        got["restore_none"] = ask(hs, {"id": 15, "op": "log.restore", "version": 999999})

        # Branches: the host's
        got["branches0"] = self.do(ws, "log.branches")
        first = [b["name"] for b in (got["branches0"] or {}).get("branches", [])
                 if b.get("current")]
        first = first[0] if first else ""
        got["first"] = self.first = first
        got["h_branches0"] = ask(hs, {"id": 20, "op": "log.branches"})
        got["branch"] = ask(hs, {"id": 21, "op": "log.branch", "name": "try"})
        got["host_branch"] = on_host(host())
        got["branches"] = self.do(ws, "log.branches")
        got["host_on_try"] = on_host(change("on try", Height=40))
        got["a_switch"] = self.do(ws, "log.switch", name=first)
        got["a_rename"] = self.do(ws, "log.renameBranch", name="try", to="a-tried")
        got["host_a2"] = on_host(host())
        got["switch"] = ask(hs, {"id": 22, "op": "log.switch", "name": first})
        got["host_switch"] = on_host(host())
        got["rows_switch"] = ask(hs, {"id": 23, "op": "log.rows"})
        away = [r["seq"] for r in (got["rows_switch"] or {}).get("rows", [])
                if r["name"] == "on try"]
        got["undo_away"] = ask(hs, {"id": 24, "op": "log.undo", "rows": away[:1]})
        got["branch_taken"] = ask(hs, {"id": 25, "op": "log.branch", "name": "try"})
        got["branch_noname"] = ask(hs, {"id": 26, "op": "log.branch", "name": "  "})
        got["branch_row"] = ask(hs, {"id": 27, "op": "log.branch", "name": "old",
                                     "row": by_name.get("taller", 0)})
        got["host_row"] = on_host(host())
        got["rename_branch"] = ask(hs, {"id": 28, "op": "log.renameBranch", "name": "old",
                                        "to": "older"})
        got["rename_taken"] = ask(hs, {"id": 29, "op": "log.renameBranch", "name": "older",
                                       "to": "try"})
        got["host_renamed"] = on_host(host())
        got["switch_none"] = ask(hs, {"id": 30, "op": "log.switch", "name": "no-such-branch"})

        # With an edit open the document is not checked out under it
        got["edit"] = self.do(ws, "edit", obj="Cut", mode=MODE, row=by_name.get("taller", 0))
        ws.drain(1.0)
        got["host_edit"] = on_host(host())
        got["switch_editing"] = ask(hs, {"id": 31, "op": "log.switch", "name": first})
        got["branch_editing"] = ask(hs, {"id": 32, "op": "log.branch", "name": "other",
                                         "row": by_name.get("base", 0)})
        got["host_editing"] = on_host(host())
        self.do(ws, "resetEdit")
        ws.drain(0.5)
        got["switch_left"] = ask(hs, {"id": 33, "op": "log.switch", "name": first})
        got["host_left"] = on_host(host())

        # Deleting a branch
        got["delete"] = self.do(ws, "log.deleteBranch", name="try")
        got["host_delete"] = on_host(host())
        got["h_delete_current"] = ask(hs, {"id": 34, "op": "log.deleteBranch", "name": first})
        got["h_delete"] = ask(hs, {"id": 35, "op": "log.deleteBranch", "name": "try"})
        got["h_delete_none"] = ask(hs, {"id": 36, "op": "log.deleteBranch", "name": "try"})
        got["host_deleted"] = on_host(host())
        got["branches_after"] = self.do(ws, "log.branches")
        hs.close()
        ws.close()

    def own(self):
        got = self.got
        by_name, first, kept = self.by_name, self.first, self.kept
        got["main_before"] = on_host(host())
        got["served"] = on_host(serve_branch)
        name = (got["served"] or {}).get("name")
        got["main_served"] = on_host(host())
        cs = self.join("serve-log-acts-c", doc=name)

        got["c_rows"] = self.do(cs, "log.rows")
        got["c_branches"] = self.do(cs, "log.branches")
        got["c_versions"] = self.do(cs, "log.versions")
        got["c_host0"] = on_host(host(name))
        floor = (got["c_rows"] or {}).get("floor", 0)

        # From before the serving point: not C's
        got["c_undo_before"] = self.do(cs, "log.undo", rows=[self.mine1])
        got["c_restore_before"] = self.do(cs, "log.restore", version=kept)
        got["c_name_before"] = self.do(cs, "log.nameVersion", version=kept, name="c's now")
        got["c_branch_row_before"] = self.do(cs, "log.branch", name="c-old",
                                             row=by_name.get("taller", 0))
        got["c_branch_version_before"] = self.do(cs, "log.branch", name="c-old2", version=kept)
        got["c_switch_before"] = self.do(cs, "log.switch", name=first)
        got["c_rename_before"] = self.do(cs, "log.renameBranch", name=first, to="c-main")
        got["c_delete_before"] = self.do(cs, "log.deleteBranch", name="older")
        got["c_host_before"] = on_host(host(name))

        # From the serving point on: C's own
        got["c_write"] = self.write(cs, "Height", 25.0, doc=name)
        got["c_host_write"] = on_host(host(name))
        got["c_snapshot"] = self.do(cs, "log.snapshot", name="c-kept")
        vc = (got["c_snapshot"] or {}).get("version", 0)
        got["c_name"] = self.do(cs, "log.nameVersion", version=vc, name="c-kept well")
        got["c_branch"] = self.do(cs, "log.branch", name="c-try")
        got["c_write2"] = self.write(cs, "Length", 30.0, doc=name)
        got["c_host_try"] = on_host(host(name))
        got["c_switch"] = self.do(cs, "log.switch", name="for-ann")
        got["c_host_switch"] = on_host(host(name))
        got["c_branch_floor"] = self.do(cs, "log.branch", name="c-root", row=floor)
        got["c_host_floor"] = on_host(host(name))
        got["c_branches_made"] = self.do(cs, "log.branches")
        got["c_restore"] = self.do(cs, "log.restore", version=vc)
        got["c_host_restore"] = on_host(host(name))
        got["c_rename"] = self.do(cs, "log.renameBranch", name="c-try", to="c-tried")
        got["c_delete_root"] = self.do(cs, "log.deleteBranch", name="for-ann")
        got["c_delete"] = self.do(cs, "log.deleteBranch", name="c-tried")
        got["c_host_deleted"] = on_host(host(name))
        got["c_write3"] = self.write(cs, "Width", 33.0, doc=name)
        got["c_write4"] = self.write(cs, "Length", 44.0, doc=name)
        got["c_rows_own"] = self.do(cs, "log.rows")
        own = [r["seq"] for r in mine(got["c_rows_own"]) if r.get("within")]
        got["c_undo"] = self.do(cs, "log.undo", rows=own[1:2])
        got["c_host_undo"] = on_host(host(name))
        got["c_versions_end"] = self.do(cs, "log.versions")
        got["main_after"] = on_host(host())
        cs.close()


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
        doc.saveAs(os.path.join(tempfile.mkdtemp(prefix="fc-serve-log-acts-"), "log.FCStd"))
        for name, prop, value in (("taller", "Height", 16), ("wider", "Length", 14),
                                  ("deeper", "Width", 12), ("last", "Height", 18)):
            doc.openTransaction(name)
            setattr(box, prop, value)
            doc.recompute()
            doc.commitTransaction()

        port = free_port()
        if not check("the document is served with no window",
                     FreeCADGui.serveDocument(doc, port), "port %d" % port):
            finish()
            return
        # A link that may only look and the host, beside the door that may edit
        FreeCADGui.serveSetGrants([{"token": VIEW_TOKEN, "access": 1},
                                   {"identity": HOST, "access": 3}, {"access": 0}])
        watcher = Watcher(port)
        client = Client(port, watcher)
        state["watcher"], state["client"] = watcher, client
        QtCore.QTimer.singleShot(20, serve_asked)
        watcher.start()
        client.start()
        QtCore.QTimer.singleShot(280000, lambda: (check("the run ended", state["done"]), finish()))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
        finish()


def code(reply):
    return (reply or {}).get("code")


def ok(reply):
    return (reply or {}).get("ok") is True


def forbidden(got, *keys):
    return all(code(got.get(k)) == "Forbidden" for k in keys)


def verify_shared(got, watcher):
    h0 = got.get("host0", {})
    rows = got.get("rows") or {}
    named = {r["name"]: r["seq"] for r in rows.get("rows", [])}
    check("the box as it was built", h0.get("box") == (14, 12, 18), h0.get("box"))
    check("the client is told the document is shared, and that the rows are another's",
          rows.get("host") is False and rows.get("own") is False and "floor" not in rows
          and all(r.get("mine") is False and r.get("within") is True
                  for r in rows.get("rows", []) if r.get("ops")),
          (rows.get("host"), rows.get("own"),
           [(r.get("name"), r.get("mine")) for r in rows.get("rows", [])][:4]))

    # rows taken back
    hosts = got.get("undo_hosts") or {}
    check("a row of the host's is not the client's to take back, and it is told whose it is",
          code(hosts) == "NotYours" and "host" in hosts.get("message", "")
          and got.get("host_hosts") == h0, hosts)
    written = got.get("host_written", {})
    own = mine(got.get("rows_mine"))
    check("what the client writes are rows of its own",
          ok(got.get("write1")) and ok(got.get("write2")) and written.get("box") == (20, 22, 18)
          and len(own) == 2 and all(r.get("author") == wsclient.IDENTITY for r in own),
          (written.get("box"), [(r.get("seq"), r.get("name"), r.get("author")) for r in own]))
    mine2, mine1 = ([r["seq"] for r in own] + [0, 0])[:2]
    hu = got.get("host_undo", {})
    new = hu.get("rows", [])[len(written.get("rows", [])):]
    check("its own row that is not the last is taken back: what it set, and nothing else",
          ok(got.get("undo")) and hu.get("box") == (14, 22, 18)
          and got["undo"].get("head") == got["undo"].get("last"),
          (got.get("undo"), hu.get("box")))
    listed = [r for r in (got.get("rows_undo") or {}).get("rows", [])
              if r.get("kind") == "undo"]
    check("as one new row, which is the client's own and inverts the row",
          len([r for r in new if r[2] == "undo"]) == 1 and len(listed) == 1
          and listed[0].get("author") == wsclient.IDENTITY and listed[0].get("mine") is True
          and listed[0].get("inverts") == mine1,
          (new, [(r.get("name"), r.get("author"), r.get("inverts")) for r in listed]))
    check("the client is told when its own act moves the log",
          got.get("told") and got["told"][0] == DOC, got.get("told"))
    again = got.get("undo_again") or {}
    check("the same row again is refused with the reason",
          code(again) == "Refused" and len(again.get("message", "")) > 8, again)
    check("with another's among those chosen none is taken back",
          code(got.get("undo_mixed")) == "NotYours" and got.get("host_again") == hu,
          got.get("undo_mixed"))

    hh = got.get("host_h_undo", {})
    check("the host takes back anyone's row",
          (got.get("h_rows") or {}).get("host") is True and ok(got.get("h_undo"))
          and hh.get("box") == (14, 22, 16), (got.get("h_undo"), hh.get("box")))
    ht = got.get("host_two", {})
    two = [r for r in ht.get("rows", [])[len(hh.get("rows", [])):] if r[2] == "undo"]
    check("and two as one step, the host's and the client's, named by both",
          ok(got.get("h_undo_two")) and ht.get("box") == (14, 10, 16) and len(two) == 1
          and "deeper" in two[0][1], (got.get("h_undo_two"), ht.get("box"), two))
    check("a record has nothing to take back",
          code(got.get("undo_record")) == "NothingToUndo", got.get("undo_record"))
    check("a row there is none of is said so",
          code(got.get("undo_norow")) == "UnknownRow", got.get("undo_norow"))
    check("no rows is a bad request", code(got.get("undo_none")) == "BadRequest",
          got.get("undo_none"))
    check("what was refused wrote nothing", got.get("host_refused") == ht,
          (len(got.get("host_refused", {}).get("rows", [])), len(ht.get("rows", []))))

    # versions
    snap = got.get("snapshot") or {}
    num = snap.get("version", 0)
    versions = {v["num"]: v for v in (got.get("versions") or {}).get("versions", [])}
    check("the client takes a snapshot with a name: a named version",
          ok(snap) and num > 0 and versions.get(num, {}).get("named") is True
          and versions[num].get("name") == "kept", (snap, versions.get(num)))
    check("which is the document as it stands, of this history",
          versions.get(num, {}).get("head") is True and versions[num].get("here") is True,
          versions.get(num))
    ha = got.get("host_a", {})
    check("naming a version, restoring and making a branch are refused the client",
          forbidden(got, "a_name", "a_restore", "a_branch")
          and "shared" in (got.get("a_branch") or {}).get("message", "")
          and ha.get("versions", {}).get(num) == ("named", "kept")
          and ha.get("branches") == ht.get("branches") and ha.get("box") == ht.get("box"),
          (got.get("a_name"), got.get("a_restore"), got.get("a_branch")))
    check("the host renames it", ok(got.get("rename"))
          and got.get("host_rename", {}).get("versions", {}).get(num) == ("named", "kept well"),
          (got.get("rename"), got.get("host_rename", {}).get("versions", {}).get(num)))
    check("and unnames it", ok(got.get("unname"))
          and got.get("host_unname", {}).get("versions", {}).get(num, ("", ""))[0] == "unnamed",
          (got.get("unname"), got.get("host_unname", {}).get("versions", {}).get(num)))
    check("a version there is none of is said so",
          code(got.get("name_none")) == "UnknownVersion"
          and code(got.get("restore_none")) == "UnknownVersion",
          (got.get("name_none"), got.get("restore_none")))
    changed = {v["num"]: v for v in (got.get("versions_changed") or {}).get("versions", [])}
    check("after a change it is no longer the document as it stands",
          got.get("host_changed", {}).get("box") == (14, 10, 30)
          and changed.get(num, {}).get("head") is False
          and changed.get(num, {}).get("name") == "kept", changed.get(num))
    hr = got.get("host_restore", {})
    restored = hr.get("rows", [])[len(got.get("host_changed", {}).get("rows", [])):]
    check("the host restores the document to it, as a row of its own",
          ok(got.get("restore")) and hr.get("box") == (14, 10, 16)
          and any(r[2] == "restore" for r in restored),
          (got.get("restore"), hr.get("box"), restored))

    # branches
    first = got.get("first", "")
    b0 = (got.get("branches0") or {}).get("branches", [])
    check("the branches: the one the document is on, and who asks",
          ok(got.get("branches0")) and len(b0) == 1 and b0[0].get("current") is True
          and first and got["branches0"].get("host") is False
          and got["branches0"].get("own") is False
          and (got.get("h_branches0") or {}).get("host") is True,
          (got.get("branches0"), (got.get("h_branches0") or {}).get("host")))
    hb = got.get("host_branch", {})
    check("the host's new branch from here is the one the document is on, with nothing moved",
          ok(got.get("branch")) and got["branch"].get("branchName") == "try"
          and hb.get("box") == hr.get("box")
          and ("try", True) in hb.get("branches", []), (got.get("branch"), hb.get("branches")))
    listed_b = {b["name"]: b for b in (got.get("branches") or {}).get("branches", [])}
    check("and is listed so to the client", set(listed_b) == {first, "try"}
          and listed_b.get("try", {}).get("current") is True
          and listed_b.get(first, {}).get("current") is False, listed_b)
    check("switching and renaming a branch are refused the client",
          forbidden(got, "a_switch", "a_rename")
          and got.get("host_a2", {}).get("branches") == got.get("host_on_try", {})
          .get("branches"), (got.get("a_switch"), got.get("a_rename")))
    hs = got.get("host_switch", {})
    check("a change there is not here after the host switches back",
          got.get("host_on_try", {}).get("box") == (14, 10, 40) and ok(got.get("switch"))
          and got["switch"].get("branchName") == first and hs.get("box") == (14, 10, 16)
          and (first, True) in hs.get("branches", []), (got.get("switch"), hs.get("box")))
    away = [r for r in (got.get("rows_switch") or {}).get("rows", [])
            if r["name"] == "on try"]
    check("its row is listed as another branch's",
          len(away) == 1 and away[0].get("here") is False
          and away[0].get("branchName") == "try", away)
    check("and is not taken back from here, by the host either",
          code(got.get("undo_away")) == "NotHere", got.get("undo_away"))
    check("a name taken is refused, and no name is a bad request",
          code(got.get("branch_taken")) == "Refused"
          and code(got.get("branch_noname")) == "BadRequest",
          (got.get("branch_taken"), got.get("branch_noname")))
    hrow = got.get("host_row", {})
    check("a branch from a row is the document as that row left it",
          ok(got.get("branch_row")) and hrow.get("box") == (10, 10, 16)
          and ("old", True) in hrow.get("branches", []),
          (got.get("branch_row"), hrow.get("box"), hrow.get("branches")))
    hren = got.get("host_renamed", {})
    check("a branch is renamed, not to a name taken",
          ok(got.get("rename_branch")) and got["rename_branch"].get("branchName") == "older"
          and code(got.get("rename_taken")) == "Refused"
          and ("older", True) in hren.get("branches", [])
          and not any(n == "old" for n, _ in hren.get("branches", [])),
          (got.get("rename_branch"), got.get("rename_taken"), hren.get("branches")))
    check("a branch there is none of is said so",
          code(got.get("switch_none")) == "UnknownBranch", got.get("switch_none"))

    # an edit open
    he = got.get("host_editing", {})
    check("with an edit open the document is not switched, nor branched from elsewhere",
          ok(got.get("edit")) and got.get("host_edit", {}).get("edit") is True
          and code(got.get("switch_editing")) == "Editing"
          and code(got.get("branch_editing")) == "Editing" and he.get("edit") is True
          and ("older", True) in he.get("branches", []),
          (got.get("edit"), got.get("switch_editing"), got.get("branch_editing"),
           he.get("branches")))
    hl = got.get("host_left", {})
    check("and is once the edit is left",
          ok(got.get("switch_left")) and hl.get("edit") is False
          and (first, True) in hl.get("branches", []) and hl.get("box") == (14, 10, 16),
          (got.get("switch_left"), hl.get("branches"), hl.get("box")))

    # deleting
    check("deleting a branch is refused the client",
          code(got.get("delete")) == "Forbidden"
          and got.get("host_delete", {}).get("branches") == hl.get("branches"),
          (got.get("delete"), got.get("host_delete", {}).get("branches")))
    hd = got.get("host_deleted", {})
    check("the host does: the branch and the rows only it holds",
          ok(got.get("h_delete")) and got["h_delete"].get("rows", 0) >= 1
          and not any(n == "try" for n, _ in hd.get("branches", []))
          and not any(r[1] == "on try" for r in hd.get("rows", [])),
          (got.get("h_delete"), hd.get("branches")))
    check("not the one the document is on, and not one that is gone",
          code(got.get("h_delete_current")) == "Refused"
          and code(got.get("h_delete_none")) == "UnknownBranch",
          (got.get("h_delete_current"), got.get("h_delete_none")))
    after = sorted(b["name"] for b in (got.get("branches_after") or {}).get("branches", []))
    check("the client lists what is left", after == sorted([first, "older"]), after)

    check("the client that may only look is refused all ten",
          all(code(watcher.got.get(op)) == "ViewOnly" for op in ACTS),
          {op: code(watcher.got.get(op)) for op in ACTS})


def verify_own(got):
    first = got.get("first", "")
    served = got.get("served") or {}
    before, after_serve = got.get("main_before", {}), got.get("main_served", {})
    check("a branch is served in a document of its own, of the same file",
          served.get("name") and served.get("same") is False and served.get("file") is True
          and len(served.get("docs", [])) == 2, served)
    check("the first document stays where it is, and has the new branch in its log",
          after_serve.get("box") == before.get("box") == (14, 10, 16)
          and (first, True) in after_serve.get("branches", [])
          and ("for-ann", False) in after_serve.get("branches", []),
          (after_serve.get("box"), after_serve.get("branches")))

    rows = got.get("c_rows") or {}
    floor = rows.get("floor", 0)
    listed = rows.get("rows", [])
    c0 = got.get("c_host0", {})
    check("its client is told the document is its own, and where it was served from",
          ok(rows) and rows.get("own") is True and rows.get("host") is False and floor > 0
          and rows.get("branchName") == "for-ann" and c0.get("box") == (14, 10, 16)
          and ("for-ann", True) in c0.get("branches", []),
          (rows.get("own"), rows.get("host"), floor, rows.get("branchName"), c0.get("branches")))
    old = [r for r in listed if r["seq"] <= floor]
    new = [r for r in listed if r["seq"] > floor]
    check("the rows from before are listed, and said to be from before",
          len(old) >= 8 and all(r.get("within") is False for r in old)
          and all(r.get("within") is True for r in new)
          and any(r.get("mine") for r in old),
          ([(r["seq"], r.get("within"), r.get("mine")) for r in listed][:6], floor))
    branches = {b["name"]: b for b in (got.get("c_branches") or {}).get("branches", [])}
    check("so are the branches: the one served is its own, the others are not",
          (got.get("c_branches") or {}).get("own") is True
          and branches.get("for-ann", {}).get("within") is True
          and branches["for-ann"].get("root") is True
          and branches["for-ann"].get("current") is True
          and branches.get(first, {}).get("within") is False
          and branches.get("older", {}).get("within") is False,
          {n: (b.get("within"), b.get("root")) for n, b in branches.items()})
    versions = {v["name"]: v for v in (got.get("c_versions") or {}).get("versions", [])}
    at_floor = [v for v in (got.get("c_versions") or {}).get("versions", [])
                if v.get("within")]
    # (the version at the serving point is the one the first document was
    # last at: only records came after it, so its row is not after the fork)
    check("and the versions: the one it was served from is its own, an older one is not",
          versions.get("kept", {}).get("within") is False and len(at_floor) == 1
          and at_floor[0]["seq"] <= floor,
          [(v["num"], v["name"], v["seq"], v.get("within"))
           for v in (got.get("c_versions") or {}).get("versions", [])])

    cb = got.get("c_host_before", {})
    check("a row from before is not taken back, though this user made it",
          forbidden(got, "c_undo_before")
          and "before" in (got.get("c_undo_before") or {}).get("message", ""),
          got.get("c_undo_before"))
    check("a version from before is neither restored nor named",
          forbidden(got, "c_restore_before", "c_name_before"),
          (got.get("c_restore_before"), got.get("c_name_before")))
    check("no branch is made from a row or a version from before",
          forbidden(got, "c_branch_row_before", "c_branch_version_before"),
          (got.get("c_branch_row_before"), got.get("c_branch_version_before")))
    check("a branch from before is not switched to, renamed or deleted",
          forbidden(got, "c_switch_before", "c_rename_before", "c_delete_before"),
          (got.get("c_switch_before"), got.get("c_rename_before"),
           got.get("c_delete_before")))
    check("none of which moved the document", cb == c0,
          (cb.get("box"), cb.get("branches"), len(cb.get("rows", [])), len(c0.get("rows", []))))

    cw = got.get("c_host_write", {})
    check("the client writes in its document",
          ok(got.get("c_write")) and cw.get("box") == (14, 10, 25), cw.get("box"))
    vc = (got.get("c_snapshot") or {}).get("version", 0)
    ct = got.get("c_host_try", {})
    check("takes a version there and names it",
          ok(got.get("c_snapshot")) and vc > 0 and ok(got.get("c_name"))
          and ct.get("versions", {}).get(vc) == ("named", "c-kept well"),
          (got.get("c_snapshot"), got.get("c_name"), ct.get("versions", {}).get(vc)))
    check("makes a branch from where it is, and writes on it",
          ok(got.get("c_branch")) and got["c_branch"].get("branchName") == "c-try"
          and ok(got.get("c_write2")) and ct.get("box") == (30, 10, 25)
          and ("c-try", True) in ct.get("branches", []),
          (got.get("c_branch"), ct.get("box"), ct.get("branches")))
    csw = got.get("c_host_switch", {})
    check("switches back to the branch it was served",
          ok(got.get("c_switch")) and csw.get("box") == (14, 10, 25)
          and ("for-ann", True) in csw.get("branches", []),
          (got.get("c_switch"), csw.get("box")))
    cf = got.get("c_host_floor", {})
    made = {b["name"]: b for b in (got.get("c_branches_made") or {}).get("branches", [])}
    check("branches from the serving point itself: the document as it was served",
          ok(got.get("c_branch_floor")) and cf.get("box") == (14, 10, 16)
          and ("c-root", True) in cf.get("branches", []),
          (got.get("c_branch_floor"), cf.get("box"), cf.get("branches")))
    check("and that branch is one of its own",
          made.get("c-root", {}).get("within") is True
          and made.get("c-try", {}).get("within") is True
          and made.get(first, {}).get("within") is False,
          {n: b.get("within") for n, b in made.items()})
    cr = got.get("c_host_restore", {})
    check("restores a version of its own",
          ok(got.get("c_restore")) and cr.get("box") == (14, 10, 25),
          (got.get("c_restore"), cr.get("box")))
    cd = got.get("c_host_deleted", {})
    check("renames and deletes a branch of its own",
          ok(got.get("c_rename")) and ok(got.get("c_delete"))
          and not any(n in ("c-try", "c-tried") for n, _ in cd.get("branches", [])),
          (got.get("c_rename"), got.get("c_delete"), cd.get("branches")))
    check("but not the one the document was opened on",
          forbidden(got, "c_delete_root")
          and any(n == "for-ann" for n, _ in cd.get("branches", [])),
          (got.get("c_delete_root"), cd.get("branches")))
    cu = got.get("c_host_undo", {})
    check("and takes back a row of its own that is not the last",
          ok(got.get("c_write3")) and ok(got.get("c_write4")) and ok(got.get("c_undo"))
          and cu.get("box") == (44, 10, 25), (got.get("c_undo"), cu.get("box")))
    ma = got.get("main_after", {})
    check("none of it moved the first document",
          ma.get("box") == (14, 10, 16) and (first, True) in ma.get("branches", [])
          and ma.get("rows", [])[:len(after_serve.get("rows", []))] == after_serve.get("rows")
          and ma.get("versions", {}).get(got.get("snapshot", {}).get("version"))
          == ("named", "kept"),
          (ma.get("box"), ma.get("branches")))


def verify():
    if state["done"]:
        return
    try:
        client, watcher = state["client"], state["watcher"]
        finished.set()
        got = client.got
        if client.error or watcher.error:
            check("the clients ran", False, client.error or watcher.error)
        verify_shared(got, watcher)
        verify_own(got)
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
