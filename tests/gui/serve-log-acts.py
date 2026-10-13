"""A client of a served document acts on its transaction log: rows taken
back, versions, branches.

docs/TransactionLog.md sec 32.22. The log's panel in a browser lists what
the host answers (sec 32.21, tests/gui/serve-log.py); this is what it does
with it, each an op the host does or refuses with the reason
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
  log.deleteBranch  the host's to do: nothing takes it back

A document served with no window. Client A may edit and states a camera;
client B came by a link that may only look; client H is the host. Asserted:

  - a row that is not the last is taken back, as one new row; one that a
    row since set the same of is refused with the reason, as is the same
    row again, and nothing is written; two rows are one step; a record, a
    row there is none of and no rows are each said so;
  - a snapshot with a name is a named version, the document as it stands;
    it is renamed, unnamed, named again; after a change the document is
    restored to it, as a row of its own;
  - a new branch from here is the one the document is on, with nothing
    moved; a change there is not here after switching back, and its row is
    another branch's, which is not taken back from here; a name taken is
    refused; a branch from a row is the document as that row left it;
    a branch is renamed;
  - with an edit open the document is not switched, and is once it is left;
  - A is told when its own acts move the log;
  - deleting a branch is refused A and done for H;
  - B is refused all ten.

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


def host():
    """What the host has: the box, the rows, the branches, the versions."""
    doc = FreeCAD.getDocument(DOC)
    box = doc.getObject("Box")
    return {
        "box": (round(box.Length.Value), round(box.Width.Value), round(box.Height.Value)),
        "rows": [(r["seq"], r["name"], r["kind"]) for r in doc.getTransactionLog()],
        "branches": sorted((b["name"], bool(b["current"]))
                           for b in doc.getTransactionBranches()),
        "versions": {v["num"]: (v["kind"], v["name"]) for v in doc.getTransactionVersions()},
        "edit": FreeCADGui.getDocument(DOC).getInEdit() is not None,
    }


def change(name, **dims):
    def run():
        doc = FreeCAD.getDocument(DOC)
        doc.openTransaction(name)
        for prop, value in dims.items():
            setattr(doc.getObject("Box"), prop, value)
        doc.recompute()
        doc.commitTransaction()
        return host()
    return run


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


class Client(threading.Thread):
    """Client A: may edit, states a camera, acts on the log."""

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

    def talk(self):
        got = self.got
        ws = WS(self.port)
        ws.hello("serve-log-acts-a")
        if ws.next_binary(20.0) is None:
            raise RuntimeError("no snapshot")
        ws.next_binary(0.5)
        ws.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH))
        ws.drain(0.5)
        self.watcher.ready.wait(30.0)

        got["rows"] = self.do(ws, "log.rows")
        by_name = {r["name"]: r["seq"] for r in (got["rows"] or {}).get("rows", [])}
        record = [r["seq"] for r in (got["rows"] or {}).get("rows", []) if r["ops"] == 0]
        got["host0"] = on_host(host)

        # Rows that are not the last, taken back
        got["undo_set_since"] = self.do(ws, "log.undo", rows=[by_name.get("taller", 0)])
        got["host_set_since"] = on_host(host)
        mark = len(ws.pushes)
        got["undo"] = self.do(ws, "log.undo", rows=[by_name.get("wider", 0)])
        end = time.perf_counter() + 5.0
        while time.perf_counter() < end and not log_pushes(ws.pushes[mark:]):
            text = ws.next_text(0.3)
            if text is not None:
                ws.pushes.append(text)
        got["told"] = log_pushes(ws.pushes[mark:])
        got["host_undo"] = on_host(host)
        got["rows_undo"] = self.do(ws, "log.rows", limit=4)
        got["undo_again"] = self.do(ws, "log.undo", rows=[by_name.get("wider", 0)])
        got["host_again"] = on_host(host)
        got["undo_two"] = self.do(ws, "log.undo",
                                  rows=[by_name.get("last", 0), by_name.get("deeper", 0)])
        got["host_two"] = on_host(host)
        got["undo_record"] = self.do(ws, "log.undo", rows=record[:1])
        got["undo_norow"] = self.do(ws, "log.undo", rows=[999999])
        got["undo_none"] = self.do(ws, "log.undo", rows=[])
        got["host_refused"] = on_host(host)

        # Versions
        got["snapshot"] = self.do(ws, "log.snapshot", name="kept")
        num = (got["snapshot"] or {}).get("version", 0)
        got["versions"] = self.do(ws, "log.versions")
        got["rename"] = self.do(ws, "log.nameVersion", version=num, name="kept well")
        got["host_rename"] = on_host(host)
        got["unname"] = self.do(ws, "log.nameVersion", version=num, name="")
        got["host_unname"] = on_host(host)
        got["name"] = self.do(ws, "log.nameVersion", version=num, name="kept")
        got["name_none"] = self.do(ws, "log.nameVersion", version=999999, name="x")
        got["host_changed"] = on_host(change("taller still", Height=30))
        got["versions_changed"] = self.do(ws, "log.versions")
        got["restore"] = self.do(ws, "log.restore", version=num)
        got["host_restore"] = on_host(host)
        got["restore_none"] = self.do(ws, "log.restore", version=999999)

        # Branches
        got["branches0"] = self.do(ws, "log.branches")
        first = [b["name"] for b in (got["branches0"] or {}).get("branches", [])
                 if b.get("current")]
        first = first[0] if first else ""
        got["first"] = first
        got["branch"] = self.do(ws, "log.branch", name="try")
        got["host_branch"] = on_host(host)
        got["branches"] = self.do(ws, "log.branches")
        got["host_on_try"] = on_host(change("on try", Height=40))
        got["switch"] = self.do(ws, "log.switch", name=first)
        got["host_switch"] = on_host(host)
        got["rows_switch"] = self.do(ws, "log.rows")
        away = [r["seq"] for r in (got["rows_switch"] or {}).get("rows", [])
                if r["name"] == "on try"]
        got["undo_away"] = self.do(ws, "log.undo", rows=away[:1])
        got["branch_taken"] = self.do(ws, "log.branch", name="try")
        got["branch_noname"] = self.do(ws, "log.branch", name="  ")
        got["branch_row"] = self.do(ws, "log.branch", name="old", row=by_name.get("wider", 0))
        got["host_row"] = on_host(host)
        got["rename_branch"] = self.do(ws, "log.renameBranch", name="old", to="older")
        got["rename_taken"] = self.do(ws, "log.renameBranch", name="older", to="try")
        got["host_renamed"] = on_host(host)
        got["switch_none"] = self.do(ws, "log.switch", name="no-such-branch")

        # With an edit open the document is not checked out under it
        got["edit"] = self.do(ws, "edit", obj="Cut", mode=MODE, row=by_name.get("taller", 0))
        ws.drain(1.0)
        got["host_edit"] = on_host(host)
        got["switch_editing"] = self.do(ws, "log.switch", name=first)
        got["branch_editing"] = self.do(ws, "log.branch", name="other",
                                        row=by_name.get("base", 0))
        got["host_editing"] = on_host(host)
        self.do(ws, "resetEdit")
        ws.drain(0.5)
        got["switch_left"] = self.do(ws, "log.switch", name=first)
        got["host_left"] = on_host(host)

        # Deleting a branch is the host's
        got["delete"] = self.do(ws, "log.deleteBranch", name="try")
        got["host_delete"] = on_host(host)
        hs = WS(self.port, headers="%s: %s\r\n" % (wsclient.IDENTITY_HEADER, HOST))
        hs.hello("serve-log-acts-h")
        hs.next_binary(20.0)
        got["h_branches"] = ask(hs, {"id": 1, "op": "log.branches"})
        got["h_delete_current"] = ask(hs, {"id": 2, "op": "log.deleteBranch", "name": first})
        got["h_delete"] = ask(hs, {"id": 3, "op": "log.deleteBranch", "name": "try"})
        got["h_delete_none"] = ask(hs, {"id": 4, "op": "log.deleteBranch", "name": "try"})
        got["host_deleted"] = on_host(host)
        got["branches_after"] = self.do(ws, "log.branches")
        hs.close()
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
        QtCore.QTimer.singleShot(200000, lambda: (check("the run ended", state["done"]), finish()))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
        finish()


def code(reply):
    return (reply or {}).get("code")


def ok(reply):
    return (reply or {}).get("ok") is True


def verify():
    if state["done"]:
        return
    try:
        client, watcher = state["client"], state["watcher"]
        finished.set()
        got = client.got
        if client.error or watcher.error:
            check("the clients ran", False, client.error or watcher.error)
        h0 = got.get("host0", {})
        named = {r["name"]: r["seq"] for r in (got.get("rows") or {}).get("rows", [])}
        check("the box as it was built", h0.get("box") == (14, 12, 18), h0.get("box"))

        # rows taken back
        since = got.get("undo_set_since") or {}
        check("a row that a row since set the same of is not taken back: the reason is said",
              code(since) == "Refused" and len(since.get("message", "")) > 8
              and got.get("host_set_since") == h0, since)
        hu = got.get("host_undo", {})
        new = hu.get("rows", [])[len(h0.get("rows", [])):]
        check("a row that is not the last is taken back: what it set, and nothing else",
              ok(got.get("undo")) and hu.get("box") == (10, 12, 18)
              and got["undo"].get("head") == got["undo"].get("last"),
              (got.get("undo"), hu.get("box")))
        steps = [r for r in new if r[2] == "undo"]
        check("as one new row, named by what it undid",
              len(steps) == 1 and steps[0][1] == "Undo wider", new)
        listed = [r for r in (got.get("rows_undo") or {}).get("rows", [])
                  if r.get("kind") == "undo"]
        check("which is the client's own, and inverts the row",
              len(listed) == 1 and listed[0].get("author") == wsclient.IDENTITY
              and listed[0].get("inverts") == named.get("wider"),
              [(r.get("name"), r.get("author"), r.get("authorKind"), r.get("inverts"))
               for r in listed])
        check("the client is told when its own act moves the log",
              got.get("told") and got["told"][0] == DOC, got.get("told"))
        again = got.get("undo_again") or {}
        check("the same row again is refused, and nothing is written",
              code(again) == "Refused" and got.get("host_again") == hu, again)
        ht = got.get("host_two", {})
        two = ht.get("rows", [])[len(hu.get("rows", [])):]
        steps = [r for r in two if r[2] == "undo"]
        check("two rows are taken back as one step, named by both",
              ok(got.get("undo_two")) and ht.get("box") == (10, 10, 16) and len(steps) == 1
              and "deeper" in steps[0][1] and "last" in steps[0][1],
              (got.get("undo_two"), ht.get("box"), two))
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
        check("a snapshot with a name is a named version",
              ok(snap) and num > 0 and versions.get(num, {}).get("named") is True
              and versions[num].get("name") == "kept", (snap, versions.get(num)))
        check("which is the document as it stands, of this history",
              versions.get(num, {}).get("head") is True and versions[num].get("here") is True
              and sum(1 for v in versions.values() if v.get("head")) >= 1, versions.get(num))
        check("it is renamed", ok(got.get("rename"))
              and got.get("host_rename", {}).get("versions", {}).get(num) == ("named",
                                                                              "kept well"),
              (got.get("rename"), got.get("host_rename", {}).get("versions", {}).get(num)))
        check("and unnamed again", ok(got.get("unname"))
              and got.get("host_unname", {}).get("versions", {}).get(num, ("", ""))[0]
              == "unnamed",
              (got.get("unname"), got.get("host_unname", {}).get("versions", {}).get(num)))
        check("a version there is none of is said so",
              code(got.get("name_none")) == "UnknownVersion"
              and code(got.get("restore_none")) == "UnknownVersion",
              (got.get("name_none"), got.get("restore_none")))
        changed = {v["num"]: v for v in (got.get("versions_changed") or {}).get("versions", [])}
        check("after a change it is no longer the document as it stands",
              got.get("host_changed", {}).get("box") == (10, 10, 30)
              and changed.get(num, {}).get("head") is False
              and changed.get(num, {}).get("name") == "kept", changed.get(num))
        hr = got.get("host_restore", {})
        restored = hr.get("rows", [])[len(got.get("host_changed", {}).get("rows", [])):]
        check("the document is restored to it, as a row of its own",
              ok(got.get("restore")) and hr.get("box") == (10, 10, 16)
              and any(r[2] == "restore" for r in restored),
              (got.get("restore"), hr.get("box"), restored))

        # branches
        first = got.get("first", "")
        b0 = (got.get("branches0") or {}).get("branches", [])
        check("the branches: the one the document is on, and deleting is not this client's",
              ok(got.get("branches0")) and len(b0) == 1 and b0[0].get("current") is True
              and first and got["branches0"].get("mayDelete") is False, got.get("branches0"))
        hb = got.get("host_branch", {})
        check("a new branch from here is the one the document is on, with nothing moved",
              ok(got.get("branch")) and got["branch"].get("branchName") == "try"
              and hb.get("box") == hr.get("box")
              and ("try", True) in hb.get("branches", []), (got.get("branch"), hb))
        listed_b = {b["name"]: b for b in (got.get("branches") or {}).get("branches", [])}
        check("and is listed so", set(listed_b) == {first, "try"}
              and listed_b.get("try", {}).get("current") is True
              and listed_b.get(first, {}).get("current") is False, listed_b)
        hs = got.get("host_switch", {})
        check("a change there is not here after switching back",
              got.get("host_on_try", {}).get("box") == (10, 10, 40) and ok(got.get("switch"))
              and got["switch"].get("branchName") == first and hs.get("box") == (10, 10, 16)
              and (first, True) in hs.get("branches", []), (got.get("switch"), hs.get("box")))
        away = [r for r in (got.get("rows_switch") or {}).get("rows", [])
                if r["name"] == "on try"]
        check("its row is listed as another branch's",
              len(away) == 1 and away[0].get("here") is False
              and away[0].get("branchName") == "try", away)
        check("and is not taken back from here",
              code(got.get("undo_away")) == "NotHere", got.get("undo_away"))
        check("a name taken is refused, and no name is a bad request",
              code(got.get("branch_taken")) == "Refused"
              and code(got.get("branch_noname")) == "BadRequest",
              (got.get("branch_taken"), got.get("branch_noname")))
        hrow = got.get("host_row", {})
        check("a branch from a row is the document as that row left it",
              ok(got.get("branch_row")) and hrow.get("box") == (14, 10, 16)
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
              and (first, True) in hl.get("branches", []) and hl.get("box") == (10, 10, 16),
              (got.get("switch_left"), hl.get("branches"), hl.get("box")))

        # deleting
        check("deleting a branch is refused the client that may edit",
              code(got.get("delete")) == "Forbidden"
              and got.get("host_delete", {}).get("branches") == hl.get("branches"),
              (got.get("delete"), got.get("host_delete", {}).get("branches")))
        hd = got.get("host_deleted", {})
        check("the host is told it may, and does: the branch and the rows only it holds",
              (got.get("h_branches") or {}).get("mayDelete") is True and ok(got.get("h_delete"))
              and got["h_delete"].get("rows", 0) >= 1
              and not any(n == "try" for n, _ in hd.get("branches", []))
              and not any(r[1] == "on try" for r in hd.get("rows", [])),
              (got.get("h_branches"), got.get("h_delete"), hd.get("branches")))
        check("not the one the document is on, and not one that is gone",
              code(got.get("h_delete_current")) == "Refused"
              and code(got.get("h_delete_none")) == "UnknownBranch",
              (got.get("h_delete_current"), got.get("h_delete_none")))
        after = sorted(b["name"] for b in (got.get("branches_after") or {}).get("branches", []))
        check("the client lists what is left", after == sorted([first, "older"]), after)

        check("the client that may only look is refused all ten",
              all(code(watcher.got.get(op)) == "ViewOnly" for op in ACTS),
              {op: code(watcher.got.get(op)) for op in ACTS})
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
