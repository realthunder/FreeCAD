"""A real browser reads the transaction log in its own panel, and is shown a
row's change.

The browser half of docs/TransactionLog.md sec 32.21. tests/gui/serve-log.py
checks the ops against synthetic socket clients: the rows, a page at a
time, the ops of one, a value, the push, the refusal of a client that may
only look, the `edit` op with a row named. What no synthetic client can
check is the page (web/src/logpanel.tsx): that its card lists what the host
answered, that the host's cue brings a new row into it, that a row chosen
shows its ops and one of those its value, and that "Show" ends in the edit
of that row's change -- the host's panel of it up in the page, with the
host's own Close (widgets/panel.tsx), and the two states drawn lit.

So this serves a document with a box and a hole made taller and then wider,
serves the built viewer over HTTP with the card open on load (`?history`),
and lets Chrome do it through scripts/logpanel-drive.js. Asserted:

  - the card lists the rows of the host's log that changed something,
    newest first, with who wrote them, and names the branch;
  - a row the host commits meanwhile comes into the card by itself;
  - the rows that changed nothing are listed when asked for;
  - a row chosen lists its ops: the property set, and the shape that
    followed with a "Show";
  - an op chosen shows its value after, and before when asked;
  - "Show" enters the edit on the host, of that row and not of the last;
    the page has the host's panel of it, which says the row and has Close
    and no OK; the two states are drawn;
  - Close in the page ends the edit on the host, the card stays;
  - none of it but the host's own commit wrote a row of the log.

Needs the built viewer (build/wasm/fcviewer.html; `ninja -C build/wasm`),
node, a puppeteer-core install named by PUPPETEER_PATH, and a Chrome named
by CHROME. Skips rather than fails when any is missing.

  PUPPETEER_PATH=/path/to/node_modules/puppeteer-core \\
  CHROME=~/.cache/puppeteer/chrome/*/chrome-linux64/chrome \\
  scripts/gui-test.sh tests/gui/serve-log-browser.py /tmp/log-web \\
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
from PySide import QtCore, QtWidgets

import wsclient
from wsclient import free_port

clock = time.perf_counter

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
REPO = os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__))))
WASM = os.path.join(REPO, "build", "wasm")
DRIVER = os.path.join(REPO, "scripts", "logpanel-drive.js")
DOC = "ServeLogBrowser"
OBJ = "Cut"
MODE = 6  # Gui::ViewProvider::ValueDiff
SETTLE_MS = 9000
RUN_WAIT_S = 300
# The browser acts on the document, so it is someone: the holder of an
# invitation issued to its name (docs/TransactionLog.md sec 30.6 U4, U6).
TOKEN = "log-browser-invite"
CLIENT = "log-browser"

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
            env["LOGPANEL_PHASES"] = self.phase_path
            env["LOGPANEL_RESULT"] = self.result_path
            env.setdefault("LOGPANEL_SHOTS", OUT)
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


def host_rows():
    """The rows as the host has them: (seq, name, ops), oldest first."""
    doc = state["doc"]
    return [(r["seq"], r["name"], len(doc.getTransactionOps(r["seq"])))
            for r in doc.getTransactionLog()]


def sample():
    gdoc = FreeCADGui.getDocument(DOC)
    what = None
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "PartGui::ShapeDiffPanel":
            label = w.findChild(QtWidgets.QLabel, "diffWhat")
            what = label.text() if label else None
    return {"edit": gdoc.getInEdit() is not None, "what": what, "rows": host_rows()}


def at_phase(name):
    """What the host does when the page has got so far."""
    if name == "listed":
        doc = state["doc"]
        doc.openTransaction("deeper")
        doc.getObject("Box").Width = 12
        doc.recompute()
        doc.commitTransaction()


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
        doc.saveAs(os.path.join(tempfile.mkdtemp(prefix="fc-log-browser-"), "log.FCStd"))
        doc.openTransaction("taller")
        box.Height = 16
        doc.recompute()
        doc.commitTransaction()
        doc.openTransaction("wider")
        box.Length = 14
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


def first_line(text):
    return (text or "").split("\n")[0].strip()


def verify():
    try:
        run = state["run"]
        out = run.result()
        s = state["samples"]
        if run.error or out.get("failure"):
            check("the browser run", False, run.error or out.get("failure"))
            note("---- driver log")
            note(run.tail(25))
        listed, pushed = s.get("listed", {}), s.get("pushed", {})
        shown, left = s.get("shown", {}), s.get("left", {})

        changed = [r for r in listed.get("rows", []) if r[2] > 0][::-1]
        rows = out.get("rows") or []
        check("the card is up on load and lists the rows that changed something, newest first",
              out.get("card") is True and out.get("error") is None
              and [(r["seq"], first_line(r["what"])) for r in rows]
              == [(r[0], r[1]) for r in changed] and len(rows) >= 3,
              ([(r.get("seq"), first_line(r.get("what"))) for r in rows], changed,
               out.get("error")))
        check("with who wrote them, and the branch the document is on",
              all(r.get("who") and r.get("when") for r in rows) and out.get("branch"),
              ([r.get("who") for r in rows], out.get("branch")))

        after = [r for r in pushed.get("rows", []) if r[2] > 0][::-1]
        now = out.get("rowsPushed") or []
        check("a row the host commits meanwhile comes into the card by itself",
              out.get("pushed") is True
              and [(r["seq"], first_line(r["what"])) for r in now]
              == [(r[0], r[1]) for r in after] and first_line(now[0]["what"]) == "deeper",
              [(r.get("seq"), first_line(r.get("what"))) for r in now][:4])
        records = out.get("rowsWithRecords") or []
        check("the rows that changed nothing are listed when asked for",
              [r["seq"] for r in records] == [r[0] for r in pushed.get("rows", [])][::-1]
              and len(records) > len(now) and any(r.get("record") for r in records),
              (len(records), len(now), len(pushed.get("rows", []))))

        ops = out.get("ops") or []
        texts = {o["text"]: o for o in ops}
        check("a row chosen lists its ops: the property set, and the shape with a Show",
              out.get("chose") is True and "Box: Height" in texts
              and texts["Box: Height"]["show"] is False and "Cut: Shape" in texts
              and texts["Cut: Shape"]["show"] is True, [(o["text"], o["show"]) for o in ops])
        check("an op chosen shows its value after, and before when asked",
              out.get("choseOp") is True and "16" in (out.get("valueAfter") or "")
              and "10" in (out.get("valueBefore") or "")
              and out.get("valueAfter") != out.get("valueBefore")
              and ["After", True] in (out.get("sides") or []),
              (out.get("valueAfter"), out.get("valueBefore"), out.get("sides")))

        taller = [r[0] for r in listed.get("rows", []) if r[1] == "taller"]
        check("Show enters the edit on the host, of that row and not of the last",
              out.get("asked") is True and out.get("entered") is True
              and shown.get("edit") is True and taller
              and str(shown.get("what", "")).startswith("taller (row %d)" % taller[0]),
              (out.get("asked"), out.get("entered"), shown.get("what")))
        check("the page has the host's panel of it, which says the row",
              out.get("panelBefore") == 0 and out.get("panel") is True
              and "taller (row" in (out.get("panelText") or ""),
              (out.get("panelBefore"), out.get("panel"), out.get("panelText")))
        check("with the host's own Close, and no OK",
              out.get("buttons") == ["Close"], out.get("buttons"))
        check("the two states are drawn",
              out.get("magentaBefore", 1 << 30) < 500 and out.get("magentaIn", 0) > 20000,
              (out.get("magentaBefore"), out.get("magentaIn")))
        check("Close in the page ends the edit on the host, and the card stays",
              out.get("closed") is True and out.get("left") is True
              and left.get("edit") is False and left.get("what") is None
              and out.get("panelAfter") == 0 and out.get("cardAfter") is True,
              (out.get("closed"), out.get("left"), left.get("edit"), out.get("panelAfter")))
        check("none of it but the host's own commit wrote a row of the log",
              left.get("rows") == pushed.get("rows")
              and len(pushed.get("rows", [])) > len(listed.get("rows", [])),
              (len(listed.get("rows", [])), len(pushed.get("rows", [])),
               len(left.get("rows", []))))
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
