"""Clients of a served document act on its transaction log -- rows taken
back and applied, versions, branches, requests to merge -- each within what
it may of the branch.

docs/TransactionLog.md sec 32.22, 32.25. The log's panel in a browser lists
what the host answers (sec 32.21, tests/gui/serve-log.py); this is what it
does with it, each an op the host does or refuses with the reason
(Gui/SceneLog.cpp):

  log.undo          rows taken back as one step, though none is the last
  log.apply         rows of another branch applied to this one
  log.versions      the versions, and which is the document as it stands
  log.snapshot      a version of the document as it stands, named or not
  log.nameVersion   a version named, or unnamed again
  log.restore       the document made what a version was
  log.branches      the branches, and which the document is on
  log.branch        a new branch, from here, a row or a version
  log.switch        the document made the head of another branch
  log.renameBranch
  log.deleteBranch
  log.requests      the requests to merge one branch into another
  log.request       ask for one
  log.closeRequest  withdraw one, or reject it
  log.mergeRequest  do what one asks

What one may (sec 32.25) is by branch: read, or write from a point on; the
host all of it. Read lists the branch, applies its rows here and asks for
merges of it; write moves a document that is on it and deals with the
requests to merge into it. It is said in the log by user and branch.
Where nothing is said a client reads the branch of the document it is in,
and a document opened from a branch to be served (Gui.serveBranch) is
written by whoever joins it, from where the branch forks.

A document served with no window. Client A may edit and states a camera;
client B came by a link that may only look; client H is the host; client C
joins a document served from a branch of the first. Asserted, of the
shared document:

  - A is told it reads and does not write, and whose each row is;
  - A does not take back the host's row; it does its own, as one new row
    which is its own and inverts it; the same row again is refused with
    the reason; with another's among those chosen none is taken back;
  - the host takes back anyone's: one, and two as one step; a record, a
    row there is none of and no rows are each said so;
  - A takes a snapshot with a name, which is the document as it stands;
    naming a version, restoring and making a branch are refused A;
  - the host renames and unnames the version, and after a change restores
    the document to it; makes a branch, switches back, is refused a row of
    the other branch and a name taken, branches from a row, renames;
  - A is listed the branch the document is on and no other, and a branch
    it does not read is unknown to it; switching and renaming are refused;
  - given to read another branch, A is listed it and its rows, applies a
    row of it here as a row of its own, and where that meets what was set
    here since is refused with how many, and says a side;
  - A asks for that branch to be merged into this one; the same again is
    refused; it is listed as its own, and is not A's to merge or reject;
    A withdraws it and asks again; the host merges that one, which says
    so; another the host rejects;
  - with an edit open the document is not switched nor branched from
    elsewhere, and is once the edit is left;
  - the host deletes a branch, but not the one the document is on;
  - B is refused all fifteen.

Of the document served from a branch:

  - it is a document of its own on a new branch, the first one staying
    where it is;
  - C is told it writes, and from where; the rows and versions from before
    are listed as ones it does not move the document from; it is listed
    the branch it was served and the one that was made from, which it
    reads;
  - C takes back a row of its own from before the point; it does not
    restore or name a version from before, nor branch from a row or a
    version from before, nor switch to or rename the branch it only
    reads; a branch it does not read is unknown to it;
  - C writes, takes a version and names it, makes a branch, switches back,
    branches from the serving point itself, restores its own version,
    renames and deletes a branch of its own making -- but not the one it
    was given -- and takes back a row of its own;
  - D, who joins the same document, writes the branches C made there and
    moves the document from them, but does not delete them (ruled
    2026-10-14);
  - none of it moved the first document;
  - C asks for its branch to be merged back, which is not C's to do; the
    host does it in the first document, which then has what C did.

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
OTHER = "other@example.com"

EYE = (5.0, 5.0, 120.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 400.0
VW, VH = 800, 600

ACTS = ("log.undo", "log.apply", "log.versions", "log.snapshot", "log.nameVersion",
        "log.restore", "log.branches", "log.branch", "log.switch", "log.renameBranch",
        "log.deleteBranch", "log.requests", "log.request", "log.closeRequest",
        "log.mergeRequest")

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
            "requests": [(r["id"], r["source"], r["target"], r["by"], r["state"],
                          r["closed_by"]) for r in doc.getTransactionMergeRequests()],
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


def grant(branch, write=False):
    """The host says what the test's client may of a branch."""
    def run():
        doc = FreeCAD.getDocument(DOC)
        doc.setTransactionAccess("verified", wsclient.IDENTITY, branch, write)
        return [(a["kind"], a["name"], a["branch"], a["write"])
                for a in doc.getTransactionAccess()]
    return run


def ungrant(branch):
    def run():
        return FreeCAD.getDocument(DOC).clearTransactionAccess("verified", wsclient.IDENTITY,
                                                               branch)
    return run


def said():
    """What the log says of users and branches."""
    return [(a["kind"], a["name"], a["branch"], a["write"], a["made"])
            for a in FreeCAD.getDocument(DOC).getTransactionAccess()]


def undo_main_state():
    """The first document's merges, and what the host's connection has to
    redo (an undo step is its author's, sec 30.10)."""
    doc = FreeCAD.getDocument(DOC)
    doc.pushTransactionActor("verified", HOST)
    try:
        redo = list(doc.RedoNames)
    finally:
        doc.popTransactionActor()
    return {"redo": redo,
            "merges": [r["seq"] for r in doc.getTransactionLog() if r["kind"] == "merge"]}


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
                                        "name": "x", "to": "y", "source": "x", "target": "y",
                                        "request": 1, "state": "withdrawn"})
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
        got["h_try_version"] = ask(hs, {"id": 52, "op": "log.snapshot", "name": "try-v"})
        tv = (got["h_try_version"] or {}).get("version", 0)
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

        # A branch that is not read; then read
        got["a_branches_unread"] = self.do(ws, "log.branches")
        got["a_rows_unread"] = self.do(ws, "log.rows")
        got["a_apply_unread"] = self.do(ws, "log.apply", rows=away[:1])
        got["a_switch_unread"] = self.do(ws, "log.switch", name="try")
        got["a_request_unread"] = self.do(ws, "log.request", source="try", target=first)
        # ... nor are its rows and versions by their numbers, nor a request
        # of it
        got["a_undo_unread"] = self.do(ws, "log.undo", rows=away[:1])
        got["a_edit_unread"] = self.do(ws, "edit", obj="Cut", mode=MODE, row=(away + [0])[0])
        ws.drain(0.5)
        got["host_edit_unread"] = on_host(host())
        if ok(got["a_edit_unread"]):
            self.do(ws, "resetEdit")
            ws.drain(0.5)
        got["a_versions_unread"] = self.do(ws, "log.versions")
        got["a_name_unread"] = self.do(ws, "log.nameVersion", version=tv, name="seen")
        got["a_restore_unread"] = self.do(ws, "log.restore", version=tv)
        got["h_request_unread"] = ask(hs, {"id": 50, "op": "log.request", "source": "try",
                                           "target": first, "note": "the host's"})
        r0 = (got["h_request_unread"] or {}).get("request", 0)
        got["a_requests_unread"] = self.do(ws, "log.requests")
        got["a_withdraw_unread"] = self.do(ws, "log.closeRequest", request=r0,
                                           state="withdrawn")
        got["a_merge_unread"] = self.do(ws, "log.mergeRequest", request=r0)
        got["h_withdraw_unread"] = ask(hs, {"id": 51, "op": "log.closeRequest", "request": r0,
                                            "state": "withdrawn"})
        got["granted"] = on_host(grant("try"))
        got["a_versions_read"] = self.do(ws, "log.versions")
        got["a_branches_read"] = self.do(ws, "log.branches")
        got["a_rows_read"] = self.do(ws, "log.rows")
        got["a_apply_here"] = self.do(ws, "log.apply", rows=[by_name.get("deeper", 0)])
        got["a_apply"] = self.do(ws, "log.apply", rows=away[:1])
        got["host_applied"] = on_host(host())
        got["rows_applied"] = self.do(ws, "log.rows", limit=3)
        got["host_moved_on"] = on_host(change("taller yet", Height=50))
        got["a_apply_conflict"] = self.do(ws, "log.apply", rows=away[:1])
        # What it would do, each conflict by the key its side is picked by
        got["a_preview"] = self.do(ws, "log.preview", rows=away[:1])
        keys = [c["key"] for c in (got["a_preview"] or {}).get("changes", [])
                if c.get("kind") == "conflict"]
        got["a_apply_ours"] = self.do(ws, "log.apply", rows=away[:1],
                                      picks={k: "ours" for k in keys})
        got["host_ours"] = on_host(host())
        got["host_conflict"] = on_host(host())
        got["a_apply_theirs"] = self.do(ws, "log.apply", rows=away[:1], fallback="theirs")
        got["host_theirs"] = on_host(host())

        # Requests to merge
        got["a_request"] = self.do(ws, "log.request", source="try", target=first,
                                   note="the taller box")
        r1 = (got["a_request"] or {}).get("request", 0)
        got["a_request_again"] = self.do(ws, "log.request", source="try", target=first)
        got["a_request_self"] = self.do(ws, "log.request", source=first, target=first)
        got["a_requests"] = self.do(ws, "log.requests")
        got["a_merge"] = self.do(ws, "log.mergeRequest", request=r1)
        got["a_reject"] = self.do(ws, "log.closeRequest", request=r1, state="rejected")
        got["h_requests"] = ask(hs, {"id": 40, "op": "log.requests"})
        got["a_withdraw"] = self.do(ws, "log.closeRequest", request=r1, state="withdrawn")
        got["a_withdraw_again"] = self.do(ws, "log.closeRequest", request=r1,
                                          state="withdrawn")
        got["a_request2"] = self.do(ws, "log.request", source="try", target=first)
        r2 = (got["a_request2"] or {}).get("request", 0)
        got["h_merge"] = ask(hs, {"id": 41, "op": "log.mergeRequest", "request": r2})
        got["host_merged"] = on_host(host())
        got["h_merge_again"] = ask(hs, {"id": 42, "op": "log.mergeRequest", "request": r2})
        got["a_request3"] = self.do(ws, "log.request", source="try", target=first)
        r3 = (got["a_request3"] or {}).get("request", 0)
        got["h_reject"] = ask(hs, {"id": 43, "op": "log.closeRequest", "request": r3,
                                   "state": "rejected"})
        got["h_norequest"] = ask(hs, {"id": 44, "op": "log.mergeRequest", "request": 999999})
        got["a_requests_end"] = self.do(ws, "log.requests")
        got["host_requests"] = on_host(host())

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

        # Given to write the first branch: a branch the host makes from it
        # is not the client's; one the client makes is, to delete too
        got["granted_write"] = on_host(grant(first, True))
        got["h_branch_own"] = ask(hs, {"id": 60, "op": "log.branch", "name": "hosts-own"})
        got["h_switch_own"] = ask(hs, {"id": 61, "op": "log.switch", "name": first})
        got["a_branches_hosts"] = self.do(ws, "log.branches")
        got["a_switch_hosts"] = self.do(ws, "log.switch", name="hosts-own")
        got["a_delete_hosts"] = self.do(ws, "log.deleteBranch", name="hosts-own")
        got["a_branch_own"] = self.do(ws, "log.branch", name="a-own")
        got["a_branches_own"] = self.do(ws, "log.branches")
        got["said_own"] = on_host(said)
        got["a_switch_back"] = self.do(ws, "log.switch", name=first)
        got["a_delete_own"] = self.do(ws, "log.deleteBranch", name="a-own")
        got["said_deleted"] = on_host(said)
        got["h_delete_hosts"] = ask(hs, {"id": 62, "op": "log.deleteBranch",
                                         "name": "hosts-own"})
        got["ungranted"] = on_host(ungrant(first))
        got["host_maker"] = on_host(host())
        # A row of A's own for its user to take back from the branch's
        # document, later
        got["write3"] = self.write(ws, "Length", 15.0)
        got["rows_mine3"] = self.do(ws, "log.rows", limit=3)
        self.mine3 = ([r["seq"] for r in mine(got["rows_mine3"])] + [0])[0]
        got["host_end"] = on_host(host())
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

        # From before the point: its own row is its to take back; the rest
        # is not C's
        got["c_undo_before"] = self.do(cs, "log.undo", rows=[self.mine3])
        got["c_host_undone"] = on_host(host(name))
        got["c_restore_before"] = self.do(cs, "log.restore", version=kept)
        got["c_name_before"] = self.do(cs, "log.nameVersion", version=kept, name="c's now")
        got["c_branch_row_before"] = self.do(cs, "log.branch", name="c-old",
                                             row=by_name.get("taller", 0))
        got["c_branch_version_before"] = self.do(cs, "log.branch", name="c-old2", version=kept)
        got["c_switch_before"] = self.do(cs, "log.switch", name=first)
        got["c_rename_before"] = self.do(cs, "log.renameBranch", name=first, to="c-main")
        got["c_delete_before"] = self.do(cs, "log.deleteBranch", name=first)
        got["c_delete_unread"] = self.do(cs, "log.deleteBranch", name="older")
        got["c_switch_unread"] = self.do(cs, "log.switch", name="older")
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
        got["c_said"] = on_host(said)
        # Another who joins the branch's document writes what C made there
        # (ruled 2026-10-14), and moves the document from it; what C made is
        # C's to delete
        ds = self.join("serve-log-acts-d", doc=name, identity=OTHER)
        got["d_branches"] = self.do(ds, "log.branches")
        got["d_delete"] = self.do(ds, "log.deleteBranch", name="c-try")
        got["d_switch"] = self.do(ds, "log.switch", name="for-ann")
        got["d_host_switch"] = on_host(host(name))
        got["d_switch_back"] = self.do(ds, "log.switch", name="c-root")
        got["d_host"] = on_host(host(name))
        ds.close()
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

        # The work asked back, and merged by the host in the first document
        got["c_request"] = self.do(cs, "log.request", source="for-ann", target=first,
                                   note="what C did")
        back = (got["c_request"] or {}).get("request", 0)
        got["c_requests"] = self.do(cs, "log.requests")
        got["c_merge"] = self.do(cs, "log.mergeRequest", request=back)
        hs = self.join("serve-log-acts-h2", identity=HOST)
        got["h2_requests"] = ask(hs, {"id": 1, "op": "log.requests"})
        got["c_preview"] = self.do(cs, "log.preview", request=back)
        got["h2_preview"] = ask(hs, {"id": 7, "op": "log.preview", "request": back})
        got["h2_merge"] = ask(hs, {"id": 2, "op": "log.mergeRequest", "request": back})
        if (got["h2_merge"] or {}).get("code") == "Conflicts":
            got["h2_merge"] = ask(hs, {"id": 3, "op": "log.mergeRequest", "request": back,
                                       "fallback": "theirs"})
        got["main_merged"] = on_host(host())
        got["c_requests_end"] = self.do(cs, "log.requests")

        # A merge undone, and rolled back: the host's, not another's. One of
        # its own, which writes a row (that of C's work went as it was),
        # undone by the connection that made it: an undo step is its
        # author's
        got["rb_branch"] = ask(hs, {"id": 9, "op": "log.branch", "name": "rb"})
        on_host(change("on rb", Width=7))
        got["rb_switch"] = ask(hs, {"id": 10, "op": "log.switch", "name": first})
        on_host(change("on first", Length=9))
        rq = (ask(hs, {"id": 11, "op": "log.request", "source": "rb", "target": first})
              or {}).get("request", 0)
        got["rb_merge"] = ask(hs, {"id": 12, "op": "log.mergeRequest", "request": rq})
        got["h2_undo"] = ask(hs, {"id": 8, "op": "undo"})
        hs.drain(0.5)
        got["undo_merge"] = on_host(undo_main_state)
        es = self.join("serve-log-acts-e")
        got["e_dry"] = self.do(es, "log.rollBack", dry=True)
        got["e_rollback"] = self.do(es, "log.rollBack")
        es.close()
        got["h2_dry"] = ask(hs, {"id": 4, "op": "log.rollBack", "dry": True})
        got["h2_rollback"] = ask(hs, {"id": 5, "op": "log.rollBack"})
        got["h2_rollback_again"] = ask(hs, {"id": 6, "op": "log.rollBack"})
        got["main_rolled"] = on_host(undo_main_state)
        hs.close()
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
        QtCore.QTimer.singleShot(360000, lambda: (check("the run ended", state["done"]), finish()))
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
    check("the client is told it reads the branch and does not write it, and that the rows "
          "are another's",
          rows.get("host") is False and rows.get("write") is False and "floor" not in rows
          and all(r.get("mine") is False and r.get("within") is False
                  for r in rows.get("rows", []) if r.get("ops")),
          (rows.get("host"), rows.get("write"),
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
          and "write" in (got.get("a_branch") or {}).get("message", "")
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
          and got["branches0"].get("write") is False and b0[0].get("write") is False
          and (got.get("h_branches0") or {}).get("host") is True,
          (got.get("branches0"), (got.get("h_branches0") or {}).get("host")))
    hb = got.get("host_branch", {})
    check("the host's new branch from here is the one the document is on, with nothing moved",
          ok(got.get("branch")) and got["branch"].get("branchName") == "try"
          and hb.get("box") == hr.get("box")
          and ("try", True) in hb.get("branches", []), (got.get("branch"), hb.get("branches")))
    listed_b = {b["name"]: b for b in (got.get("branches") or {}).get("branches", [])}
    check("the client is listed the branch the document is on now, and no other",
          set(listed_b) == {"try"} and listed_b["try"].get("current") is True
          and listed_b["try"].get("write") is False, listed_b)
    check("renaming it is refused the client, and the branch it does not read is unknown to it",
          forbidden(got, "a_rename") and code(got.get("a_switch")) == "UnknownBranch"
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
    # a branch that is not read, then read
    unread = [b["name"] for b in (got.get("a_branches_unread") or {}).get("branches", [])]
    rows_unread = [r["name"] for r in (got.get("a_rows_unread") or {}).get("rows", [])]
    check("back on the first branch the client is listed that one alone, and no row of the "
          "other",
          unread == [first] and "on try" not in rows_unread and "taller" in rows_unread,
          (unread, rows_unread[:5]))
    check("a row and a branch it does not read are unknown to it",
          code(got.get("a_apply_unread")) == "UnknownRow"
          and code(got.get("a_switch_unread")) == "UnknownBranch"
          and code(got.get("a_request_unread")) == "UnknownBranch",
          (got.get("a_apply_unread"), got.get("a_switch_unread"), got.get("a_request_unread")))
    check("nor are its rows by their number, to take back or to be shown",
          code(got.get("a_undo_unread")) == "UnknownRow"
          and code(got.get("a_edit_unread")) == "UnknownRow"
          and got.get("host_edit_unread", {}).get("edit") is False,
          (got.get("a_undo_unread"), got.get("a_edit_unread")))
    v_unread = [v["name"] for v in (got.get("a_versions_unread") or {}).get("versions", [])]
    v_read = {v["name"]: v for v in (got.get("a_versions_read") or {}).get("versions", [])}
    check("nor a version taken on it, to be listed, named or gone to -- until it is read",
          ok(got.get("h_try_version")) and "try-v" not in v_unread and "kept" in v_unread
          and code(got.get("a_name_unread")) == "UnknownVersion"
          and code(got.get("a_restore_unread")) == "UnknownVersion"
          and v_read.get("try-v", {}).get("branchName") == "try",
          (v_unread, got.get("a_name_unread"), got.get("a_restore_unread"),
           v_read.get("try-v")))
    r0 = (got.get("h_request_unread") or {}).get("request", 0)
    check("nor a request of it, to be listed, withdrawn or merged",
          r0 > 0
          and r0 not in [r["id"] for r in (got.get("a_requests_unread") or {})
                         .get("requests", [])]
          and code(got.get("a_withdraw_unread")) == "UnknownRequest"
          and code(got.get("a_merge_unread")) == "UnknownRequest"
          and ok(got.get("h_withdraw_unread")),
          (got.get("h_request_unread"), got.get("a_requests_unread"),
           got.get("a_withdraw_unread"), got.get("a_merge_unread")))
    read = {b["name"]: b for b in (got.get("a_branches_read") or {}).get("branches", [])}
    rows_read = {r["name"]: r for r in (got.get("a_rows_read") or {}).get("rows", [])}
    check("given to read it, the client is listed the branch, which it does not write",
          ("verified", wsclient.IDENTITY, "try", False) in (got.get("granted") or [])
          and set(read) == {first, "try"} and read["try"].get("write") is False
          and read["try"].get("made") is False, (got.get("granted"), read))
    check("and its rows, as another branch's",
          rows_read.get("on try", {}).get("here") is False
          and rows_read["on try"].get("branchName") == "try", rows_read.get("on try"))
    ap = got.get("host_applied", {})
    picked = [r for r in (got.get("rows_applied") or {}).get("rows", [])
              if r.get("kind") == "pick"]
    check("a row of this branch's own is not applied to it",
          code(got.get("a_apply_here")) == "Here", got.get("a_apply_here"))
    check("a row of the branch it reads is applied here: what it set, as a row of the client's "
          "own",
          ok(got.get("a_apply")) and got["a_apply"].get("row", 0) > 0
          and ap.get("box") == (14, 10, 40) and len(picked) == 1
          and picked[0].get("author") == wsclient.IDENTITY and picked[0].get("mine") is True
          and ("try", False) in ap.get("branches", []),
          (got.get("a_apply"), ap.get("box"), [(r.get("name"), r.get("author")) for r in picked]))
    conflict = got.get("a_apply_conflict") or {}
    check("where it meets what was set here since it is refused, with how many",
          got.get("host_moved_on", {}).get("box") == (14, 10, 50)
          and code(conflict) == "Conflicts" and conflict.get("conflicts") == 1
          and got.get("host_conflict") == got.get("host_moved_on"), conflict)
    preview = got.get("a_preview") or {}
    conflicts = [c for c in preview.get("changes", []) if c.get("kind") == "conflict"]
    check("its preview lists the conflict, by its key, with the values on each side",
          ok(preview) and preview.get("what") == "rows" and preview.get("conflicts") == 1
          and len(conflicts) == 1 and conflicts[0].get("key") == "Box.Height"
          and conflicts[0].get("prop") == "Height" and "50" in conflicts[0].get("ours", "")
          and "40" in conflicts[0].get("theirs", ""), preview)
    check("a side picked for each, it goes on: ours keeps what is here",
          ok(got.get("a_apply_ours")) and got.get("host_ours", {}).get("box") == (14, 10, 50),
          (got.get("a_apply_ours"), got.get("host_ours", {}).get("box")))
    check("and with a side said for them it is applied",
          ok(got.get("a_apply_theirs")) and got.get("host_theirs", {}).get("box") == (14, 10, 40),
          (got.get("a_apply_theirs"), got.get("host_theirs", {}).get("box")))

    # requests to merge
    r1 = (got.get("a_request") or {}).get("request", 0)
    mine_list = {r["id"]: r for r in (got.get("a_requests") or {}).get("requests", [])}
    check("the client asks for the branch it reads to be merged into this one",
          ok(got.get("a_request")) and r1 > 0 and mine_list.get(r1, {}).get("source") == "try"
          and mine_list[r1].get("target") == first and mine_list[r1].get("state") == "open"
          and mine_list[r1].get("by") == wsclient.IDENTITY
          and mine_list[r1].get("note") == "the taller box",
          (got.get("a_request"), mine_list.get(r1)))
    check("the same again is refused, as is a branch into itself",
          code(got.get("a_request_again")) == "Refused"
          and code(got.get("a_request_self")) == "Refused",
          (got.get("a_request_again"), got.get("a_request_self")))
    hosts_list = {r["id"]: r for r in (got.get("h_requests") or {}).get("requests", [])}
    check("it is the client's own to withdraw, and not its to merge or reject; the host's to "
          "do both",
          mine_list.get(r1, {}).get("mine") is True and mine_list[r1].get("handles") is False
          and mine_list[r1].get("here") is True and forbidden(got, "a_merge", "a_reject")
          and hosts_list.get(r1, {}).get("handles") is True
          and hosts_list[r1].get("mine") is False,
          (mine_list.get(r1), got.get("a_merge"), got.get("a_reject"), hosts_list.get(r1)))
    r2 = (got.get("a_request2") or {}).get("request", 0)
    check("the client withdraws it, once, and may then ask again",
          ok(got.get("a_withdraw")) and code(got.get("a_withdraw_again")) == "Refused"
          and ok(got.get("a_request2")) and r2 > r1,
          (got.get("a_withdraw"), got.get("a_withdraw_again"), got.get("a_request2")))
    hm = got.get("host_merged", {})
    check("the host merges what is asked: the branch is merged into the one the document is on",
          ok(got.get("h_merge")) and hm.get("box") == (14, 10, 40)
          and any(r[0] == r2 and r[4] == "merged" for r in hm.get("requests", [])),
          (got.get("h_merge"), hm.get("box"), hm.get("requests")))
    r3 = (got.get("a_request3") or {}).get("request", 0)
    end = {r["id"]: r for r in (got.get("a_requests_end") or {}).get("requests", [])}
    check("one that ended is not merged again; another the host rejects; one there is none "
          "of is said so",
          code(got.get("h_merge_again")) == "Refused" and ok(got.get("a_request3"))
          and ok(got.get("h_reject")) and code(got.get("h_norequest")) == "UnknownRequest",
          (got.get("h_merge_again"), got.get("h_reject"), got.get("h_norequest")))
    check("and the client is listed how each ended, and by whom",
          [end.get(i, {}).get("state") for i in (r1, r2, r3)]
          == ["withdrawn", "merged", "rejected"]
          and end.get(r1, {}).get("closedBy") == wsclient.IDENTITY
          and end.get(r2, {}).get("closedBy") == HOST
          and [r[4] for r in got.get("host_requests", {}).get("requests", [])
               if r[0] in (r1, r2, r3)]
          == ["withdrawn", "merged", "rejected"],
          ([(i, end.get(i, {}).get("state"), end.get(i, {}).get("closedBy"))
            for i in (r1, r2, r3)], got.get("host_requests", {}).get("requests")))

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
          and (first, True) in hl.get("branches", []) and hl.get("box") == (14, 10, 40),
          (got.get("switch_left"), hl.get("branches"), hl.get("box")))

    # deleting
    check("deleting a branch is refused the client",
          code(got.get("delete")) == "Forbidden"
          and got.get("host_delete", {}).get("branches") == hl.get("branches"),
          (got.get("delete"), got.get("host_delete", {}).get("branches")))
    hd = got.get("host_deleted", {})
    check("the host does, and what was said of the branch goes with it",
          ok(got.get("h_delete")) and not any(n == "try" for n, _ in hd.get("branches", []))
          and got.get("host_end") is not None,
          (got.get("h_delete"), hd.get("branches")))
    check("not the one the document is on, and not one that is gone",
          code(got.get("h_delete_current")) == "Refused"
          and code(got.get("h_delete_none")) == "UnknownBranch",
          (got.get("h_delete_current"), got.get("h_delete_none")))
    after = sorted(b["name"] for b in (got.get("branches_after") or {}).get("branches", []))
    check("the client lists the branch it reads still", after == [first], after)

    # who made a branch
    hosts = {b["name"]: b for b in (got.get("a_branches_hosts") or {}).get("branches", [])}
    check("given to write the first branch, a branch the host makes from it is not the "
          "client's: it is unknown to it",
          ("verified", wsclient.IDENTITY, first, True) in (got.get("granted_write") or [])
          and ok(got.get("h_branch_own")) and ok(got.get("h_switch_own"))
          and hosts.get(first, {}).get("write") is True and "hosts-own" not in hosts
          and code(got.get("a_switch_hosts")) == "UnknownBranch"
          and code(got.get("a_delete_hosts")) == "UnknownBranch",
          (sorted(hosts), got.get("a_switch_hosts"), got.get("a_delete_hosts")))
    own = {b["name"]: b for b in (got.get("a_branches_own") or {}).get("branches", [])}
    hk = got.get("host_maker", {})
    check("one the client makes is its own, said in the log: written, and its to delete",
          ok(got.get("a_branch_own")) and own.get("a-own", {}).get("made") is True
          and own["a-own"].get("write") is True and own["a-own"].get("current") is True
          and own.get(first, {}).get("made") is False
          and ("verified", wsclient.IDENTITY, "a-own", True, True) in (got.get("said_own") or [])
          and ok(got.get("a_switch_back")) and ok(got.get("a_delete_own"))
          and not any(s[2] == "a-own" for s in (got.get("said_deleted") or [("", "", "a-own")]))
          and ok(got.get("h_delete_hosts")) and got.get("ungranted") is True
          and not any(n in ("a-own", "hosts-own") for n, _ in hk.get("branches", [("a-own", 0)]))
          and (first, True) in hk.get("branches", []),
          (got.get("a_branch_own"), own.get("a-own"), got.get("said_own"),
           got.get("a_delete_own"), got.get("said_deleted"), hk.get("branches")))

    check("the client that may only look is refused all fifteen",
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
          after_serve.get("box") == before.get("box") == (15, 10, 40)
          and (first, True) in after_serve.get("branches", [])
          and ("for-ann", False) in after_serve.get("branches", []),
          (after_serve.get("box"), after_serve.get("branches")))

    rows = got.get("c_rows") or {}
    floor = rows.get("floor", 0)
    listed = rows.get("rows", [])
    c0 = got.get("c_host0", {})
    check("its client is told it writes the branch, and from where",
          ok(rows) and rows.get("write") is True and rows.get("host") is False and floor > 0
          and rows.get("branchName") == "for-ann" and c0.get("box") == (15, 10, 40)
          and ("for-ann", True) in c0.get("branches", []),
          (rows.get("write"), rows.get("host"), floor, rows.get("branchName"),
           c0.get("branches")))
    old = [r for r in listed if r["seq"] < floor]
    new = [r for r in listed if r["seq"] >= floor]
    check("the rows from before are listed, as ones the document is not moved from",
          len(old) >= 8 and all(r.get("within") is False for r in old)
          and all(r.get("within") is True for r in new)
          and any(r.get("mine") for r in old),
          ([(r["seq"], r.get("within"), r.get("mine")) for r in listed][:6], floor))
    branches = {b["name"]: b for b in (got.get("c_branches") or {}).get("branches", [])}
    check("it is listed the branch it was served, which it writes, and the one that was made "
          "from, which it reads",
          (got.get("c_branches") or {}).get("write") is True
          and set(branches) == {"for-ann", first}
          and branches["for-ann"].get("write") is True
          and branches["for-ann"].get("made") is False
          and branches["for-ann"].get("floor") == floor
          and branches["for-ann"].get("current") is True
          and branches[first].get("write") is False,
          {n: (b.get("write"), b.get("made"), b.get("floor")) for n, b in branches.items()})
    versions = {v["name"]: v for v in (got.get("c_versions") or {}).get("versions", [])}
    at_floor = [v for v in (got.get("c_versions") or {}).get("versions", [])
                if v.get("within")]
    # (the version at the serving point is the one the first document was
    # last at: only records came after it, so its row is not after the fork)
    check("and the versions: the one it was served from is its to go to, an older one is not",
          versions.get("kept", {}).get("within") is False and len(at_floor) == 1
          and at_floor[0]["seq"] <= floor,
          [(v["num"], v["name"], v["seq"], v.get("within"))
           for v in (got.get("c_versions") or {}).get("versions", [])])

    cu0 = got.get("c_host_undone", {})
    check("a row of its own from before the point is its to take back",
          ok(got.get("c_undo_before")) and cu0.get("box") == (14, 10, 40),
          (got.get("c_undo_before"), cu0.get("box")))
    cb = got.get("c_host_before", {})
    check("a version from before is neither restored nor named",
          forbidden(got, "c_restore_before", "c_name_before")
          and "before" in (got.get("c_restore_before") or {}).get("message", ""),
          (got.get("c_restore_before"), got.get("c_name_before")))
    check("no branch is made from a row or a version from before",
          forbidden(got, "c_branch_row_before", "c_branch_version_before"),
          (got.get("c_branch_row_before"), got.get("c_branch_version_before")))
    check("the branch it only reads is not switched to, renamed or deleted",
          forbidden(got, "c_switch_before", "c_rename_before", "c_delete_before"),
          (got.get("c_switch_before"), got.get("c_rename_before"),
           got.get("c_delete_before")))
    check("and one it does not read is unknown to it",
          code(got.get("c_delete_unread")) == "UnknownBranch"
          and code(got.get("c_switch_unread")) == "UnknownBranch",
          (got.get("c_delete_unread"), got.get("c_switch_unread")))
    check("none of which moved the document", cb == cu0,
          (cb.get("box"), cb.get("branches"), len(cb.get("rows", [])),
           len(cu0.get("rows", []))))

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
          ok(got.get("c_branch_floor")) and cf.get("box") == (15, 10, 40)
          and ("c-root", True) in cf.get("branches", []),
          (got.get("c_branch_floor"), cf.get("box"), cf.get("branches")))
    check("and the branches it made are its own, the one it was given is not of its making",
          made.get("c-root", {}).get("made") is True and made["c-root"].get("write") is True
          and made.get("c-try", {}).get("made") is True
          and made.get("for-ann", {}).get("made") is False
          and made.get(first, {}).get("write") is False,
          {n: (b.get("write"), b.get("made")) for n, b in made.items()})
    c_said = got.get("c_said") or []
    theirs = {b["name"]: b for b in (got.get("d_branches") or {}).get("branches", [])}
    check("which the log says, of C by name",
          ("verified", wsclient.IDENTITY, "c-root", True, True) in c_said
          and ("verified", wsclient.IDENTITY, "c-try", True, True) in c_said
          and not any(s[2] == "for-ann" for s in c_said), c_said)
    check("another who joins the document writes the branch that was served, and those C "
          "made there, which are not its own",
          theirs.get("for-ann", {}).get("write") is True
          and theirs["for-ann"].get("made") is False
          and theirs.get("c-root", {}).get("current") is True
          and theirs["c-root"].get("write") is True and theirs["c-root"].get("made") is False
          and theirs.get("c-try", {}).get("write") is True
          and theirs["c-try"].get("made") is False,
          {n: (b.get("write"), b.get("made")) for n, b in theirs.items()})
    ds_, dh = got.get("d_host_switch", {}), got.get("d_host", {})
    check("it moves the document from one C made, and back; deleting it is C's",
          ok(got.get("d_switch")) and ("for-ann", True) in ds_.get("branches", [])
          and ok(got.get("d_switch_back")) and dh.get("branches") == cf.get("branches")
          and code(got.get("d_delete")) == "Forbidden"
          and "maker" in (got.get("d_delete") or {}).get("message", ""),
          (got.get("d_switch"), ds_.get("branches"), got.get("d_switch_back"),
           got.get("d_delete")))
    cr = got.get("c_host_restore", {})
    check("restores a version of its own",
          ok(got.get("c_restore")) and cr.get("box") == (14, 10, 25),
          (got.get("c_restore"), cr.get("box")))
    cd = got.get("c_host_deleted", {})
    check("renames and deletes a branch of its own making",
          ok(got.get("c_rename")) and ok(got.get("c_delete"))
          and not any(n in ("c-try", "c-tried") for n, _ in cd.get("branches", [])),
          (got.get("c_rename"), got.get("c_delete"), cd.get("branches")))
    check("but not the one it was given",
          forbidden(got, "c_delete_root")
          and "given" in (got.get("c_delete_root") or {}).get("message", "")
          and any(n == "for-ann" for n, _ in cd.get("branches", [])),
          (got.get("c_delete_root"), cd.get("branches")))
    cu = got.get("c_host_undo", {})
    check("and takes back a row of its own that is not the last",
          ok(got.get("c_write3")) and ok(got.get("c_write4")) and ok(got.get("c_undo"))
          and cu.get("box") == (44, 10, 25), (got.get("c_undo"), cu.get("box")))
    ma = got.get("main_after", {})
    check("none of it moved the first document",
          ma.get("box") == (15, 10, 40) and (first, True) in ma.get("branches", [])
          and ma.get("versions", {}).get(got.get("snapshot", {}).get("version"))
          == ("named", "kept"),
          (ma.get("box"), ma.get("branches")))

    back = (got.get("c_request") or {}).get("request", 0)
    asked = {r["id"]: r for r in (got.get("c_requests") or {}).get("requests", [])}
    check("the client asks for its branch to be merged back, which is not its to do",
          ok(got.get("c_request")) and asked.get(back, {}).get("source") == "for-ann"
          and asked[back].get("target") == first and asked[back].get("mine") is True
          and asked[back].get("handles") is False and asked[back].get("here") is False
          and forbidden(got, "c_merge"),
          (got.get("c_request"), asked.get(back), got.get("c_merge")))
    seen = {r["id"]: r for r in (got.get("h2_requests") or {}).get("requests", [])}
    mm = got.get("main_merged", {})
    ended = {r["id"]: r for r in (got.get("c_requests_end") or {}).get("requests", [])}
    check("the host does it in the first document, which then has what the client did there",
          seen.get(back, {}).get("handles") is True and seen[back].get("here") is True
          and ok(got.get("h2_merge")) and mm.get("box") == (14, 10, 25)
          and (first, True) in mm.get("branches", [])
          and ended.get(back, {}).get("state") == "merged"
          and ended[back].get("closedBy") == HOST,
          (seen.get(back), got.get("h2_merge"), mm.get("box"), ended.get(back)))
    hp = got.get("h2_preview") or {}
    check("its preview, asked where the document is on its target, says what it would merge; "
          "asked elsewhere, it says so",
          ok(hp) and hp.get("what") == "merge" and hp.get("source") == "for-ann"
          and len(hp.get("changes", [])) > 0
          and code(got.get("c_preview")) == "Refused",
          (hp.get("what"), hp.get("source"), len(hp.get("changes", [])),
           got.get("c_preview")))
    um, mr = got.get("undo_merge") or {}, got.get("main_rolled") or {}
    dry = got.get("h2_dry") or {}
    check("the merge undone, another who reads the branch may not roll it back",
          len(um.get("merges", [])) >= 1 and len(um.get("redo", [])) >= 1
          and (got.get("e_dry") or {}).get("may") is False
          and code(got.get("e_rollback")) == "Forbidden",
          (um, got.get("rb_merge"), got.get("h2_undo"), got.get("e_dry"),
           got.get("e_rollback")))
    check("the host does: its rows leave the log, the merge is no longer there to redo",
          ok(dry) and dry.get("may") is True and dry.get("source") == "rb"
          and ok(got.get("h2_rollback")) and (got.get("h2_rollback") or {}).get("rows", 0) >= 2
          and len(mr.get("merges", [])) == len(um.get("merges", [])) - 1
          and len(mr.get("redo", [])) == len(um.get("redo", [])) - 1
          and code(got.get("h2_rollback_again")) == "Refused",
          (dry, got.get("h2_rollback"), mr, got.get("h2_rollback_again")))


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
