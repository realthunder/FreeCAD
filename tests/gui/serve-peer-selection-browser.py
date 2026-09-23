"""One real browser paints another's selection, and stops when it leaves.

The browser half of docs/ThinClient.md sec 8.11a. The server half -- the
route itself, the owner-tagged `peerselection` push, the take-back -- is
checked against synthetic clients elsewhere; what no synthetic client can
check is the half that lives in the viewer, which is
src/Gui/Renderer/wasm/main.cpp turning the pushed OBJECT NAMES back into
draws of its own scene and feeding them as a second, per-owner highlight.
A socket client can see the push arrive. It cannot see whether anything
was drawn, and being drawn is the whole of this feature.

So the reading is PIXELS, against a measured noise floor: two shots of a
scene nobody has touched give what this viewer's canvas does on its own,
and every later number is a multiple of that or it is nothing.

What is asserted:

  - the picker's click selects something (otherwise the rest would pass
    by saying nothing about anything);
  - the watcher is TOLD, owner-tagged, naming the object the picker
    picked -- and told on 'fc:peerselection', not 'fc:selection';
  - the watcher's canvas CHANGES when it is told: the names resolved
    against its own scene and something was painted;
  - the watcher's OWN selection never moves while any of it happens.
    That is the ruling under test: a foreign selection is paint, so what
    this client's next command acts on is still what it picked itself;
  - and when the picker disconnects the paint goes, back to within noise
    of the baseline. Nothing else would ever take it down -- the pushes
    are change-driven and a connection that has gone announces nothing.

Both connections are put on the `everyone` route by this harness, which
is the host: a client cannot route itself, which is the point of 8.11a.

Needs the built viewer (build/wasm/fcviewer.html; `cmake --build
build/wasm`), node, a puppeteer-core install named by PUPPETEER_PATH, and
a Chrome named by CHROME. Skips rather than fails when any is missing.
PEERSEL_REAL=1 runs it headful on the real GPU (scripts/wasm-chrome.js),
which is the tier that reproduces pacing and GPU-state bugs -- it opens
windows on the user's screen, so announce it first.

  PUPPETEER_PATH=/path/to/node_modules/puppeteer-core \
  CHROME=~/.cache/puppeteer/chrome/*/chrome-linux64/chrome \
  scripts/gui-test.sh tests/gui/serve-peer-selection-browser.py \
      /tmp/peersel --timeout 600
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
DRIVER = os.path.join(REPO, "scripts", "peersel-drive.js")
DOC = "ServePeerSelection"
SETTLE_MS = 9000
RUN_WAIT_S = 300

state = {"doc": None, "done": False, "run": None, "t0": clock(),
         "routed": None}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
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
        self.go_path = os.path.join(OUT, "go")

    def run(self):
        try:
            env = dict(os.environ)
            env["LD_LIBRARY_PATH"] = (
                os.path.join(REPO, ".conda", "freecad", "lib")
                + os.pathsep + env.get("LD_LIBRARY_PATH", ""))
            env["PEERSEL_PHASES"] = self.phase_path
            env["PEERSEL_RESULT"] = self.result_path
            env["PEERSEL_GO"] = self.go_path
            if os.environ.get("PEERSEL_REAL"):
                env["DISPLAY"] = os.environ.get("PEERSEL_DISPLAY", ":0")
                env.pop("WAYLAND_DISPLAY", None)
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


def build():
    try:
        import Part

        if not os.path.exists(os.path.join(WASM, "fcviewer.html")):
            skip("no built viewer at %s (cmake --build build/wasm)" % WASM)
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
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/View/Render").SetString(
                "Type", "bgfx - OpenGL")
        doc = FreeCAD.newDocument(DOC, hidden=True)
        state["doc"] = doc
        # Two boxes, apart, so a click that lands off centre lands on one
        # of them and the name that comes back says which.
        left = doc.addObject("Part::Box", "Left")
        left.Length, left.Width, left.Height = 10, 10, 10
        left.Placement.Base = FreeCAD.Vector(-16, 0, 0)
        right = doc.addObject("Part::Box", "Right")
        right.Length, right.Width, right.Height = 10, 10, 10
        right.Placement.Base = FreeCAD.Vector(6, 0, 0)
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


def route_everyone():
    """The host's decision, which is the only place it can be made.

    Both connections, because the test does not know which browser page
    the server gave which id -- and every client being on `everyone` is
    the arrangement the route is for.
    """
    clients = [c for c in FreeCADGui.serveClients() if c.get("id")]
    ok = [FreeCADGui.serveSetClientSelection(c["id"], "everyone")
          for c in clients]
    state["routed"] = (len(clients), ok)
    note("routed %d client(s): %s" % (len(clients), ok))
    return len(clients) >= 2 and all(ok)


def poll():
    run = state["run"]
    # The browsers are up and have made contact; now the host routes
    # them, and the driver is waiting on the file that says so.
    if state["routed"] is None and "ready" in run.phases():
        route_everyone()
        try:
            open(run.go_path, "w").close()
        except OSError:
            note("FAIL could not write the go file")
    if run.is_alive():
        if clock() - state["t0"] > RUN_WAIT_S:
            check("the browsers finished", False,
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
        if not check("the browsers ran without error", run.error is None,
                     run.error or ""):
            note(run.tail())
        # Recorded, not asserted: both tiers answer this test, but which
        # one answered it is part of the reading.
        note("drawn by: %s" % out.get("gpu"))
        check("the driver reported no error", not out.get("error"),
              out.get("error", ""))
        routed = state["routed"] or (0, [])
        check("the host put both connections on the everyone route",
              routed[0] >= 2 and all(routed[1]), routed)
        check("and the driver waited for it", out.get("routed") is True, out)

        # Without this the rest would pass by saying nothing about
        # anything: no pick, no push, no paint, three green ticks.
        picked = out.get("pickerSel") or []
        if not check("the picker's click selected something", bool(picked),
                     (out.get("clickedAt"), picked)):
            note(run.tail())
        name = picked[0].get("obj") if picked else None

        peer = out.get("peer") or {}
        check("the watcher was told, as somebody else's",
              bool(peer) and bool(peer.get("owner")), peer)
        told = [i.get("obj") for i in (peer.get("items") or [])]
        check("naming the object the picker picked", name and name in told,
              (name, told))

        # The measured floor. Everything below is read against it.
        noise = out.get("noise")
        check("the watcher's canvas is quiet when nobody picks",
              isinstance(noise, int) and noise >= 0, noise)
        painted = out.get("paintedDiff")
        check("and CHANGES when it is told -- the names resolved and "
              "something was drawn",
              isinstance(painted, int) and isinstance(noise, int)
              and painted > max(200, noise * 4),
              "painted %s vs noise %s (palette %s, was %s)"
              % (painted, noise, out.get("paintedPalette"),
                 out.get("basePalette")))

        # The ruling: it is paint and nothing else.
        check("the watcher's own selection never moved",
              out.get("watcherSelBefore") == out.get("watcherSelAfter")
              == out.get("watcherSelEnd"),
              (out.get("watcherSelBefore"), out.get("watcherSelAfter"),
               out.get("watcherSelEnd")))
        check("and it holds nothing it did not pick itself",
              not (out.get("watcherOwnSel") or []), out.get("watcherOwnSel"))

        # And the departure.
        check("the watcher is told when the peer leaves",
              (out.get("peerPushesAfterGone") or 0)
              > (out.get("peerPushes") or 0),
              (out.get("peerPushes"), out.get("peerPushesAfterGone")))
        gone = out.get("peerAfterGone")
        # Stated as "there was a push AND it was empty": on a viewer that
        # never consumed one at all there is no push, and a check that
        # only asked whether the items were empty would pass on nothing.
        check("with an empty set",
              isinstance(gone, dict) and not (gone.get("items") or []), gone)
        check("and the paint goes with it",
              isinstance(out.get("goneVsPainted"), int)
              and out["goneVsPainted"] > max(200, (noise or 0) * 4),
              (out.get("goneVsPainted"), noise))
        check("back to the canvas it started from",
              isinstance(out.get("goneVsBase"), int)
              and out["goneVsBase"] <= max(200, (noise or 0) * 4),
              (out.get("goneVsBase"), noise))
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
