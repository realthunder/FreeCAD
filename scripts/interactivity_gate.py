# Is the GUI thread ALIVE while the heavy machinery runs?
# (docs/SceneStreaming.md sec 13c: the descent on the worker pool;
# docs/DocumentLoad.md: the progressive load.)
#
# Two phases, one instrument. A 25ms QTimer runs from before the
# document opens to the end of the run; every firing records how late
# it was. The timer fires whenever the event loop turns -- including
# the processEvents() the sequencer pumps inside a blocking restore --
# so a stretch with no firing IS a stretch where a user's click would
# have sat in the queue. The gap histogram is the responsiveness
# verdict; nothing here infers from wall time.
#
#   Phase LOAD:  open the model with Progressive document load on and
#                sample until the deferred visual drain reports done.
#                The same ticks sample the status-bar progress bar
#                (visible? value?) -- a bar that never moves while the
#                restore holds the thread is the defect being tested
#                for, not a cosmetic.
#   Phase DROP:  pin the GPU budget high, let the first plans settle,
#                then drop it live (the 148->64 that froze the GUI for
#                ~4 minutes when the descent still built on the GUI
#                thread) and sample until the ladder converges again.
#
# The gate: no event-loop gap over FC_GAP_LIMIT_MS (default 200) in
# the DROP phase. The LOAD phase reports the same numbers and its
# progress-bar liveness; its verdict line says PASS/FAIL by the same
# limit so a regression is one grep away.
#
#   export LD_LIBRARY_PATH=~/opt/virtualgl/usr/lib:$LD_LIBRARY_PATH
#   cd ~/works/sw/fcad && env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb \
#     xvfb-run -a --server-args='-screen 0 1920x1200x24' \
#     ~/opt/virtualgl/opt/VirtualGL/bin/vglrun -d egl0 \
#     .conda/run.sh build/conda-relwithdebinfo-801/bin/FreeCAD \
#     --log-file ~/gate.log scripts/interactivity_gate.py
#
# /!\ Verify the NVIDIA string in the log; llvmpipe is silent.
# The budget/debug knobs are GLOBAL RenderParams now (2026-08-14):
#     saved per-view overrides no longer shadow them (old docs' copies
#     are stripped on restore), so the budget is set once via ParamGet.
#
# FC_MODEL, FC_LOG, FC_OUT, FC_BUDGET_HIGH_MB (148), FC_BUDGET_LOW_MB
# (64), FC_GAP_LIMIT_MS (200), FC_SETTLE_S (45), FC_MAX_WAIT (600),
# FC_CAMERA (fit|inside), FC_TOL (px; pin the refine tolerance -- a
# huge value disarms the climb entirely, which is the arm that
# measures the DESCENT machinery alone: a gap that survives it cannot
# be a climb landing).
import os
import re
import sys
import time

import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

MODEL = os.path.expanduser(
    os.environ.get("FC_MODEL", "~/works/sw/models/server_imported.FCStd"))
LOG = os.path.expanduser(os.environ.get("FC_LOG", "~/gate.log"))
OUT = os.path.expanduser(os.environ.get("FC_OUT", "~/interactivity_gate.txt"))
HIGH = int(os.environ.get("FC_BUDGET_HIGH_MB", "148"))
LOW = int(os.environ.get("FC_BUDGET_LOW_MB", "64"))
GAP_LIMIT = float(os.environ.get("FC_GAP_LIMIT_MS", "200"))
SETTLE_S = float(os.environ.get("FC_SETTLE_S", "45"))
MAX_WAIT = float(os.environ.get("FC_MAX_WAIT", "600"))
CAMERA = os.environ.get("FC_CAMERA", "fit")
TOL = os.environ.get("FC_TOL", "")
# FC_TIMING=1 arms the render stage timers (DebugTiming) for frame-cost
# attribution. Written BOTH ways on purpose: parameters persist in
# user.cfg between runs, and a measurement switch left on by one run
# poisons the next one's timings.
TIMING = os.environ.get("FC_TIMING", "") not in ("", "0")
TICK_MS = 25

LINES = []


def emit(msg):
    LINES.append(msg)
    sys.stdout.write("GATE %s\n" % msg)
    sys.stdout.flush()


PLAN = re.compile(
    r"live ([0-9.]+)MB \(uploaded [0-9.]+MB, [0-9.]+MB stale in \d+ of "
    r"(\d+) entries\).*?plan: refine (\d+) demote (\d+) downgrade (\d+)")


class Sampler(object):
    """The event-loop stethoscope: a repeating timer whose lateness is
    the time the loop spent NOT serving events. Also peeks at the
    status-bar progress bar each firing, because the ticks during a
    blocking restore happen exactly when the sequencer lets the loop
    breathe -- which is when the bar repaints too."""

    def __init__(self):
        self.timer = QtCore.QTimer()
        self.timer.setInterval(TICK_MS)
        self.timer.timeout.connect(self._tick)
        self.last = None
        self.gaps = []          # (at, gap_ms)
        self.bar_values = []    # distinct values seen, in order
        self.bar_visible_ticks = 0
        self.sb_hidden_ticks = 0
        self.ticks = 0

    def _tick(self):
        now = time.monotonic()
        if self.last is not None:
            gap = (now - self.last) * 1000.0 - TICK_MS
            if gap > 0:
                self.gaps.append((now, gap))
        self.last = now
        self.ticks += 1
        # Un-cached and over the WHOLE window: which QProgressBar is
        # the sequencer's is itself part of what is being diagnosed.
        mw = Gui.getMainWindow()
        if mw is None:
            return
        if not mw.statusBar().isVisible():
            self.sb_hidden_ticks += 1
        for bar in mw.findChildren(QtWidgets.QProgressBar):
            if bar.isVisible():
                self.bar_visible_ticks += 1
                v = bar.value()
                if not self.bar_values or self.bar_values[-1] != v:
                    self.bar_values.append(v)
                break

    def start(self):
        self.last = time.monotonic()
        self.timer.start()

    def phase(self):
        """Cut here: return and reset the phase's numbers."""
        gaps = [g for _, g in self.gaps]
        t0 = self.gaps[0][0] if self.gaps else time.monotonic()
        report = {
            "ticks": self.ticks,
            "maxgap": max(gaps) if gaps else 0.0,
            "over": sum(1 for g in gaps if g >= GAP_LIMIT),
            "worst": sorted(gaps, reverse=True)[:5],
            "trace": [(at - t0, g) for at, g in self.gaps
                      if g >= GAP_LIMIT][:200],
            "bar_values": len(self.bar_values),
            "bar_seen": self.bar_visible_ticks,
            "sb_hidden": self.sb_hidden_ticks,
        }
        self.gaps, self.bar_values = [], []
        self.bar_visible_ticks, self.sb_hidden_ticks, self.ticks = 0, 0, 0
        # A phase cut is not a gap: restart the clock.
        self.last = time.monotonic()
        return report


def report_phase(name, rep, elapsed, gate):
    emit("%s: %.0fs, %d ticks, max gap %.0fms, %d gaps >= %.0fms"
         % (name, elapsed, rep["ticks"], rep["maxgap"], rep["over"],
            GAP_LIMIT))
    if rep["worst"]:
        emit("%s: worst gaps: %s"
             % (name, " ".join("%.0f" % g for g in rep["worst"])))
    # The whole trace, timestamped from the phase start, so a stall can
    # be laid beside the plan cadence instead of guessed at.
    for at, g in rep.get("trace", []):
        emit("%s: gap +%.1fs %.0fms" % (name, at, g))
    if gate:
        emit("%s: %s (limit %.0fms)"
             % (name, "PASS" if rep["maxgap"] < GAP_LIMIT else "FAIL",
                GAP_LIMIT))


def tail(marker, since):
    try:
        with open(LOG, errors="replace") as f:
            f.seek(since)
            body = f.read()
            return ([ln for ln in body.splitlines() if marker in ln],
                    since + len(body.encode("utf-8", "replace")))
    except Exception as exc:
        return (["(log unreadable: %s)" % exc], since)


def pump(seconds, view=None):
    end = time.time() + seconds
    while time.time() < end:
        if view is not None:
            try:
                view.redraw()
            except Exception:
                pass
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.005)


def run():
    try:
        App.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
            "AutoSaveEnabled", False)
        App.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        App.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)
        rp = App.ParamGet("User parameter:BaseApp/Preferences/View/Render")
        rp.SetString("Type", "bgfx - OpenGL")
        rp.SetBool("LevelDebug", True)
        rp.SetBool("SimplifyExhausted", True)
        rp.SetBool("SimplifyMergeParts", False)
        rp.SetBool("MeshSkipRedundant", True)
        rp.SetBool("MeshSkipFinerResident", False)
        # The deflection-invariance rule. Written BOTH ways (params
        # persist): FC_MESH_INVARIANT=off is the AUDIT arm -- the rule
        # is still evaluated under LevelDebug and scored against the
        # call it would have skipped, which is the only arm whose WRONG
        # column measures anything.
        rp.SetBool("MeshSkipInvariant",
                   os.environ.get("FC_MESH_INVARIANT", "on") != "off")
        # The landing rule, same two arms (FC_MESH_LANDED=off is the
        # audit arm: claims scored, calls still made). Written BOTH
        # ways because params persist between runs.
        rp.SetBool("MeshSkipLanded",
                   os.environ.get("FC_MESH_LANDED", "on") != "off")
        # The pooled fill of big landing rebuilds, same two arms
        # (FC_VISUAL_FILL=off is the baseline: every fill inline).
        # Written BOTH ways because params persist between runs.
        rp.SetBool("VisualFillOnPool",
                   os.environ.get("FC_VISUAL_FILL", "on") != "off")
        rp.SetBool("Occlusion", False)
        rp.SetBool("DowngradeLedger", True)
        rp.SetBool("ClimbHardLimit", True)
        rp.SetBool("ProgressiveLoad", True)
        rp.SetBool("DebugTiming", TIMING)
        # The user param file pins 64 on this box; the LOAD phase must
        # run at the HIGH budget or the descent starts inside the load.
        rp.SetInt("GpuMemoryBudgetMB", HIGH)
        emit("arm: high=%dMB low=%dMB gap-limit=%.0fms camera=%s tick=%dms "
             "skip-invariant=%s"
             % (HIGH, LOW, GAP_LIMIT, CAMERA, TICK_MS,
                os.environ.get("FC_MESH_INVARIANT", "on")))

        Gui.getMainWindow().resize(1920, 1200)
        # A killed instance loses the saved status-bar toggle
        # (Preferences/MainWindow/StatusBar), and loadWindowSettings
        # re-applies it AFTER this script's first turn -- so write the
        # param, flush the loop, then assert visibility on top.
        App.ParamGet("User parameter:BaseApp/Preferences/MainWindow").SetBool(
            "StatusBar", True)
        QtCore.QCoreApplication.processEvents()
        # A startup script can run BEFORE the main window is shown (the
        # slower the GL bring-up, the likelier), and a load started then
        # races the show: every widget reads hidden for the whole open
        # and the bar verdict is about the race, not the bar. A user's
        # open happens in a shown window; wait for one.
        mw = Gui.getMainWindow()
        waited = time.time()
        while not mw.isVisible() and time.time() - waited < 15.0:
            QtCore.QCoreApplication.processEvents()
            time.sleep(0.01)
        emit("mainwindow visible=%s after %.1fs wait"
             % (mw.isVisible(), time.time() - waited))
        mw.statusBar().setVisible(True)
        QtCore.QCoreApplication.processEvents()

        sampler = Sampler()
        sampler.start()
        since = os.path.getsize(LOG) if os.path.exists(LOG) else 0

        # ---- Phase LOAD -------------------------------------------------
        t0 = time.time()
        doc = App.openDocument(MODEL)
        App.setActiveDocument(doc.Name)
        opened = time.time()
        emit("load: openDocument returned in %.1fs (%d objects)"
             % (opened - t0, len(doc.Objects)))
        # The drain continues behind the open; it reports per document
        # when its queue empties. Wait for that line (or quiet).
        drained, lastNew = False, time.time()
        while time.time() - t0 < MAX_WAIT:
            pump(0.5)
            fresh, since = tail("progressive load", since)
            if fresh:
                lastNew = time.time()
                if any("visuals in" in ln for ln in fresh):
                    drained = True
                    break
            # No progressive-load line at all (feature off or nothing
            # deferred): stop once nothing new said anything for 10s.
            if time.time() - lastNew > 10.0:
                break
        loadRep = sampler.phase()
        report_phase("load", loadRep, time.time() - t0, gate=True)
        emit("load: drain %s" % ("reported done" if drained
                                 else "never reported (quiet 10s)"))
        # The bar's liveness is its own verdict, apart from the gaps:
        # a bar that sat at one value while the thread was busy is a
        # dead readout even if the thread was busy for a good reason.
        emit("load: progress bar seen on %d ticks, %d distinct values, "
             "statusbar hidden on %d ticks -- %s"
             % (loadRep["bar_seen"], loadRep["bar_values"],
                loadRep["sb_hidden"],
                "LIVE" if loadRep["bar_values"] >= 5 else
                "STUCK" if loadRep["bar_seen"] else "NEVER SHOWN"))

        # ---- Camera (placed once), budget pinned high -------------------
        v = Gui.ActiveDocument.ActiveView
        # The budget and debug knobs are globals now, but LevelTolerance
        # is still a per-view display property, and the document restores
        # SEVERAL saved 3D views: the rendering one is not reliably
        # ActiveView (a run once wrote a per-view drop to a non-rendering
        # view and measured 600s of nothing). Per-view writes therefore
        # still go to EVERY 3D view.
        def views3d():
            try:
                return list(Gui.ActiveDocument.mdiViewsOfType(
                    "Gui::View3DInventor"))
            except Exception:
                return [v]
        def override(name, value):
            n = 0
            for vv in views3d():
                try:
                    setattr(vv, name, value)
                    n += 1
                except Exception:
                    pass
            return n
        emit("overrides go to %d 3D view(s)" % len(views3d()))
        if TOL:
            override("Render_LevelTolerance", float(TOL))
            emit("refine tolerance pinned to %spx (climb %s)"
                 % (TOL, "DISARMED" if float(TOL) >= 100 else "active"))
        v.viewIsometric()
        Gui.SendMsgToActiveView("ViewFit")
        QtCore.QCoreApplication.processEvents()
        if CAMERA == "inside":
            import pivy.coin as coin
            pump(2.0, v)
            cam = v.getCameraNode()
            pos = cam.position.getValue()
            fwd = cam.orientation.getValue().multVec(coin.SbVec3f(0, 0, -1))
            dist = float(cam.focalDistance.getValue())
            span = (float(cam.height.getValue())
                    if hasattr(cam, "height") else dist)
            centre = coin.SbVec3f(pos[0] + fwd[0] * dist,
                                  pos[1] + fwd[1] * dist,
                                  pos[2] + fwd[2] * dist)
            v.setCameraType("Perspective")
            QtCore.QCoreApplication.processEvents()
            cam = v.getCameraNode()
            cam.position.setValue(centre)
            cam.orientation.setValue(
                coin.SbRotation(coin.SbVec3f(0, 0, -1),
                                coin.SbVec3f(fwd[0], fwd[1], fwd[2])))
            cam.nearDistance.setValue(span * 1.0e-3)
            cam.focalDistance.setValue(span * 0.25)
            cam.farDistance.setValue(span * 4.0)
            emit("inside camera at (%.1f %.1f %.1f)"
                 % (centre[0], centre[1], centre[2]))

        # Let the ladder do its first work against the HIGH budget so
        # the drop below is a live reconfiguration, not part of the
        # initial build.
        emit("settling %.0fs at %dMB..." % (SETTLE_S, HIGH))
        pump(SETTLE_S, v)
        sampler.phase()  # discard: the settle is not under test

        # ---- Phase DROP -------------------------------------------------
        emit("dropping budget %d -> %d MB live" % (HIGH, LOW))
        t1 = time.time()
        rp.SetInt("GpuMemoryBudgetMB", LOW)
        emit("drop written to the global GpuMemoryBudgetMB parameter")
        window, quiet = [], 0
        while time.time() - t1 < MAX_WAIT:
            pump(0.5, v)
            fresh, since = tail("render levels:", since)
            for ln in fresh:
                m = PLAN.search(ln)
                if not m:
                    continue
                live, entries = float(m.group(1)), int(m.group(2))
                if entries == 0:
                    continue
                # Quiet = no DESCENT moves. Refines are the ladder
                # buying quality back; a stable under-budget run keeps
                # climbing for a while and must not read as unsettled
                # (a 58.8/64MB run once churned its whole 600s window
                # on refine moves alone).
                moves = sum(int(m.group(i)) for i in (4, 5))
                window.append((live, moves))
                window[:] = window[-4:]
            # Quiet alone is not settled: the ledger holds the sweep
            # while its worker jobs are still landing, so plans read
            # "downgrade 0" with live far over budget mid-descent. Only
            # a quiet window that has also ARRIVED (live near the new
            # budget) ends the phase.
            if (len(window) == 4 and all(mv == 0 for _, mv in window)
                    and 0 < window[-1][0] <= LOW * 1.15):
                break
        dropRep = sampler.phase()
        report_phase("drop", dropRep, time.time() - t1, gate=True)
        if window:
            emit("drop: settled live %.1fMB against %dMB in %.0fs"
                 % (window[-1][0], LOW, time.time() - t1))
        else:
            emit("drop: NO PLAN OBSERVED -- not a measurement")
        emit("DONE")
    except Exception as exc:
        import traceback
        emit("FAILED %s" % exc)
        emit(traceback.format_exc())
    finally:
        try:
            with open(OUT, "w") as f:
                f.write("\n".join(LINES) + "\n")
        except Exception:
            pass
        QtCore.QTimer.singleShot(1000, QtWidgets.QApplication.quit)


QtCore.QTimer.singleShot(0, run)
