"""What the REAL browser client puts on the uplink, under each policy.

The other half of the camera-uplink experiment (docs/ThinClient.md sec
8.10a). tests/gui/camera-uplink-bench.py measures the policies with a
synthetic client that speaks the wire itself -- repeatable, no browser,
no GPU. That measures the policy. It does not measure
src/Gui/Renderer/wasm/main.cpp, which is where the policy actually has
to live, and where a `?camup=` that silently did nothing would look
exactly like a policy that does not help.

So: serve a document, serve the built viewer, and let Chrome load it
under each `?camup=` in turn -- settle, one continuous orbit, then
clicks. The counters are read HERE, in the serving process, per
connection (Gui.serveClients()); the browser is only asked to move the
mouse. A client counting its own sends is the client marking its own
work, which is the objection sec 8.6 makes about selection and which
applies to this just as much.

What it asserts is the shape of each policy, not a byte count: the
browser's frame rate is the machine's, and an orbit under swiftshader
runs at whatever it runs at. Under `frame` the camera frames scale with
frames drawn; under `rate` they are near the throttle; under `lazy` the
orbit is silent and only clicks state a camera; under `lazy1` no camera
frame is ever sent at all, because it rides inside the pick. And under
every one of them a click still selects something -- a policy that saved
the bytes by delivering a stale camera fails there rather than scoring
well.

Needs the built viewer (build/wasm/fcviewer.html; `ninja -C build/wasm`),
node, a puppeteer-core install named by PUPPETEER_PATH, and a Chrome
named by CHROME. Skips rather than fails when any of those is missing --
it is an integration check, and its absence must not read as a defect in
the code it checks.

  PUPPETEER_PATH=/path/to/node_modules/puppeteer-core \
  CHROME=~/.cache/puppeteer/chrome/*/chrome-linux64/chrome \
  scripts/gui-test.sh tests/gui/camera-uplink-browser.py /tmp/camup-web \
      --timeout 900
"""
import functools
import http.server
import json
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
# .../tests/gui/this file -> the repository root, three up.
REPO = os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__))))
WASM = os.path.join(REPO, "build", "wasm")
DRIVER = os.path.join(REPO, "scripts", "camup-drive.js")
DOC = "CameraUplinkBrowser"

# CAMUP_POLICIES narrows the run to a subset, comma separated: four
# policies is a few minutes, and one is enough when the question is
# about the viewer rather than about the policies.
POLICIES = [p for p in os.environ.get(
    "CAMUP_POLICIES", "frame,rate,lazy,lazy1").split(",") if p]
SETTLE_MS = 8000
ORBIT_MS = 10000
CLICKS = 4
THROTTLE_HZ = 10          # what ?camuphz= is set to below
RUN_WAIT_S = 180

state = {"doc": None, "port": 0, "http": 0, "done": False, "rows": [],
         "run": None, "t0": clock()}


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
    """One policy: drive the browser while the GUI thread watches the
    phases it marks.

    The driver's output goes to a file rather than through a pipe, and
    the phases come back through a file of their own. Not fastidiousness:
    node buffers its stdout to a pipe, so the markers arrive in a block
    when the process ends, and the counter samples they are meant to
    trigger would all be taken at the end -- charging a whole run to
    whichever phase happened to be last. A file append is seen at once.
    """

    def __init__(self, policy, url):
        super().__init__(daemon=True)
        self.policy = policy
        self.url = url
        self.error = None
        self.log_path = os.path.join(OUT, "drive-%s.log" % policy)
        self.phase_path = os.path.join(OUT, "phases-%s.txt" % policy)

    def run(self):
        try:
            env = dict(os.environ)
            # The bundled Chrome needs libasound, which lives in the
            # conda env this build runs against; conda activation
            # replaces LD_LIBRARY_PATH, so a caller that exported it
            # loses it before we get here. Without this the browser dies
            # at launch with code 127 (see scripts/wasm-chrome.js).
            libs = os.path.join(REPO, ".conda", "freecad", "lib")
            env["LD_LIBRARY_PATH"] = (
                libs + os.pathsep + env.get("LD_LIBRARY_PATH", ""))
            env["CAMUP_PHASES"] = self.phase_path
            if os.environ.get("CAMUP_REAL"):
                # The real-GPU tier draws on the WSLg desktop, and this
                # harness runs under xvfb-run -- which set DISPLAY to its
                # own headless server and unset WAYLAND_DISPLAY. Chrome
                # would land there, on no GPU at all, and the run would
                # look like a slow real one rather than a headless one.
                env["DISPLAY"] = os.environ.get("CAMUP_DISPLAY", ":0")
                env.pop("WAYLAND_DISPLAY", None)
            with open(self.log_path, "w") as log:
                proc = subprocess.Popen(
                    ["node", DRIVER, self.url, str(SETTLE_MS), str(ORBIT_MS),
                     str(CLICKS)],
                    stdout=log, stderr=subprocess.STDOUT, env=env)
                proc.wait()
            if proc.returncode != 0:
                self.error = "node exited %d:\n%s" % (
                    proc.returncode, self.tail())
        except Exception:
            self.error = traceback.format_exc()

    def tail(self, lines=20):
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


def serve_viewer():
    """The built viewer over HTTP, in this process -- one less child to
    leak, and it dies with us."""
    handler = functools.partial(http.server.SimpleHTTPRequestHandler,
                                directory=WASM)
    handler.log_message = lambda *a, **k: None
    port = free_port()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    state["httpd"] = server
    return port


def counters():
    """Every viewer connection's uplink counters, newest first. The
    browser makes one connection per run and it is gone by the next, so
    the live one is the run's."""
    return [c for c in FreeCADGui.serveClients() if c.get("viewer")]


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

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        doc = FreeCAD.newDocument(DOC, hidden=True)
        state["doc"] = doc
        box = doc.addObject("Part::Box", "Box")
        box.Length = box.Width = box.Height = 10
        doc.recompute()
        port = free_port()
        state["port"] = port
        if not check("the document is served headless",
                     FreeCADGui.serveDocument(doc, port), "port %d" % port):
            finish()
            return
        state["http"] = serve_viewer()
        note("viewer on %d, scene on %d" % (state["http"], port))
        next_policy()
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


# CAMUP_EXTRA appends viewer parameters, for pinning something about
# the viewer while the policies are measured (?msaa=0 is the one this
# was added for).
EXTRA = os.environ.get("CAMUP_EXTRA", "")


def url_for(policy):
    return ("http://127.0.0.1:%d/fcviewer.html?scene=http://127.0.0.1:%d"
            "&camup=%s&camuphz=10%s"
            % (state["http"], state["port"], policy, EXTRA))


def next_policy():
    done = [r["policy"] for r in state["rows"]]
    remaining = [p for p in POLICIES if p not in done]
    if not remaining:
        report()
        return
    policy = remaining[0]
    run = Run(policy, url_for(policy))
    state["run"] = run
    state["seen_phases"] = 0
    state["samples"] = {}
    state["t0"] = clock()
    run.start()
    QtCore.QTimer.singleShot(50, poll)


def poll():
    run = state["run"]
    marked = run.phases()
    while len(marked) > state["seen_phases"]:
        name = marked[state["seen_phases"]]
        state["seen_phases"] += 1
        rows = counters()
        state["samples"][name] = (clock(), rows[-1] if rows else None)
        if name == "clicked":
            state["samples"]["selection"] = FreeCADGui.Selection.getSelectionEx(DOC)
    if run.is_alive():
        if clock() - state["t0"] > RUN_WAIT_S:
            check("the %s run finished" % run.policy, False,
                  "still running after %ds" % RUN_WAIT_S)
            finish()
            return
        QtCore.QTimer.singleShot(50, poll)
        return
    collect(run)


def collect(run):
    if run.error:
        check("the %s run had no error" % run.policy, False, run.error)
        finish()
        return
    row = {"policy": run.policy}
    samples = state["samples"]
    for phase in ("settled", "orbited", "clicked"):
        if phase not in samples or samples[phase][1] is None:
            check("the %s run reached '%s' with a connection"
                  % (run.policy, phase), False,
                  "phases: %s" % (run.phases(),))
            note("---- driver log")
            note(run.tail(15))
            finish()
            return
    settled_t, settled = samples["settled"]
    orbited_t, orbited = samples["orbited"]
    clicked_t, clicked = samples["clicked"]
    row["orbit_s"] = orbited_t - settled_t
    row["orbit_cam"] = orbited["cameraMsgs"] - settled["cameraMsgs"]
    row["orbit_wire"] = orbited["uplinkWire"] - settled["uplinkWire"]
    row["click_cam"] = clicked["cameraMsgs"] - orbited["cameraMsgs"]
    row["click_picks"] = clicked["pickMsgs"] - orbited["pickMsgs"]
    row["click_wire"] = clicked["uplinkWire"] - orbited["uplinkWire"]
    row["selected"] = [(s.ObjectName, tuple(s.SubElementNames))
                       for s in samples.get("selection", [])]
    row["said"] = [l.strip() for l in run.tail(400).splitlines()
                   if "camera uplink" in l]
    state["rows"].append(row)
    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    next_policy()


def report():
    note("")
    note("=== the browser's camera uplink, per policy (server-counted) ===")
    note("%-6s %8s %8s %9s %8s %8s" %
         ("policy", "orbit s", "cam/s", "orbit B/s", "clicks", "click cam"))
    for row in state["rows"]:
        note("%-6s %8.1f %8.1f %9.1f %8d %8d" % (
            row["policy"], row["orbit_s"],
            row["orbit_cam"] / row["orbit_s"],
            row["orbit_wire"] / row["orbit_s"],
            row["click_picks"], row["click_cam"]))
    note("")
    for row in state["rows"]:
        note("%-6s said: %s" % (row["policy"], "; ".join(row["said"]) or "(nothing)"))
        note("%-6s selection after the clicks: %s"
             % (row["policy"], row["selected"]))

    by = {r["policy"]: r for r in state["rows"]}
    for policy, row in by.items():
        check("%s: the browser picked something through the mirror" % policy,
              bool(row["selected"]) and row["click_picks"] > 0,
              "picks %d, selection %s" % (row["click_picks"], row["selected"]))
    if "frame" in by and "lazy" in by:
        check("lazy states no camera through an orbit",
              by["lazy"]["orbit_cam"] == 0,
              "%d camera frames" % by["lazy"]["orbit_cam"])
        # The shape, not a rate: how many frames A states is how many
        # the page drew, and under swiftshader that is a number between
        # one and two a second. What is being asserted is that A states
        # its camera while the camera moves and C states none at all.
        check("frame states a camera through an orbit and lazy states none",
              by["frame"]["orbit_cam"] > 0 and by["lazy"]["orbit_cam"] == 0,
              "frame %d in %.1f s (%.1f drawn frames/s), lazy %d"
              % (by["frame"]["orbit_cam"], by["frame"]["orbit_s"],
                 by["frame"]["orbit_cam"] / by["frame"]["orbit_s"],
                 by["lazy"]["orbit_cam"]))
        check("lazy costs less than frame through an orbit",
              by["lazy"]["orbit_wire"] < by["frame"]["orbit_wire"],
              "%d B vs %d B" % (by["lazy"]["orbit_wire"],
                                by["frame"]["orbit_wire"]))
    if "rate" in by and "frame" in by:
        # Only when the page draws faster than the throttle. Under
        # swiftshader it draws at one or two frames a second, and a
        # 10 Hz throttle that never binds leaves B doing exactly what A
        # does -- which is the policy behaving correctly, and would read
        # as a failure if asserted unconditionally.
        drawn = by["frame"]["orbit_cam"] / by["frame"]["orbit_s"]
        if drawn > THROTTLE_HZ:
            check("rate is throttled below frame",
                  by["rate"]["orbit_cam"] < by["frame"]["orbit_cam"],
                  "%d vs %d camera frames" % (by["rate"]["orbit_cam"],
                                              by["frame"]["orbit_cam"]))
        else:
            note("SKIP rate vs frame: the page drew %.1f frames/s, below "
                 "the %d Hz throttle, so B and A are the same policy here"
                 % (drawn, THROTTLE_HZ))
    if "lazy1" in by:
        check("lazy1 sends no camera frame at all -- it rides the pick",
              by["lazy1"]["orbit_cam"] == 0 and by["lazy1"]["click_cam"] == 0,
              "orbit %d, clicks %d" % (by["lazy1"]["orbit_cam"],
                                       by["lazy1"]["click_cam"]))
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    server = state.get("httpd")
    if server:
        server.shutdown()
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(0, build)
