"""A real browser acts on the transaction log from its own panel: rows taken
back, a version, branches -- in a document served to it from a branch.

The browser half of docs/TransactionLog.md sec 32.22 and 32.23. tests/gui/
serve-log-acts.py checks the ops against synthetic socket clients: each
act, each refusal, who may. What no synthetic client can check is the page
(web/src/logpanel.tsx): that the rows chosen have their acts, that an act
asks its question in the card, that what the host answers is said, that
the card follows the host -- the rows, the branch, the versions -- by
itself after each, and that it offers what is the client's to do and not
the rest.

So this builds a document with a box made taller, wider and deeper, serves
a branch of it in a document of its own (Gui.serveBranch), in which the
page's user has made three rows, serves the built viewer over HTTP with
the card open on load (`?history`) and that document joined, and lets
Chrome do it through scripts/logacts-drive.js. Asserted:

  - the card lists the rows, its user's own on top and those from before
    the serving point marked; with no row chosen there are no acts on
    rows;
  - a row from before is offered neither to be taken back nor to be
    branched from, and the card says why;
  - a row of its own is taken back: the card says so, lists the new row
    first, and the host has what the row set as it was before;
  - two rows chosen, the second with the key held, are one step;
  - a row taken back already is refused, in the host's words, with nothing
    written;
  - a snapshot is asked a name and listed among the versions as the
    document as it stands, those from before marked;
  - a branch from here is asked a name -- none is not one -- and is the
    one the card and the host are on; a row the host commits there comes
    into the card;
  - switching back from the card moves the host; the branch the document
    was served from is not offered to be switched to; the row of the other
    branch is listed as away and is not offered to be taken back; the
    branch made is offered to be deleted, and no other; a branch of the
    host's the page's user does not read is listed greyed and offered for
    nothing (sec 32.30);
  - after a change by the host the version kept is no longer the document
    as it stands, and restoring it from the card puts the host back; a
    trim and a squash there are greyed as the host's (sec 32.30);
  - the branch is renamed from the card, starting from its name;
  - a row of the branch it was made from that meets what was set here
    asks one side for all, or opens the list of what it would do; a side
    picked for its conflict, it is applied with it (sec 32.26);
  - a merge the page's user asked for and made, undone on its socket, is
    rolled back from the card: the button is offered, asks, and the merge
    and its undoing leave the log while the box stays as it was (sec 32.26);
  - a value is kept in the file from its op by the page's user, who writes
    the branch -- every value of its object is the host's to keep -- and
    the op then says so, the tab counts it and the host keeps it; it is
    listed with its object and property as the page's user's, its text
    read, and let go from there (sec 32.27);
  - a copy the page's user sent, waiting unread, is listed to it as its
    own: not its to bring in, its to withdraw, which it does (sec 32.30);
  - none of it moved the document the branch was served from.

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

BRANCH = "for-web"
UNREAD = "hosts-own"

state = {"doc": None, "served": None, "done": False, "run": None, "t0": clock(),
         "seen_phases": 0, "samples": {}}


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
    """What the host has of the document the branch is served in: the box,
    the rows, the branches, the versions -- and the rows of the first."""
    doc = state["served"]
    box = doc.getObject("Box")
    first = state["doc"].getObject("Box")
    return {
        # (the log is the file's one: its rows are both documents', so the
        # first is read by what it is and where it stands)
        "main": ((round(first.Length.Value), round(first.Width.Value),
                  round(first.Height.Value)),
                 [b["name"] for b in state["doc"].getTransactionBranches() if b["current"]],
                 state["doc"].UndoCount),
        "box": (round(box.Length.Value), round(box.Width.Value), round(box.Height.Value)),
        "rows": [(r["seq"], r["name"], r["kind"], len(doc.getTransactionOps(r["seq"])))
                 for r in doc.getTransactionLog()],
        "branches": sorted((b["name"], bool(b["current"]))
                           for b in doc.getTransactionBranches()),
        "versions": sorted((v["kind"], v["name"]) for v in doc.getTransactionVersions()),
        "kept": [(k["object"], k["prop"]) for k in doc.getKeptTransactionValues()],
    }


def change(name, **dims):
    doc = state["served"]
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
    elif name == "restored":
        # In the first document, on the branch the page's was made from
        doc = state["doc"]
        doc.openTransaction("on main")
        doc.getObject("Box").Length = 30
        doc.recompute()
        doc.commitTransaction()
    elif name == "applied":
        # One more row of the page's user, to have something to merge back
        served = state["served"]
        served.pushTransactionActor("invited", CLIENT)
        try:
            change("mine last", Height=21)
        finally:
            served.popTransactionActor()
    elif name == "conflicted":
        # The same property set on the branch the page's was made from, and
        # here: a row of the first that meets what was set here
        doc = state["doc"]
        doc.openTransaction("main height")
        doc.getObject("Box").Height = 50
        doc.recompute()
        doc.commitTransaction()
        change("here height", Height=60)
    elif name == "main moved":
        # Something on the first branch for the page's merge of it to take
        doc = state["doc"]
        doc.openTransaction("main wider")
        doc.getObject("Box").Width = 33
        doc.recompute()
        doc.commitTransaction()
    elif name == "rolled back":
        # A copy the page's user sent, waiting in the log unread (sec 32.30)
        served = state["served"]
        served.keepTransactionSentFile(state["doc"].FileName, "mine.FCStd", CLIENT, "invited")
    elif name == "withdrawn":
        state["sent_after"] = [f["name"] for f in state["served"].getTransactionSentFiles()
                               if not f.get("branch")]
    elif name == "asked":
        doc = state["doc"]
        state["asked"] = [(r["source"], r["target"], r["by"], r["by_kind"], r["state"],
                           r["note"]) for r in doc.getTransactionMergeRequests()]
        state["merge"] = doc.mergeTransactionBranch(BRANCH)
        state["ended"] = [(r["source"], r["target"], r["state"])
                          for r in doc.getTransactionMergeRequests()]


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
        # A branch of the host's that the page's user does not read: listed
        # to it greyed (sec 32.30)
        main = doc.getTransactionCursor()["branch"]
        main = [b["name"] for b in doc.getTransactionBranches() if b["id"] == main][0]
        doc.createTransactionBranch(UNREAD)
        doc.switchTransactionBranch(main)

        port = free_port()
        if not check("the document is served headless",
                     FreeCADGui.serveDocument(doc, port), "port %d" % port):
            finish()
            return
        http_port = serve_viewer()
        note("viewer on %d, scene on %d" % (http_port, port))
        FreeCADGui.serveSetGrants(wsclient.invitation(TOKEN, CLIENT))
        # A branch of it, in a document of its own, for the page -- whose
        # user has made three rows there
        served = FreeCADGui.serveBranch(doc, BRANCH)
        state["served"] = served
        served.pushTransactionActor("invited", CLIENT)
        try:
            for name, prop, value in (("mine taller", "Height", 20), ("mine wider", "Length", 24),
                                      ("mine deeper", "Width", 22)):
                served.openTransaction(name)
                setattr(served.getObject("Box"), prop, value)
                served.recompute()
                served.commitTransaction()
        finally:
            served.popTransactionActor()
        url = ("http://127.0.0.1:%d/fcviewer.html?scene=http://127.0.0.1:%d"
               "&token=%s&client=%s&doc=%s&history"
               % (http_port, port, TOKEN, CLIENT, served.Name))
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
        rows = out.get("rows") or []

        check("the card is up, lists the rows and says the branch the document is on",
              out.get("card") is True and out.get("listed") is True
              and [r["what"] for r in rows][:3] == ["mine deeper", "mine wider", "mine taller"]
              and first == BRANCH and (BRANCH, True) in listed.get("branches", [])
              and listed.get("box") == (24, 22, 20),
              ([r.get("what") for r in rows], out.get("branch"), listed.get("branches")))
        check("the rows from before the serving point are marked, its own are not",
              [r["what"] for r in rows if r.get("before")] == ["deeper", "wider", "taller",
                                                               "base"]
              and not any(r.get("before") for r in rows if r["what"].startswith("mine")),
              [(r.get("what"), r.get("before")) for r in rows])
        check("with no row chosen there are no acts on rows, and no branch to delete",
              out.get("noActs") is True and out.get("noDelete") is True,
              (out.get("noActs"), out.get("noDelete")))
        check("a row from before is offered neither to be taken back nor branched from, "
              "and the card says why",
              (out.get("beforeUndo") or {}).get("disabled") is True
              and "theirs" in (out.get("beforeUndo") or {}).get("title", "")
              and (out.get("beforeBranch") or {}).get("disabled") is True
              and "before" in (out.get("beforeBranch") or {}).get("title", ""),
              (out.get("beforeUndo"), out.get("beforeBranch")))

        undone = s.get("undone", {})
        new = undone.get("rows", [])[len(listed.get("rows", [])):]
        says = out.get("undoSays") or {}
        check("a row of its own is offered to be taken back, by its number",
              out.get("picked") is True
              and says.get("text") == "Undo row %d" % seq.get("mine wider", 0)
              and says.get("disabled") is False, says)
        check("taken back, the card says so and lists the new row first",
              out.get("undoAsked") is True
              and said(out.get("undoSaid"), "Row %d undone" % seq.get("mine wider", 0))
              and out.get("undoListed") is True, (out.get("undoSaid"), out.get("undoListed")))
        check("and the host has what the row set as it was before, by one new row",
              undone.get("box") == (14, 22, 20)
              and [r[1] for r in new if r[2] == "undo"] == ["Undo mine wider"],
              (undone.get("box"), new))

        undone2 = s.get("undone2", {})
        new2 = [r for r in undone2.get("rows", [])[len(undone.get("rows", [])):]
                if r[2] == "undo"]
        two = out.get("twoSays") or {}
        check("two rows chosen, the second with the key held, are offered as two",
              sorted(out.get("twoPicked") or []) == ["mine deeper", "mine taller"]
              and two.get("text") == "Undo 2 rows" and two.get("disabled") is False
              and (out.get("branchHereSays") or {}).get("disabled") is True,
              (out.get("twoPicked"), two, out.get("branchHereSays")))
        check("and are one step on the host",
              out.get("twoAsked") is True and said(out.get("twoSaid"), "2 rows undone")
              and undone2.get("box") == (14, 12, 16) and len(new2) == 1,
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
              and kept[0].get("now") is True and kept[0].get("branch") == BRANCH
              and kept[0].get("before") is False, kept)
        check("a version from before the serving point is marked",
              any(v.get("before") for v in out.get("versions") or []),
              [(v.get("num"), v.get("name"), v.get("before")) for v in out.get("versions") or []])

        branched = s.get("branched", {})
        no_name = out.get("branchNoName") or {}
        check("a branch from here is asked a name, and no name is not one",
              out.get("branchAsked") is True and no_name.get("text") == "Branch"
              and no_name.get("disabled") is True and out.get("branchAnswered") is True,
              no_name)
        on_try = out.get("onTry") or {}
        check("it is the one the card and the host are on, with nothing moved",
              said(out.get("branchSaid"), "try") and on_try.get("value") == "try"
              and {BRANCH, "try"} <= set(on_try.get("options", []))
              and ("try", True) in branched.get("branches", [])
              and branched.get("box") == (14, 12, 16),
              (out.get("branchSaid"), on_try, branched.get("branches")))
        check("a row the host commits there comes into the card", out.get("tryRow") is True)

        switched = s.get("switched", {})
        away = [r for r in out.get("rowsSwitched") or [] if r.get("what") == "on try"]
        check("switching back from the card moves the host",
              out.get("switched") is True and said(out.get("switchSaid"), "On branch " + BRANCH)
              and (out.get("onFirst") or {}).get("value") == BRANCH
              and (BRANCH, True) in switched.get("branches", [])
              and switched.get("box") == (14, 12, 16),
              (out.get("switchSaid"), out.get("onFirst"), switched.get("branches"),
               switched.get("box")))
        check("a branch the page's user does not read is listed greyed, and offered for nothing",
              UNREAD in (out.get("branch") or {}).get("greyed", [])
              and UNREAD not in (out.get("branch") or {}).get("offered", [UNREAD]),
              out.get("branch"))
        check("the branch the document was served from is not offered to be switched to",
              sorted(on_try.get("offered", [])) == sorted([BRANCH, "try"])
              and len(on_try.get("options", [])) > len(on_try.get("offered", [])), on_try)
        applied = s.get("applied", {})
        says = out.get("applySays") or {}
        check("a row of the branch this one was made from, which it reads, comes into the card "
              "and is offered to be applied here, not to be taken back",
              out.get("mainRow") is True and says.get("disabled") is False
              and says.get("text", "").startswith("Apply row ")
              and (out.get("applyUndoSays") or {}).get("disabled") is True,
              (out.get("mainRow"), says, out.get("applyUndoSays")))
        check("applied, the host has what it set in the page's document, and the first is as "
              "it was",
              out.get("applyAsked") is True and said(out.get("applySaid"), "applied as row")
              and applied.get("box") == (30, 12, 16) and applied.get("main", ((),))[0]
              == (30, 12, 16)
              and any(r[2] == "pick" for r in applied.get("rows", [])),
              (out.get("applySaid"), applied.get("box"), applied.get("main")))

        asked = s.get("asked", {})
        check("a merge is asked of two branches, and of none it is not",
              out.get("noRequests") == 0 and (out.get("askNothing") or {}).get("disabled") is True
              and out.get("pickedSource") is True and out.get("pickedTarget") is True
              and (out.get("askSays") or {}).get("disabled") is False,
              (out.get("noRequests"), out.get("askNothing"), out.get("askSays")))
        reqs = out.get("requests") or []
        check("the page asks for its branch to be merged back: the card lists it, and the host "
              "has it under the page's user",
              out.get("askAsked") is True and said(out.get("askSaid"), "merge %s into main" % BRANCH)
              and len(reqs) == 1 and reqs[0].get("what") == "merge %s into main" % BRANCH
              and reqs[0].get("state") == "open" and CLIENT in reqs[0].get("who", "")
              and state.get("asked") == [(BRANCH, "main", CLIENT, "invited", "open",
                                          "the work of the page")]
              and "(1)" in (out.get("requestTab") or ""),
              (out.get("askSaid"), reqs, state.get("asked"), out.get("requestTab")))
        acts = out.get("requestActs") or [None, None, None]
        check("it is the page's to withdraw, and not its to merge or reject",
              [a and a.get("disabled") for a in acts] == [True, True, False], acts)
        merged = s.get("merged", {})
        ends = out.get("requestsEnd") or []
        check("the host merges it in the first document, which then has the page's work",
              state.get("ended") == [(BRANCH, "main", "merged")]
              and merged.get("main", ((),))[0] == (30, 12, 21) and asked.get("box") == (30, 12, 21),
              (state.get("ended"), merged.get("main"), asked.get("box")))
        check("and the card says so by itself, with nothing left to do of it",
              out.get("mergedSeen") is True and len(ends) == 1 and "merged" in ends[0].get("state", "")
              and [a and a.get("disabled") for a in (out.get("requestActsEnd") or [])]
              == [True, True, True], (ends, out.get("requestActsEnd")))

        check("the row of the other branch is listed as away, and not offered to be taken back",
              len(away) == 1 and away[0].get("away") is True
              and (out.get("awaySays") or {}).get("disabled") is True,
              (away, out.get("awaySays")))
        check("the branch made is offered to be deleted, and no other",
              out.get("deletes") == ["try"], out.get("deletes"))

        restored = s.get("restored", {})
        changed = [v for v in out.get("versionsChanged") or [] if v.get("name") == "kept"]
        check("after a change by the host the version kept is no longer the document as it "
              "stands", len(changed) == 1 and changed[0].get("now") is False, changed)
        check("a version chosen has its acts",
              out.get("versionPicked") is True
              and out.get("versionActs") == ["Restore", "Rename...", "Unname",
                                             "Branch from here..."], out.get("versionActs"))
        trim, squash = (list(out.get("hostActs") or []) + [None, None])[:2]
        check("a trim and a squash are the host's: greyed, and the card says so",
              (trim or {}).get("disabled") is True and (squash or {}).get("disabled") is True
              and "host" in (trim or {}).get("title", "")
              and "host" in (squash or {}).get("title", ""), out.get("hostActs"))
        check("restoring it from the card puts the host back, by a row of its own",
              out.get("restoreAsked") is True and said(out.get("restoreSaid"), "Restored to kept")
              and restored.get("box") == (14, 12, 16)
              and any(r[2] == "restore" for r in restored.get("rows", [])),
              (out.get("restoreSaid"), restored.get("box")))

        renamed = s.get("renamed", {})
        check("the branch is renamed from the card, starting from its name",
              out.get("renameAsked") is True and out.get("renameStarts") == BRANCH
              and out.get("renameAnswered") is True
              and (out.get("renamed") or {}).get("value") == "trunk"
              and ("trunk", True) in renamed.get("branches", [])
              and not any(n == BRANCH for n, _ in renamed.get("branches", [])),
              (out.get("renameStarts"), out.get("renamed"), renamed.get("branches")))
        picked = s.get("picked", {})
        pk = out.get("picker") or {}
        check("a row that meets what was set here asks one side for all, or each: the list of "
              "what it would do, waiting for a side for its conflict",
              out.get("conflictApply") is True and "Which side" in (out.get("conflictAsk") or "")
              and out.get("moreAsked") is True
              and any(c.get("key") == "Box.Height" and c.get("conflict")
                      for c in pk.get("changes", []))
              and pk.get("yesDisabled") is True and out.get("pickedTheirs") is True
              and out.get("yesAfter") is False,
              (out.get("conflictAsk"), pk, out.get("yesAfter")))
        check("theirs picked for it, the row is applied with it",
              out.get("pickerYes") is True and said(out.get("pickerSaid"), "applied as row")
              and out.get("pickerGone") is True and picked.get("box", (0, 0, 0))[2] == 50,
              (out.get("pickerSaid"), picked.get("box")))
        ks = out.get("keepSays") or [{}, {}]
        check("an op's value is offered to be kept; every value of its object is the host's",
              out.get("keepOp") is True and ks[0].get("text") == "Keep"
              and ks[0].get("disabled") is False and ks[1].get("text") == "Keep all of it"
              and ks[1].get("disabled") is True and "host keeps" in ks[1].get("title", ""),
              ks)
        check("kept from the card: said, the op says so, the tab counts it, the host keeps it",
              out.get("keepAsked") is True and said(out.get("keepSaid"), "kept: a save writes")
              and out.get("opKept") is True and out.get("keptTab") == "Kept (1)"
              and s.get("kept", {}).get("kept") == [("Box", "Shape")],
              (out.get("keepSaid"), out.get("keptTab"), s.get("kept", {}).get("kept")))
        lines = out.get("keptLines") or [{}]
        ka = out.get("keptActs") or [{}, {}]
        check("listed with its object and property as the page's user's, its text read, "
              "theirs to let go",
              len(lines) == 1 and lines[0].get("kind") == "value"
              and lines[0].get("cells", [])[1:3] == ["Box", "Shape"]
              and "by you" in lines[0].get("cells", [""])[0]
              and out.get("keptValueShown") is True and ka[0].get("disabled") is False
              and (ka[1].get("text") or "").startswith("Show row"),
              (lines, ka))
        check("let go from the list: said, the tab and the host have nothing kept",
              out.get("releaseAsked") is True and said(out.get("releaseSaid"), "let go")
              and out.get("keptTabEnd") == "Kept" and out.get("keptEmpty") is True
              and s.get("released", {}).get("kept") == [],
              (out.get("releaseSaid"), out.get("keptTabEnd"), s.get("released", {}).get("kept")))
        merged, undid, rolled = (s.get("page merged", {}), s.get("page undid", {}),
                                 s.get("rolled back", {}))
        kinds = lambda sample: [r[2] for r in sample.get("rows", [])]
        check("the page's user asks for the first branch and merges it, as a merge row",
              out.get("rbAsked") is True and out.get("rbRequest") is True
              and out.get("rbMergeAsked") is True and said(out.get("rbMergeSaid"), "Merged")
              and merged.get("box", (0, 0, 0))[1] == 33
              and kinds(merged).count("merge") == kinds(s.get("released", {})).count("merge") + 1,
              (out.get("rbMergeSaid"), merged.get("box")))
        check("undone on its own socket: the box is as it was",
              out.get("rbUndoSent") is True and undid.get("box", (0, 0, 0))[1] != 33
              and kinds(undid)[-1:] == ["undo"], (undid.get("box"), kinds(undid)[-3:]))
        rb = out.get("rbSays") or {}
        check("Roll back merge... is offered, asks, and the merge leaves the log; the box stays",
              out.get("rbOffered") is True and rb.get("disabled") is False
              and out.get("rbClicked") is True and out.get("rbAnswered") is True
              and said(out.get("rbSaid"), "rolled back")
              and kinds(rolled).count("merge") == kinds(s.get("released", {})).count("merge")
              and "undo" not in kinds(rolled)[-1:]
              and rolled.get("box") == undid.get("box"),
              (rb, out.get("rbSaid"), rolled.get("box"), kinds(rolled)[-3:]))
        sent_rows = out.get("sentRows") or []
        bring, drop = (list(out.get("sentActs") or []) + [None, None])[:2]
        check("a copy the page's user sent is listed to it, unread; not its to bring in, its "
              "to withdraw, which it does",
              out.get("sentListed") is True and len(sent_rows) == 1
              and sent_rows[0][0].startswith("mine.FCStd") and "not read" in sent_rows[0][0]
              and (bring or {}).get("disabled") is True
              and "host" in (bring or {}).get("title", "")
              and (drop or {}).get("text") == "Withdraw" and (drop or {}).get("disabled") is False
              and out.get("withdrawAnswered") is True
              and said(out.get("withdrawSaid"), "withdrawn") and state.get("sent_after") == [],
              (sent_rows, out.get("sentActs"), out.get("withdrawSaid"), state.get("sent_after")))
        before_main = s.get("restored", {})
        check("nothing the page did moved the document the branch was served from",
              before_main.get("main") == listed.get("main")
              and listed.get("main", ((), [], 0))[0] == (14, 12, 16)
              and len(listed.get("main", ((), [], 0))[1]) == 1
              and listed["main"][1][0] != BRANCH
              and renamed.get("main", ((), [], 0))[1] == listed["main"][1],
              (listed.get("main"), before_main.get("main"), renamed.get("main")))
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
