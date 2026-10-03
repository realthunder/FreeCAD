"""A real browser draws a dimension's value editor and works it.

docs/SketcherPort.md "One editor for a constraint's value", the browser
half. tests/gui/serve-datum-in-place.py checks the server side against a
synthetic socket client; this checks what web/src/onview.tsx draws from
the "datum" entry of the onview push, through scripts/datum-drive.js:

  - a dimension made from the browser (the command op, a click on the
    line) opens the editor in the page: its value, its name row, its
    driving toggle, and the field has the keys;
  - the editor is kept off the line it measures;
  - typing '=' and the start of a name offers names from the client's own
    completion (pathcomplete.ts, shared with the omni box), and taking one
    goes up and comes back as the server's text;
  - the toggle is a click, and the server's word comes back;
  - Escape takes the editor away.

Needs what serve-onview-browser.py needs: build/wasm with its web bundle,
node, PUPPETEER_PATH and CHROME. Skips rather than fails without them.

  PUPPETEER_PATH=/path/to/node_modules/puppeteer-core \
  CHROME=~/.cache/puppeteer/chrome/*/chrome-linux64/chrome \
  scripts/gui-test.sh tests/gui/serve-datum-browser.py /tmp/datum-web \
      --timeout 600
"""
import functools
import http.server
import json as jsonlib
import os
import subprocess
import threading
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

from wsclient import free_port

clock = time.perf_counter

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
REPO = os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__))))
WASM = os.path.join(REPO, "build", "wasm")
DRIVER = os.path.join(REPO, "scripts", "datum-drive.js")
DOC = "ServeDatumBrowser"
OBJ = "Sketch"
SETTLE_MS = 8000
RUN_WAIT_S = 300

state = {"doc": None, "done": False, "run": None, "t0": clock()}


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
            env["DATUM_PHASES"] = self.phase_path
            env["DATUM_RESULT"] = self.result_path
            if os.environ.get("ONVIEW_REAL"):
                env["DISPLAY"] = os.environ.get("ONVIEW_DISPLAY", ":0")
                env.pop("WAYLAND_DISPLAY", None)
            with open(self.log_path, "w") as log:
                proc = subprocess.Popen(
                    ["node", DRIVER, self.url, OBJ, str(SETTLE_MS)],
                    stdout=log, stderr=subprocess.STDOUT, env=env)
                proc.wait()
            if proc.returncode != 0:
                self.error = "node exited %d:\n%s" % (proc.returncode,
                                                      self.tail())
        except Exception:
            self.error = traceback.format_exc()

    def tail(self, lines=25):
        try:
            with open(self.log_path) as f:
                return "".join(f.readlines()[-lines:])
        except OSError:
            return "(no driver log)"

    def result(self):
        try:
            with open(self.result_path) as f:
                return jsonlib.load(f)
        except Exception:
            return {}


def serve_viewer():
    handler = functools.partial(http.server.SimpleHTTPRequestHandler,
                                directory=WASM)
    handler.log_message = lambda *a, **k: None
    port = free_port()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    state["httpd"] = server
    return port


def views_3d():
    return len(FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor"))


def build():
    try:
        import Part

        if not os.path.exists(os.path.join(WASM, "fcviewer.html")):
            skip("no built viewer at %s (ninja -C build/wasm)" % WASM)
            return
        if not os.path.exists(os.path.join(WASM, "web", "inspector.js")):
            skip("no DOM bundle at %s/web (the boxes are drawn by it)" % WASM)
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

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        # The first dimension of a freehand sketch scales it; this is about
        # the editor
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/dimensioning").SetInt(
                "AutoScaleMode", 1)
        doc = FreeCAD.newDocument(DOC, hidden=True)
        state["doc"] = doc
        sketch = doc.addObject("Sketcher::SketchObject", OBJ)
        # Its middle is what the viewer's camera centres on, and is not the
        # origin: a click there would pick the sketch's root point
        sketch.addGeometry(Part.LineSegment(FreeCAD.Vector(-20, 5, 0),
                                            FreeCAD.Vector(20, 5, 0)), False)
        doc.recompute()

        port = free_port()
        if not check("the document is served headless",
                     FreeCADGui.serveDocument(doc, port), "port %d" % port):
            finish()
            return
        http_port = serve_viewer()
        note("viewer on %d, scene on %d" % (http_port, port))
        url = ("http://127.0.0.1:%d/fcviewer.html?scene=http://127.0.0.1:%d"
               % (http_port, port))
        state["run"] = Run(url)
        state["t0"] = clock()
        state["run"].start()
        QtCore.QTimer.singleShot(50, poll)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def poll():
    run = state["run"]
    state.setdefault("views", []).append(views_3d())
    if run.is_alive():
        if clock() - state["t0"] > RUN_WAIT_S:
            check("the browser finished", False,
                  "still running after %ds\n%s" % (RUN_WAIT_S, run.tail()))
            finish()
            return
        QtCore.QTimer.singleShot(100, poll)
        return
    QtCore.QTimer.singleShot(700, verify)


def verify():
    run = state["run"]
    out = run.result()
    try:
        if not check("the browser ran without error", run.error is None,
                     run.error or ""):
            note(run.tail())
        check("the driver reported no error", not out.get("error"),
              out.get("error", ""))
        check("the browser entered the edit session", out.get("entered") is True, out)
        check("the command op left the page", out.get("commandSent") is True, out)
        opened = out.get("opened") or {}
        check("the dimension's editor is drawn in the page", out.get("drawn") is True
              and bool(opened), opened)
        check("with the line's length, a name row, a driving toggle, and the keys",
              str(opened.get("value", "")).startswith("40") and opened.get("name") is True
              and opened.get("toggle") is True and opened.get("focused") is True, opened)
        centre = out.get("centre") or {}
        if opened and centre:
            covers = (opened["left"] <= centre["x"] <= opened["right"]
                      and opened["top"] <= centre["y"] <= opened["bottom"])
            check("it does not cover the line it measures", not covers, (opened, centre))
        typed = out.get("typed") or {}
        check("'=' and a name's start offer names from the client's completion",
              out.get("listed") is True and "Sketch" in (typed.get("rows") or []), typed)
        completed = out.get("completed") or {}
        check("taking one comes back as the server's text",
              completed.get("value") == "=Sketch", completed)
        toggled = out.get("toggled") or {}
        check("the toggle is a click, and the server's word comes back",
              toggled.get("toggle") is False, toggled)
        check("Escape takes the editor away", out.get("atEnd") is None, out.get("atEnd"))
        sketch = state["doc"].getObject(OBJ)
        check("and leaves no constraint behind", sketch.ConstraintCount == 0,
              sketch.ConstraintCount)
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        state["httpd"].shutdown()
    except Exception:
        pass
    try:
        FreeCADGui.getDocument(DOC).resetEdit()
    except Exception:
        pass
    FreeCAD.ParamGet(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/dimensioning").RemInt("AutoScaleMode")
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, build)
