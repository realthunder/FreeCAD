"""A real browser draws a sketch tool's on-view parameters and types in one.

The browser half of docs/ThinClient.md sec 8.7 (stage 5).
tests/gui/serve-onview-params.py already checks the server side against a
synthetic socket client: the allowlisted `command` op, the pushed set, a
key frame typed into the box that has the keys. What no synthetic client
can check is the two halves that live in the browser -- main.cpp
projecting each world anchor into canvas pixels with the camera of the
frame it is drawing, and web/src/onview.tsx drawing a box there and
forwarding a keystroke back rather than editing itself.

The reading that only a browser can give is the PROJECTION. Orbit the
camera and the boxes must travel with the geometry, while the server is
never told the camera moved -- under the default uplink policy it is not
told between clicks at all (sec 8.10b). A box positioned by the server
would sit still, and no test that runs on one side alone can see that.

What is asserted:

  - the browser enters the session and starts a tool through the control
    channel. That op had to exist for any of this to be reachable: the
    sketcher's own shortcuts are Qt shortcuts on a main window, so a
    browser had no way to start a tool at all;
  - the tool's boxes reach the page ('fc:onview') and are DRAWN -- the
    assertion is on the DOM elements, not on the event that feeds them,
    so the whole chain is in it;
  - each box is placed where its anchor projects, which is checked by
    orbiting: the boxes move, and the count of pushes from the server
    does not change while they do;
  - typing into a drawn box changes what it shows -- and the change
    comes from the server, because the box is a display: it prevents the
    default on every key, so a field editing itself would show nothing;
  - and Escape takes the boxes away with the tool, since a box left on
    screen would be forwarding keystrokes to a tool that has finished.

Needs the built viewer (build/wasm/fcviewer.html; `ninja -C build/wasm`)
WITH the DOM bundle beside it (web/inspector.js -- the boxes are drawn by
the Solid layer, not by the canvas), node, a puppeteer-core install named
by PUPPETEER_PATH, and a Chrome named by CHROME. Skips rather than fails
when any is missing.

  PUPPETEER_PATH=/path/to/node_modules/puppeteer-core \
  CHROME=~/.cache/puppeteer/chrome/*/chrome-linux64/chrome \
  scripts/gui-test.sh tests/gui/serve-onview-browser.py /tmp/onview-web \
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
DRIVER = os.path.join(REPO, "scripts", "onview-drive.js")
DOC = "ServeOnViewBrowser"
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
            env["ONVIEW_PHASES"] = self.phase_path
            env["ONVIEW_RESULT"] = self.result_path
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
        # The default shows only the dimensional boxes, and a line tool's
        # first state has only positioning ones -- nothing would be drawn
        # until the second click. This is about the mechanism, so ask for
        # all of them.
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").SetInt(
                "OnViewParameterVisibility", 2)
        doc = FreeCAD.newDocument(DOC, hidden=True)
        state["doc"] = doc
        sketch = doc.addObject("Sketcher::SketchObject", OBJ)
        sketch.addGeometry(Part.LineSegment(FreeCAD.Vector(-5, 0, 0),
                                            FreeCAD.Vector(5, 0, 0)), False)
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


def moved(before, after):
    """How far the boxes travelled, in CSS pixels, pairwise."""
    if not before or len(before) != len(after):
        return []
    return [round(max(abs(a["x"] - b["x"]), abs(a["y"] - b["y"])))
            for b, a in zip(before, after)]


def verify():
    run = state["run"]
    out = run.result()
    try:
        if not check("the browser ran without error", run.error is None,
                     run.error or ""):
            note(run.tail())
        check("the driver reported no error", not out.get("error"),
              out.get("error", ""))
        check("the browser entered the edit session", out.get("entered") is True,
              out)
        check("the tool op left the page", out.get("toolSent") is True, out)
        check("the session never made a 3D view",
              all(v == 0 for v in state.get("views", [])),
              sorted(set(state.get("views", []))))

        check("the page was told about the boxes", (out.get("pushes") or 0) > 0,
              out.get("pushes"))
        boxes = out.get("boxes") or []
        check("and drew them", len(boxes) > 0, boxes)
        check("the viewer placed them, every frame they moved",
              (out.get("layouts") or 0) > 0, out.get("layouts"))
        if boxes:
            check("a drawn box shows text", bool(boxes[0].get("text")), boxes[0])

        typed = out.get("typed") or {}
        after = out.get("boxesAfterTyping") or []
        if boxes and after:
            changed = [i for i, (b, a) in enumerate(zip(boxes, after))
                       if b.get("text") != a.get("text")]
            check("typing into a box changed what it shows",
                  typed.get("ok") is True and bool(changed),
                  (typed, boxes, after))
            check("and changed exactly one of them", len(changed) <= 1, changed)

        orbit = out.get("orbit") or {}
        travel = moved(orbit.get("before"), orbit.get("after"))
        check("orbiting moves the boxes with the geometry",
              bool(travel) and max(travel) >= 4, orbit)
        # The discriminating half: the server was not told, so a position
        # computed there could not have produced that movement.
        check("without the server restating them",
              orbit.get("pushesAfter") == orbit.get("pushesBefore"),
              (orbit.get("pushesBefore"), orbit.get("pushesAfter")))

        check("leaving takes the boxes away",
              not (out.get("boxesAtEnd") or []), out.get("boxesAtEnd"))
        check("and the session with them", not out.get("editingAtEnd"),
              out.get("editingAtEnd"))
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
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, build)
