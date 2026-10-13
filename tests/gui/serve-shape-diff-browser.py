"""A real browser asks for an object's last change, sees it and works it.

The browser half of docs/TransactionLog.md sec 32.19.
tests/gui/serve-shape-diff-edit.py checks the server side against a
synthetic socket client: the `edit` op with the mode, the two states a node
of the session, the panel mirrored, a write to its slider. What no
synthetic client can check is the page: that the session's overlay is
DRAWN by the viewer, that the task's panel comes up by itself and has a
slider in it (web/src/main.tsx, widgets/panel.tsx), that the slider moved
in the page is the host's slider moved, and that the left button stays the
camera's in an edit that has nothing in the view to click
(wasm/main.cpp, the push's `"pointer":false`).

So this serves a document with a box and a hole made taller, serves the
built viewer over HTTP, and lets Chrome do it through
scripts/shapediff-drive.js. Asserted:

  - the page's ask enters the edit: the document says so, and it is the
    mode of the last change;
  - nothing of the colour a changed face is drawn in is on the canvas
    before, a great deal while the edit runs, and nothing after;
  - the panel is up in the page without being asked for, with its slider
    at the later state and what was found in words; none before, none
    after;
  - a left drag across the view sends no input to the edit -- counted on
    this connection by the server -- and the edit goes on;
  - the slider moved in the page puts the two states at the earlier one,
    read on the host;
  - leaving from the page ends the edit on the host, with nothing of the
    two states left;
  - the document has no 3D view throughout, and none of it wrote a row of
    the log or an undo step.

Needs the built viewer (build/wasm/fcviewer.html; `ninja -C build/wasm`),
node, a puppeteer-core install named by PUPPETEER_PATH, and a Chrome named
by CHROME. Skips rather than fails when any is missing.

  PUPPETEER_PATH=/path/to/node_modules/puppeteer-core \\
  CHROME=~/.cache/puppeteer/chrome/*/chrome-linux64/chrome \\
  scripts/gui-test.sh tests/gui/serve-shape-diff-browser.py /tmp/sd-web \\
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
DRIVER = os.path.join(REPO, "scripts", "shapediff-drive.js")
DOC = "ServeShapeDiffBrowser"
OBJ = "Cut"
MODE = 6  # Gui::ViewProvider::ValueDiff
SETTLE_MS = 9000
RUN_WAIT_S = 300
# The browser acts on the document, so it is someone: the holder of an
# invitation issued to its name (docs/TransactionLog.md sec 30.6 U4, U6).
TOKEN = "shape-diff-browser-invite"
CLIENT = "shape-diff-browser"

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
            env["SHAPEDIFF_PHASES"] = self.phase_path
            env["SHAPEDIFF_RESULT"] = self.result_path
            env.setdefault("SHAPEDIFF_SHOTS", OUT)
            with open(self.log_path, "w") as log:
                proc = subprocess.Popen(
                    ["node", DRIVER, self.url, OBJ, str(SETTLE_MS)],
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


def named(node, name):
    from pivy import coin

    if node is None:
        return -1
    search = coin.SoSearchAction()
    search.setName(name)
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(node)
    return search.getPaths().getLength()


def viewer_client():
    """This connection's counters, as the SERVER counted them."""
    rows = [c for c in FreeCADGui.serveClients() if c.get("viewer")]
    return rows[-1] if rows else None


def sample():
    from pivy import coin

    doc = state["doc"]
    gdoc = FreeCADGui.getDocument(DOC)
    cut = doc.getObject(OBJ)
    holder = coin.SoNode.getByName("EditingSessionNode")
    mode = None
    try:
        mode = gdoc.getInEdit() is not None and gdoc.getEditMode()
    except Exception:
        mode = gdoc.getInEdit() is not None
    client = viewer_client()
    return {
        "edit": gdoc.getInEdit() is not None,
        "mode": mode,
        "in_session": named(holder, "ShapeDiff") if holder is not None else 0,
        "in_object": named(cut.ViewObject.RootNode, "ShapeDiff"),
        "found": dict(cut.ViewObject.getValueDiff()),
        "views": len(gdoc.mdiViewsOfType("Gui::View3DInventor")),
        "input": client["inputMsgs"] if client else None,
        "written": (len(doc.getTransactionLog()), doc.UndoCount, doc.RedoCount),
    }


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
        doc.saveAs(os.path.join(tempfile.mkdtemp(prefix="fc-sd-browser-"), "sd.FCStd"))
        doc.openTransaction("taller")
        box.Height = 16
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
               "&token=%s&client=%s" % (http_port, port, TOKEN, CLIENT))
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


def verify():
    try:
        run = state["run"]
        out = run.result()
        s = state["samples"]
        if run.error or out.get("error"):
            check("the browser run", False, run.error or out.get("error"))
            note("---- driver log")
            note(run.tail(25))
        settled, entered = s.get("settled", {}), s.get("entered", {})
        dragged, slid, left = s.get("dragged", {}), s.get("slid", {}), s.get("left", {})

        check("the page's ask enters the edit, and the document says so",
              out.get("asked") is True and out.get("entered") is True
              and entered.get("edit") is True, (out.get("asked"), out.get("entered"), entered))
        check("the two states are a node of the session, not under the object's root",
              entered.get("in_session") == 1 and entered.get("in_object") == 0,
              (entered.get("in_session"), entered.get("in_object")))
        check("nothing of a changed face's colour is drawn before, a great deal in the edit",
              out.get("before", 1e9) < 500 and out.get("inEdit", 0) > 20000,
              "%s before, %s in the edit" % (out.get("before"), out.get("inEdit")))
        text = out.get("panelText") or ""
        check("the panel is up in the page by itself, with its slider at the later state",
              out.get("slidersBefore") == 0 and out.get("panel") is True
              and out.get("sliderValue") == 100,
              (out.get("slidersBefore"), out.get("panel"), out.get("sliderValue")))
        check("and says which row and what became of the faces",
              "taller" in text and "1 as they were, 6 changed" in text, text.replace("\n", " / "))
        check("a left drag sends no input to the edit, which goes on",
              dragged.get("input") is not None and dragged.get("input") == entered.get("input")
              and out.get("editingAfterDrag") == OBJ and dragged.get("edit") is True,
              "input frames %s, were %s; editing %s"
              % (dragged.get("input"), entered.get("input"), out.get("editingAfterDrag")))
        check("the slider moved in the page puts the two states at the earlier one",
              out.get("moved") is True and slid.get("found", {}).get("position") == 0,
              (out.get("moved"), slid.get("found", {}).get("position")))
        check("leaving from the page ends the edit on the host, nothing of the two states left",
              out.get("left") is True and left.get("edit") is False
              and left.get("in_session") == 0 and left.get("in_object") == 0
              and left.get("found") == {},
              (out.get("left"), left.get("edit"), left.get("in_session"), left.get("in_object")))
        check("and in the page: no panel, and nothing of the colour drawn",
              out.get("slidersAfter") == 0 and out.get("after", 1e9) < 500,
              "%s sliders, %s pixels" % (out.get("slidersAfter"), out.get("after")))
        check("the document has no 3D view throughout",
              all(x.get("views") == 0 for x in (settled, entered, slid, left) if x),
              [x.get("views") for x in (settled, entered, slid, left)])
        # Counted from when the browser is in: its coming is a row (login)
        check("none of it wrote a row of the log or an undo step",
              settled.get("written") is not None and left.get("written") == settled.get("written"),
              "%s, were %s" % (left.get("written"), settled.get("written")))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
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
