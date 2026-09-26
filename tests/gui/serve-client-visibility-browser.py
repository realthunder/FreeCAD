"""One browser client hides and shows objects for itself; the other does not.

docs/CoinRetirement.md 5.18, per client, the drawing half. A client sets
its own ObjectVisibilities map with the `view.visibility` op; the host
parses it onto the client's mirror (tests/gui/serve-client-visibility.py
checks the host's picks against synthetic clients) and tells the client
the parsed table back. The viewer draws by it with the renderer's own
rule, against the object chains SceneDump v80 ships -- and a hidden
object one client shows travels to every client, flagged, admitted only
by the table that shows it. No synthetic client can see any of that: it
is what a canvas shows.

The reading is pixels by colour: Box red, Box2 green, a hidden box yellow.
What is asserted, client A setting a table and client B setting none:

  - both draw the red and the green box and neither the yellow one;
  - A's bare hide of Box2 takes the green out of A's canvas and not B's;
  - A's bare show of the hidden box draws it yellow in A and not in B,
    and B does not re-frame for it;
  - a click where Box2 was drawn selects no Box2 in A;
  - A's cleared map gives A its green back and takes the yellow away.

Needs the built viewer (build/wasm/fcviewer.html; `cmake --build
build/wasm`), node, a puppeteer-core install named by PUPPETEER_PATH, and
a Chrome named by CHROME. Skips rather than fails when any is missing.

  PUPPETEER_PATH=/path/to/node_modules/puppeteer-core \
  CHROME=~/.cache/puppeteer/chrome/*/chrome-linux64/chrome \
  scripts/gui-test.sh tests/gui/serve-client-visibility-browser.py \
      /tmp/clientvis --timeout 600
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
DRIVER = os.path.join(REPO, "scripts", "clientvis-drive.js")
DOC = "ServeClientVisBrowser"
SETTLE_MS = 9000
RUN_WAIT_S = 300

state = {"doc": None, "done": False, "run": None, "t0": clock()}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ")
         + name + (" | " + str(detail) if detail else ""))
    return cond


def skip(why):
    note("SKIP " + why)
    finish()


class Run(threading.Thread):
    """The two browsers, driven from off the GUI thread. Log and phase
    markers go to files: node buffers stdout to a pipe."""

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
            env["CLIENTVIS_PHASES"] = self.phase_path
            env["CLIENTVIS_RESULT"] = self.result_path
            with open(self.log_path, "w") as log:
                proc = subprocess.Popen(
                    ["node", DRIVER, self.url, str(SETTLE_MS)],
                    stdout=log, stderr=subprocess.STDOUT, env=env)
                proc.wait()
            if proc.returncode != 0:
                self.error = "node exited %d:\n%s" % (proc.returncode,
                                                      self.tail())
        except Exception:
            self.error = traceback.format_exc()

    def phases(self):
        try:
            with open(self.phase_path) as f:
                return f.read().split()
        except OSError:
            return []

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


def serve_viewer():
    handler = functools.partial(http.server.SimpleHTTPRequestHandler,
                                directory=WASM)
    handler.log_message = lambda *a, **k: None
    port = free_port()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    state["httpd"] = server
    return port


def build():
    try:
        if not os.path.exists(os.path.join(WASM, "fcviewer.html")):
            skip("no built viewer at %s (cmake --build build/wasm)" % WASM)
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
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/View/Render").SetString(
                "Type", "bgfx - OpenGL")
        doc = FreeCAD.newDocument(DOC, hidden=True)
        state["doc"] = doc
        boxes = (("Box", 0, 0, (1.0, 0.0, 0.0)),
                 ("Box2", 16, 0, (0.0, 1.0, 0.0)),
                 ("Hid", 8, 16, (1.0, 1.0, 0.0)))
        for name, x, y, colour in boxes:
            box = doc.addObject("Part::Box", name)
            box.Length = box.Width = box.Height = 10
            box.Placement.Base = FreeCAD.Vector(x, y, 0)
        doc.recompute()
        for name, x, y, colour in boxes:
            vo = doc.getObject(name).ViewObject
            vo.ShapeColor = colour
            vo.LineColor = colour
            vo.PointColor = colour
        doc.getObject("Hid").ViewObject.Visibility = False

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
    if run.is_alive():
        if clock() - state["t0"] > RUN_WAIT_S:
            check("the browsers finished", False,
                  "still running after %ds\n%s" % (RUN_WAIT_S, run.tail()))
            finish()
            return
        QtCore.QTimer.singleShot(100, poll)
        return
    QtCore.QTimer.singleShot(700, verify)


def count(shot, colour):
    return ((shot or {}).get(colour) or {}).get("n", 0)


def verify():
    run = state["run"]
    out = run.result()
    try:
        if not check("the browsers ran without error", run.error is None,
                     run.error or ""):
            note(run.tail())
        check("the driver reported no error", not out.get("error"),
              out.get("error", ""))
        for key in ("a0", "b0", "a1", "b1", "a2", "b2", "a3", "b3"):
            note("%s: %s" % (key, jsonlib.dumps(out.get(key))))
        for key in ("hideReply", "showReply", "clearReply", "selAfterClick"):
            note("%s: %s" % (key, out.get(key)))

        a0, b0 = out.get("a0"), out.get("b0")
        green, red = count(a0, "green"), count(a0, "red")
        check("both draw the red and the green box",
              min(count(a0, "red"), count(a0, "green"),
                  count(b0, "red"), count(b0, "green")) > 200,
              (a0, b0))
        # A few hundred edge pixels blend red and green into "yellow";
        # the hidden box is tens of thousands.
        noise = max(count(a0, "yellow"), count(b0, "yellow"))
        drawn = max(2000, 10 * noise)
        check("and neither the hidden yellow one", noise < 1000,
              (count(a0, "yellow"), count(b0, "yellow")))

        ok = lambda k: '"ok":true' in (out.get(k) or "")
        check("the host takes A's hide", ok("hideReply"), out.get("hideReply"))
        check("A's hide takes the green out of A's canvas",
              count(out.get("a1"), "green") < 0.05 * green,
              (count(out.get("a1"), "green"), green))
        check("and leaves A's red", count(out.get("a1"), "red") > 0.5 * red,
              (count(out.get("a1"), "red"), red))
        check("B still draws the green",
              count(out.get("b1"), "green") > 0.5 * count(b0, "green"),
              (count(out.get("b1"), "green"), count(b0, "green")))

        check("the host takes A's show", ok("showReply"), out.get("showReply"))
        check("A draws the hidden box it shows",
              count(out.get("a2"), "yellow") > drawn,
              (count(out.get("a2"), "yellow"), drawn))
        check("B does not", count(out.get("b2"), "yellow") <= 2 * noise,
              (count(out.get("b2"), "yellow"), noise))
        # The box A shows is in everybody's scene, flagged. B must not
        # frame it either: its canvas stays as it was.
        check("nor does B re-frame for it",
              abs(count(out.get("b2"), "green") - count(b0, "green"))
              <= 0.1 * count(b0, "green"),
              (count(out.get("b2"), "green"), count(b0, "green")))

        sel = jsonlib.dumps(out.get("selAfterClick"))
        check("a click where Box2 was selects no Box2 in A",
              out.get("clickedAt") is not None and "Box2" not in sel,
              (out.get("clickedAt"), sel))

        check("the host takes A's clear", ok("clearReply"), out.get("clearReply"))
        check("A's clear gives the green back",
              count(out.get("a3"), "green") > 0.5 * green,
              (count(out.get("a3"), "green"), green))
        check("and takes the yellow away",
              count(out.get("a3"), "yellow") <= 2 * noise,
              (count(out.get("a3"), "yellow"), noise))
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
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, build)
