"""A real browser acts on the transaction log from its own panel: rows taken
back, a version, branches.

The browser half of docs/TransactionLog.md sec 32.22. tests/gui/
serve-log-acts.py checks the ops against synthetic socket clients: each
act, each refusal, who may. What no synthetic client can check is the page
(web/src/logpanel.tsx): that the rows chosen have their acts, that an act
asks its question in the card, that what the host answers is said, and
that the card follows the host -- the rows, the branch, the versions --
by itself after each.

So this serves a document with a box made taller, wider and deeper, serves
the built viewer over HTTP with the card open on load (`?history`), and
lets Chrome do it through scripts/logacts-drive.js. Asserted:

  - with no row chosen there are no acts on rows, and deleting a branch is
    not offered to a client that is not the host;
  - a row chosen is taken back: the card says so, lists the new row first,
    and the host has what the row set as it was before;
  - two rows chosen, the second with the key held, are one step;
  - a row taken back already is refused, in the host's words, with nothing
    written;
  - a snapshot is asked a name and listed among the versions as the
    document as it stands;
  - a branch from here is asked a name -- none is not one -- and is the
    one the card and the host are on; a row the host commits there comes
    into the card;
  - switching back from the card moves the host; the row of the other
    branch is listed as away and is not offered to be taken back;
  - after a change by the host the version kept is no longer the document
    as it stands, and restoring it from the card puts the host back;
  - the branch is renamed from the card, starting from its name.

Needs the built viewer (build/wasm/fcviewer.html; `ninja -C build/wasm`),
node, a puppeteer-core install named by PUPPETEER_PATH, and a Chrome named
by CHROME. Skips rather than fails when any is missing.

  PUPPETEER_PATH=/path/to/node_modules/puppeteer-core \\
  CHROME=~/.cache/puppeteer/chrome/*/chrome-linux64/chrome \\
  scripts/gui-test.sh tests/gui/serve-log-acts-browser.py /tmp/log-acts-web \\
      --timeout 600
"""
import functools
import http.server
import json as jsonlib
import os
import subprocess
import tempfile
import threading
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

import wsclient
from wsclient import free_port

clock = time.perf_counter

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
REPO = os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__))))
WASM = os.path.join(REPO, "build", "wasm")
DRIVER = os.path.join(REPO, "scripts", "logacts-drive.js")
DOC = "ServeLogActsBrowser"
OBJ = "Cut"
SETTLE_MS = 9000
RUN_WAIT_S = 300
# The browser acts on the document, so it is someone: the holder of an
# invitation issued to its name (docs/TransactionLog.md sec 30.6 U4, U6).
TOKEN = "log-acts-invite"
CLIENT = "log-acts"

state = {"doc": None, "done": False, "run": None, "t0": clock(), "seen_phases": 0,
         "samples": {}}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def skip(why):
    note("SKIP " + why)
    finish()


class Run(threading.Thread):
    """The browser, driven from off the GUI thread. Log and phase markers
    go to files: node buffers stdout to a pipe."""

    def __init__(self, url):
        super().__init__(daemon=True)
        self.url = url
        self.error = None
        self.log_path = os.path.join(OUT, "drive.log")
        self.phase_path = os.path.join(OUT, "phases.txt")
        self.result_path = os.path.join(OUT, "drive-result.json")

    def run(self):
        try:
            env = dict(os.environ)
            env["LD_LIBRARY_PATH"] = (
                os.path.join(REPO, ".conda", "freecad", "lib")
                + os.pathsep + env.get("LD_LIBRARY_PATH", ""))
            env["LOGACTS_PHASES"] = self.phase_path
            env["LOGACTS_RESULT"] = self.result_path
            env.setdefault("LOGACTS_SHOTS", OUT)
            with open(self.log_path, "w") as log:
                proc = subprocess.Popen(
                    ["node", DRIVER, self.url, str(SETTLE_MS)],
                    stdout=log, stderr=subprocess.STDOUT, env=env)
                proc.wait()
            if proc.returncode != 0:
                self.error = "node exited %d:\n%s" % (proc.returncode, self.tail())
        except Exception:
            self.error = traceback.format_exc()

    def tail(self, lines=25):
        try:
            with open(self.log_path) as f:
                return "".join(f.readlines()[-lines:])
        except OSError:
            return "(no driver log)"

    def phases(self):
        try:
            with open(self.phase_path) as f:
                return [line.strip() for line in f if line.strip()]
        except OSError:
            return []

    def result(self):
        try:
            with open(self.result_path) as f:
                return jsonlib.load(f)
        except Exception:
            return {}


def serve_viewer():
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=WASM)
    handler.log_message = lambda *a, **k: None
    port = free_port()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    state["httpd"] = server
    return port


def sample():
    """What the host has: the box, the rows, the branches, the versions."""
    doc = state["doc"]
    box = doc.getObject("Box")
    return {
        "box": (round(box.Length.Value), round(box.Width.Value), round(box.Height.Value)),
        "rows": [(r["seq"], r["name"], r["kind"], len(doc.getTransactionOps(r["seq"])))
                 for r in doc.getTransactionLog()],
        "branches": sorted((b["name"], bool(b["current"]))
                           for b in doc.getTransactionBranches()),
        "versions": sorted((v["kind"], v["name"]) for v in doc.getTransactionVersions()),
    }


def change(name, **dims):
    doc = state["doc"]
    doc.openTransaction(name)
    for prop, value in dims.items():
        setattr(doc.getObject("Box"), prop, value)
    doc.recompute()
    doc.commitTransaction()


def at_phase(name):
    """What the host does when the page has got so far."""
    if name == "branched":
        change("on try", Height=40)
    elif name == "switched":
        change("taller still", Height=30)


def build():
    try:
        if not os.path.exists(os.path.join(WASM, "fcviewer.html")):
            skip("no built viewer at %s (ninja -C build/wasm)" % WASM)
            return
        if not os.path.exists(DRIVER):
            skip("no driver at %s" % DRIVER)
            return
        if not os.environ.get("PUPPETEER_PATH"):
            skip("PUPPETEER_PATH is unset (a puppeteer-core install)")
            return
        if not os.environ.get("CHROME"):
            skip("CHROME is unset (a Chrome executable)")
            return

        p = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
        state["log"] = p.GetInt("TransactionLog", 2)
        p.SetInt("TransactionLog", 2)
        p.SetInt("AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        doc = FreeCAD.newDocument(DOC, hidden=True)
        state["doc"] = doc
        doc.UndoMode = 1
        doc.openTransaction("base")
        box = doc.addObject("Part::Box", "Box")
        cyl = doc.addObject("Part::Cylinder", "Cyl")
        cyl.Radius, cyl.Height = 2, 30
        cyl.Placement.Base = FreeCAD.Vector(5, 5, -10)
        cut = doc.addObject("Part::Cut", OBJ)
        cut.Base, cut.Tool = box, cyl
        doc.recompute()
        doc.commitTransaction()
        doc.saveAs(os.path.join(tempfile.mkdtemp(prefix="fc-log-acts-browser-"), "log.FCStd"))
        for name, prop, value in (("taller", "Height", 16), ("wider", "Length", 14),
                                  ("deeper", "Width", 12)):
            doc.openTransaction(name)
            setattr(box, prop, value)
            doc.recompute()
            doc.commitTransaction()

        port = free_port()
        if not check("the document is served headless",
                     FreeCADGui.serveDocument(doc, port), "port %d" % port):
            finish()
            return
        http_port = serve_viewer()
        note("viewer on %d, scene on %d" % (http_port, port))
        FreeCADGui.serveSetGrants(wsclient.invitation(TOKEN, CLIENT))
        url = ("http://127.0.0.1:%d/fcviewer.html?scene=http://127.0.0.1:%d"
               "&token=%s&client=%s&history" % (http_port, port, TOKEN, CLIENT))
        state["run"] = Run(url)
        state["t0"] = clock()
        state["run"].start()
        QtCore.QTimer.singleShot(50, poll)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def poll():
    run = state["run"]
    marked = run.phases()
    while len(marked) > state["seen_phases"]:
        name = marked[state["seen_phases"]]
        state["seen_phases"] += 1
        try:
            state["samples"][name] = sample()
            at_phase(name)
        except Exception:
            note("FAIL sample %s:\n%s" % (name, traceback.format_exc()))
    if run.is_alive():
        if clock() - state["t0"] > RUN_WAIT_S:
            check("the browser run finished", False, "still running after %ds" % RUN_WAIT_S)
            note("---- driver log")
            note(run.tail(25))
            finish()
            return
        QtCore.QTimer.singleShot(50, poll)
        return
    QtCore.QTimer.singleShot(600, verify)


def said(status, text=None, bad=False):
    status = status or {}
    return status.get("bad") is bad and (text is None or text in status.get("text", ""))


def verify():
    try:
        run = state["run"]
        out = run.result()
        s = state["samples"]
        if run.error or out.get("failure"):
            check("the browser run", False, run.error or out.get("failure"))
            note("---- driver log")
            note(run.tail(25))
        listed = s.get("listed", {})
        seq = {r[1]: r[0] for r in listed.get("rows", [])}
        first = (out.get("branch") or {}).get("value", "")

        check("the card is up, lists the rows and says the branch the document is on",
              out.get("card") is True and out.get("listed") is True
              and [r["what"] for r in out.get("rows", [])][:3] == ["deeper", "wider", "taller"]
              and first and (first, True) in listed.get("branches", [])
              and listed.get("box") == (14, 12, 16),
              ([r.get("what") for r in out.get("rows", [])], out.get("branch"),
               listed.get("branches")))
        check("with no row chosen there are no acts on rows, and deleting a branch is not "
              "offered", out.get("noActs") is True and out.get("noDelete") is True,
              (out.get("noActs"), out.get("noDelete")))

        undone = s.get("undone", {})
        new = undone.get("rows", [])[len(listed.get("rows", [])):]
        check("a row chosen is offered to be taken back, by its number",
              out.get("picked") is True
              and out.get("undoSays") == {"text": "Undo row %d" % seq.get("wider", 0),
                                          "disabled": False}, out.get("undoSays"))
        check("taken back, the card says so and lists the new row first",
              out.get("undoAsked") is True
              and said(out.get("undoSaid"), "Row %d undone" % seq.get("wider", 0))
              and out.get("undoListed") is True, (out.get("undoSaid"), out.get("undoListed")))
        check("and the host has what the row set as it was before, by one new row",
              undone.get("box") == (10, 12, 16)
              and [r[1] for r in new if r[2] == "undo"] == ["Undo wider"],
              (undone.get("box"), new))

        undone2 = s.get("undone2", {})
        new2 = [r for r in undone2.get("rows", [])[len(undone.get("rows", [])):]
                if r[2] == "undo"]
        check("two rows chosen, the second with the key held, are offered as two",
              sorted(out.get("twoPicked") or []) == ["deeper", "taller"]
              and out.get("twoSays") == {"text": "Undo 2 rows", "disabled": False}
              and (out.get("branchHereSays") or {}).get("disabled") is True,
              (out.get("twoPicked"), out.get("twoSays"), out.get("branchHereSays")))
        check("and are one step on the host",
              out.get("twoAsked") is True and said(out.get("twoSaid"), "2 rows undone")
              and undone2.get("box") == (10, 10, 10) and len(new2) == 1,
              (out.get("twoSaid"), undone2.get("box"), new2))

        refused = s.get("refused", {})
        check("a row taken back already is refused in the host's words, with nothing written",
              out.get("refusedAsked") is True and said(out.get("refusedSaid"), bad=True)
              and len((out.get("refusedSaid") or {}).get("text", "")) > 8
              and refused.get("rows") == undone2.get("rows")
              and refused.get("box") == undone2.get("box"),
              (out.get("refusedSaid"), len(refused.get("rows", [])),
               len(undone2.get("rows", []))))

        snap = s.get("snapshot", {})
        kept = [v for v in out.get("versions") or [] if v.get("name") == "kept"]
        check("a snapshot is asked a name, and the host has the named version",
              out.get("snapAsked") is True and out.get("snapAnswered") is True
              and said(out.get("snapSaid"), "kept")
              and ("named", "kept") in snap.get("versions", []),
              (out.get("snapSaid"), snap.get("versions")))
        check("which the card lists among the versions, as the document as it stands",
              out.get("versionsListed") is True and len(kept) == 1
              and kept[0].get("now") is True and kept[0].get("branch") == first, kept)

        branched = s.get("branched", {})
        check("a branch from here is asked a name, and no name is not one",
              out.get("branchAsked") is True
              and out.get("branchNoName") == {"text": "Branch", "disabled": True}
              and out.get("branchAnswered") is True, out.get("branchNoName"))
        check("it is the one the card and the host are on, with nothing moved",
              said(out.get("branchSaid"), "try")
              and (out.get("onTry") or {}).get("value") == "try"
              and sorted((out.get("onTry") or {}).get("options", [])) == sorted([first, "try"])
              and ("try", True) in branched.get("branches", [])
              and branched.get("box") == (10, 10, 10),
              (out.get("branchSaid"), out.get("onTry"), branched.get("branches")))
        check("a row the host commits there comes into the card", out.get("tryRow") is True)

        switched = s.get("switched", {})
        away = [r for r in out.get("rowsSwitched") or [] if r.get("what") == "on try"]
        check("switching back from the card moves the host",
              out.get("switched") is True and said(out.get("switchSaid"), "On branch " + first)
              and (out.get("onFirst") or {}).get("value") == first
              and (first, True) in switched.get("branches", [])
              and switched.get("box") == (10, 10, 10),
              (out.get("switchSaid"), out.get("onFirst"), switched.get("branches"),
               switched.get("box")))
        check("the row of the other branch is listed as away, and not offered to be taken back",
              len(away) == 1 and away[0].get("away") is True
              and (out.get("awaySays") or {}).get("disabled") is True,
              (away, out.get("awaySays")))

        restored = s.get("restored", {})
        changed = [v for v in out.get("versionsChanged") or [] if v.get("name") == "kept"]
        check("after a change by the host the version kept is no longer the document as it "
              "stands", len(changed) == 1 and changed[0].get("now") is False, changed)
        check("a version chosen has its acts",
              out.get("versionPicked") is True
              and out.get("versionActs") == ["Restore", "Rename...", "Unname",
                                             "Branch from here..."], out.get("versionActs"))
        check("restoring it from the card puts the host back, by a row of its own",
              out.get("restoreAsked") is True and said(out.get("restoreSaid"), "Restored to kept")
              and restored.get("box") == (10, 10, 10)
              and any(r[2] == "restore" for r in restored.get("rows", [])),
              (out.get("restoreSaid"), restored.get("box")))

        renamed = s.get("renamed", {})
        check("the branch is renamed from the card, starting from its name",
              out.get("renameAsked") is True and out.get("renameStarts") == first
              and out.get("renameAnswered") is True
              and (out.get("renamed") or {}).get("value") == "trunk"
              and ("trunk", True) in renamed.get("branches", [])
              and not any(n == first for n, _ in renamed.get("branches", [])),
              (out.get("renameStarts"), out.get("renamed"), renamed.get("branches")))
    except Exception:
        note("FAIL verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
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
