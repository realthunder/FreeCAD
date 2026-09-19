"""What the camera uplink costs, under each of the three policies.

The experiment of docs/ThinClient.md sec 8.10a. Stage 3 sends the `'C'`
camera frame once per client frame whenever it changed, and again before
every pick. In view mode the only reader of a mirror's camera is a click
(`SceneServeSource::mirrorFor` has exactly one caller), so an orbit that
is never clicked through pays sixty frames a second for an answer nobody
asks for -- on the uplink, which is the scarce direction on the links
this tier exists for.

Three policies, measured against each other on one served document:

  frame  (A, as built) one frame per client frame when the camera
         changed, plus a forced one before each pick.
  rate   (B)           the same, throttled while nothing is being
         picked, still forced before a pick.
  lazy   (C)           no periodic send at all: the camera goes up with
         the click, and only when it differs from the last one sent.
  lazy1  (C')          the same, carried INSIDE the click as one 'Q'
         message rather than as a 'C' frame ahead of it.

The policies live in the browser viewer (`?camup=`, src/Gui/Renderer/
wasm/main.cpp); what runs here is the same policy over the same wire
frames, driven from a synthetic client so the measurement is repeatable
and has no browser, no GPU and no compositor in it. That the real client
implements the same policy is a separate question, and the answer to it
is the same counters read while a browser drives -- see the doc.

**The bytes are counted on the server**, per connection, through
Gui.serveClients() (SceneClientInfo's uplink counters). A client counting
its own sends is the client marking its own work; sec 8.6 makes that
objection about selection and it applies here unchanged.

Three phases per policy: an idle camera (which costs nothing under any
policy, and saying so is the point -- A is expensive while moving, not
always), a camera in continuous motion, and clicks during that motion.
The clicks are the correctness witness: each is aimed three pixels clear
of the box silhouette, which only lands on an edge if the server is
resolving it through a mirror holding the camera the ray was computed in
(see serve-mirror-pick.py). A policy that saves bytes by delivering a
stale camera fails here rather than scoring well.

Not a pass/fail test and not registered in ctest -- it is a measurement,
and it takes about a minute. Run it the way the tests run:

  scripts/gui-test.sh tests/gui/camera-uplink-bench.py /tmp/camup --timeout 400
"""
import math
import os
import threading
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

import wsclient
from wsclient import WS, free_port

clock = time.perf_counter

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "CameraUplinkBench"

POLICIES = ["frame", "rate", "lazy", "lazy1"]
IDLE_S = 2.0            # a camera that is not moving
MOTION_S = 10.0         # a camera in continuous motion
TICK_HZ = 60.0          # the client frame rate the policies run at
RATE_MS = 100.0         # policy B's throttle (the ?camuphz=10 default)
CLICK_EVERY_S = 1.5
BENCH_WAIT_S = 240

# The camera: eye above the box looking straight down, which in Coin's
# convention (down its own -Z with +Y up) is the identity rotation. The
# motion pans and dollies rather than orbiting -- the ray arithmetic
# stays legible that way, and the uplink cannot tell the difference: the
# 'C' frame is fifty-eight bytes whatever the camera did.
EYE0 = (5.0, 5.0, 60.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 200.0
VW, VH = 800, 600
ASPECT = float(VW) / float(VH)
PICK_RADIUS = 5.0
TOP_Z = 10.0
TH = math.tan(0.5 * HEIGHT_ANGLE)

state = {"doc": None, "port": 0, "done": False, "t0": clock(),
         "label": None, "sample": None, "samples": {}, "rows": [],
         "bench": None}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def eye_at(t):
    """Where the camera is t seconds into the motion phase."""
    return (EYE0[0] + 4.0 * math.sin(t * 1.7),
            EYE0[1] + 4.0 * math.cos(t * 1.3),
            EYE0[2] + 8.0 * math.sin(t * 0.9))


def units_per_px(eye):
    return ((eye[2] - TOP_Z) * TH * ASPECT) / (0.5 * VW)


def camera_frame(eye):
    return wsclient.camera_frame(eye, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH,
                                 pick_radius=PICK_RADIUS)


def ray_to(x, y, eye):
    depth = eye[2] - TOP_Z
    direction = ((x - eye[0]) / depth, (y - eye[1]) / depth, -1.0)
    length = math.sqrt(sum(c * c for c in direction))
    return eye, tuple(c / length for c in direction)


def edge_ray(eye, side):
    """A ray three pixels clear of the box silhouette -- an edge pick,
    which a zero-radius ray never lands on. Two sides, alternated, so a
    click always changes the selection and so always answers with a
    push."""
    upp = units_per_px(eye)
    x = 10.0 + 3.0 * upp if side else 0.0 - 3.0 * upp
    return ray_to(x, 5.0, eye)


class Bench(threading.Thread):
    """One policy's run: idle, motion, clicks during motion."""

    def __init__(self, port, policy):
        super().__init__(daemon=True)
        self.port = port
        self.policy = policy
        self.label = "camup-" + policy
        self.error = None
        self.ready = False
        self.sent_msgs = 0      # what this side believes it sent, as a
        self.sent_bytes = 0     # cross-check on the server's count
        self.clicks = 0
        self.pushes = 0
        self.latencies = []
        self.last_sent = None
        self.last_sent_at = 0.0
        self.sent_at_begin = (0, 0)

    # -- the wire, under this policy -------------------------------

    def send_camera(self, ws, packed, force=False):
        if not force and packed == self.last_sent:
            return False
        ws.send(2, packed)
        self.sent_msgs += 1
        self.sent_bytes += wsclient.wire_bytes(packed)
        self.last_sent = packed
        self.last_sent_at = clock()
        return True

    def tick_camera(self, ws, packed):
        """The per-frame send, under whichever policy is in force."""
        if self.policy == "frame":
            self.send_camera(ws, packed)
        elif self.policy == "rate":
            # Throttled, not dropped: the cache is left alone when a send
            # is skipped, so a camera that stops moving is stated in the
            # end.
            if (clock() - self.last_sent_at) * 1000.0 >= RATE_MS:
                self.send_camera(ws, packed)
        # lazy and lazy1 say nothing until a click.

    def click(self, ws, packed, eye, side):
        ws.drain(0.02)
        pick = wsclient.pick_frame(*edge_ray(eye, side))
        if self.policy == "lazy1" and packed != self.last_sent:
            both = wsclient.camera_and_pick(packed, pick)
            ws.send(2, both)
            self.sent_msgs += 1
            self.sent_bytes += wsclient.wire_bytes(both)
            self.last_sent = packed
            self.last_sent_at = clock()
        else:
            if self.policy in ("frame", "rate"):
                self.send_camera(ws, packed, force=True)
            elif self.policy == "lazy":
                self.send_camera(ws, packed)
            ws.send(2, pick)
            self.sent_msgs += 1
            self.sent_bytes += wsclient.wire_bytes(pick)
        started = clock()
        self.clicks += 1
        if ws.next_binary(3.0) is not None:
            self.pushes += 1
            self.latencies.append((clock() - started) * 1000.0)

    # -- the phases ------------------------------------------------

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()

    def talk(self):
        ws = WS(self.port)
        ws.hello(self.label)
        if ws.next_binary(20.0) is None:
            raise RuntimeError("no snapshot")
        ws.drain(0.3)
        self.ready = True

        # State the camera once before the idle phase is sampled, under
        # the policies that would: the first frame of a connection is
        # news whatever the camera is doing, and counting it against the
        # idle phase would say a still camera costs something when what
        # it cost was arriving. The lazy policies state nothing here --
        # that they have nothing to say yet IS the policy.
        packed = camera_frame(EYE0)
        self.tick_camera(ws, packed)
        time.sleep(0.05)
        # This side's own count at the same instant, so the cross-check
        # below compares two deltas over one window rather than a delta
        # against a total.
        self.sent_at_begin = (self.sent_msgs, self.sent_bytes)
        sample("%s.begin" % self.policy)

        # Phase 1: an idle camera. Every policy should now say nothing
        # at all -- A included, which coalesces on the packed bytes.
        deadline = clock() + IDLE_S
        while clock() < deadline:
            self.tick_camera(ws, packed)
            time.sleep(1.0 / TICK_HZ)
        sample("%s.idle" % self.policy)

        # Phase 2 and 3: a camera in continuous motion, clicked through.
        t0 = clock()
        next_click = t0 + CLICK_EVERY_S
        side = True
        tick = 0
        while True:
            now = clock() - t0
            if now >= MOTION_S:
                break
            eye = eye_at(now)
            packed = camera_frame(eye)
            self.tick_camera(ws, packed)
            if clock() >= next_click:
                self.click(ws, packed, eye, side)
                side = not side
                next_click += CLICK_EVERY_S
            tick += 1
            target = t0 + tick / TICK_HZ
            left = target - clock()
            if left > 0:
                time.sleep(left)
        sample("%s.motion" % self.policy)
        ws.close()


# -- the counters, read on the GUI thread ---------------------------

def counters(label):
    for c in FreeCADGui.serveClients():
        if c.get("client") == label:
            return c
    return None


def sample(key):
    """Ask the GUI thread for this connection's counters and wait for the
    answer. The roster is the server's, so it is read where the server's
    own callbacks are marshalled rather than from this thread."""
    state["sample"] = key
    deadline = clock() + 10.0
    while clock() < deadline:
        if key in state["samples"]:
            return state["samples"][key]
        time.sleep(0.005)
    raise RuntimeError("no counter sample for " + key)


def build():
    try:
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
        note("bench: %.0f s idle + %.0f s motion per policy, %d Hz client, "
             "a click every %.1f s" % (IDLE_S, MOTION_S, int(TICK_HZ),
                                       CLICK_EVERY_S))
        next_policy()
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def next_policy():
    remaining = [p for p in POLICIES if p not in [r["policy"] for r in state["rows"]]]
    if not remaining:
        report()
        return
    bench = Bench(state["port"], remaining[0])
    state["bench"] = bench
    state["label"] = bench.label
    state["t0"] = clock()
    bench.start()
    QtCore.QTimer.singleShot(20, poll)


def poll():
    bench = state["bench"]
    key = state["sample"]
    if key and key not in state["samples"]:
        state["samples"][key] = (clock(), counters(state["label"]))
        state["sample"] = None
    if bench.is_alive():
        if clock() - state["t0"] > BENCH_WAIT_S:
            check("the %s run finished" % bench.policy, False,
                  "still running after %ds" % BENCH_WAIT_S)
            finish()
            return
        QtCore.QTimer.singleShot(20, poll)
        return
    collect(bench)
    next_policy()


def collect(bench):
    if bench.error:
        check("the %s run had no error" % bench.policy, False, bench.error)
        finish()
        return
    row = {"policy": bench.policy, "error": bench.error}
    try:
        begin_t, begin = state["samples"]["%s.begin" % bench.policy]
        idle_t, idle = state["samples"]["%s.idle" % bench.policy]
        motion_t, motion = state["samples"]["%s.motion" % bench.policy]
    except KeyError:
        check("the %s run was sampled" % bench.policy, False,
              str(sorted(state["samples"])))
        finish()
        return
    if not (begin and idle and motion):
        check("the %s connection was on the roster" % bench.policy, False,
              "begin=%s idle=%s motion=%s" % (bool(begin), bool(idle), bool(motion)))
        finish()
        return

    def delta(a, b, field):
        return b[field] - a[field]

    row["idle_s"] = idle_t - begin_t
    row["idle_wire"] = delta(begin, idle, "uplinkWire")
    row["idle_cam_msgs"] = delta(begin, idle, "cameraMsgs")
    row["motion_s"] = motion_t - idle_t
    row["motion_wire"] = delta(idle, motion, "uplinkWire")
    row["motion_cam_msgs"] = delta(idle, motion, "cameraMsgs")
    row["motion_cam_wire"] = delta(idle, motion, "cameraWire")
    row["motion_pick_msgs"] = delta(idle, motion, "pickMsgs")
    row["motion_pick_wire"] = delta(idle, motion, "pickWire")
    row["clicks"] = bench.clicks
    row["pushes"] = bench.pushes
    row["latencies"] = sorted(bench.latencies)
    row["sent_msgs"] = bench.sent_msgs - bench.sent_at_begin[0]
    row["sent_wire"] = bench.sent_bytes - bench.sent_at_begin[1]
    row["server_msgs"] = delta(begin, motion, "uplinkMsgs")
    row["server_wire"] = delta(begin, motion, "uplinkWire")
    state["rows"].append(row)


def median(values):
    if not values:
        return float("nan")
    mid = len(values) // 2
    if len(values) % 2:
        return values[mid]
    return 0.5 * (values[mid - 1] + values[mid])


def report():
    note("")
    note("=== camera uplink, per policy (server-counted, one connection) ===")
    note("%-6s %9s %9s %7s %7s %9s %9s" %
         ("policy", "idle B/s", "move B/s", "cam/s", "clicks",
          "push ms", "worst ms"))
    for row in state["rows"]:
        note("%-6s %9.1f %9.1f %7.1f %7d %9.1f %9.1f" % (
            row["policy"],
            row["idle_wire"] / row["idle_s"],
            row["motion_wire"] / row["motion_s"],
            row["motion_cam_msgs"] / row["motion_s"],
            row["clicks"],
            median(row["latencies"]),
            row["latencies"][-1] if row["latencies"] else float("nan")))
    note("")
    for row in state["rows"]:
        note("%-6s motion: %d msgs (%d camera, %d pick), %d B "
             "(%d camera, %d pick)" % (
                 row["policy"], row["motion_cam_msgs"] + row["motion_pick_msgs"],
                 row["motion_cam_msgs"], row["motion_pick_msgs"],
                 row["motion_wire"], row["motion_cam_wire"],
                 row["motion_pick_wire"]))

    # What the run is worth only if these hold.
    for row in state["rows"]:
        check("%s: every click was answered with a push" % row["policy"],
              row["clicks"] > 0 and row["pushes"] == row["clicks"],
              "%d of %d" % (row["pushes"], row["clicks"]))
        check("%s: an idle camera costs nothing" % row["policy"],
              row["idle_cam_msgs"] == 0,
              "%d camera frames while still" % row["idle_cam_msgs"])
        check("%s: the server counted what the client sent" % row["policy"],
              row["server_msgs"] == row["sent_msgs"]
              and row["server_wire"] == row["sent_wire"],
              "server %d msgs / %d B, client %d / %d"
              % (row["server_msgs"], row["server_wire"],
                 row["sent_msgs"], row["sent_wire"]))
    by = {r["policy"]: r for r in state["rows"]}
    if "frame" in by and "lazy" in by:
        a = by["frame"]["motion_wire"] / by["frame"]["motion_s"]
        c = by["lazy"]["motion_wire"] / by["lazy"]["motion_s"]
        note("")
        note("frame -> lazy through the motion: %.1f B/s -> %.1f B/s, "
             "%.1fx less" % (a, c, a / c if c else float("inf")))
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(0, build)
