# What does the level plan SETTLE at under a pinned GPU budget?
# (docs/SceneStreaming.md sec 13, 13c.2)
#
# This exists because the first attempt to compare two ladder arms was
# taken wrong, and the way it was wrong is worth stating because it is
# easy to repeat:
#
#   * it drove the scene for a fixed number of WALL CLOCK seconds and
#     then summarised every plan line the run had emitted;
#   * that includes the plans that fired BEFORE any geometry reached the
#     renderer (live 0.0MB, 0 cache entries);
#   * the arms emitted 20 to 77 plans each, so the share of those empty
#     plans differed per arm, and the "median live memory" was partly a
#     measure of WHEN a run happened to settle rather than of WHAT it
#     settled at.
#
# So: settle on a CONDITION, never on a clock. A run here ends when the
# plan has stopped changing anything -- FC_CONV_PLANS consecutive plans
# with refine/demote/downgrade all zero and the live meter steady to
# within FC_CONV_TOL percent -- and reports the state it converged to.
# A run that does not converge says so and is NOT a measurement.
#
# /!\ THE INSTRUMENT IS VALIDATED BEFORE ITS VERDICT IS READ. Three
# gates, each of which has already produced a plausible-looking lie in
# this workstream:
#
#   1. the scene must have REACHED the renderer -- a plan over 0 cache
#      entries is not a small scene, it is no scene;
#   2. live must be > 0 while entries > 0 -- live counts meshes
#      referenced by the last two frames, so 0.0MB against a populated
#      cache means the view is not drawing and every memory number in
#      the row is about a frame nobody rendered;
#   3. the camera must NOT MOVE after the initial fit -- the plan fires
#      ~300ms after the camera settles somewhere new, so a harness that
#      keeps re-fitting keeps restarting the very process it is timing;
#   4. the refine tolerance must be FINITE. It is printed as `infpx` on
#      this model, and an infinite tolerance is not a large one -- it
#      means `levelError * diagPx > tolerancePx` is false for every
#      source, so the climb is switched off entirely and the ladder only
#      ever descends. A run that "settles" under it has settled because
#      nothing can move, which is not the same as settling.
#
# Reads the plan's own readout out of the log file, so Render_LevelDebug
# must be on and --log-file must be given.
#
#   export LD_LIBRARY_PATH=~/opt/virtualgl/usr/lib:$LD_LIBRARY_PATH
#   cd ~/works/sw/fcad && env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb \
#     xvfb-run -a --server-args='-screen 0 1920x1200x24' \
#     ~/opt/virtualgl/opt/VirtualGL/bin/vglrun -d egl0 \
#     .conda/run.sh build/conda-relwithdebinfo-801/bin/FreeCAD \
#     --log-file ~/level.log scripts/level_converge.py
#
# /!\ Verify the NVIDIA string in the log every run; the llvmpipe
# fallback is silent.
#
# FC_MODEL, FC_GPU_BUDGET_MB, FC_SIMPLIFY (on|off), FC_MERGE (0|1),
# FC_CONV_PLANS, FC_CONV_TOL, FC_MAX_WAIT, FC_OUT, FC_LOG,
# FC_MESH_SKIP/FC_MESH_FINER (on|off), FC_OCCLUSION (on|off),
# FC_FEED (on|off; off pins the demote streak to 0 so culling runs
# with the downgrade feed alone disarmed), FC_CAMERA (fit|inside).
import os
import re
import sys
import time

import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore

MODEL = os.path.expanduser(
    os.environ.get("FC_MODEL", "~/works/sw/models/server_imported.FCStd"))
LOG = os.path.expanduser(os.environ.get("FC_LOG", "~/level.log"))
OUT = os.path.expanduser(os.environ.get("FC_OUT", "~/level_converge.txt"))
BUDGET = int(os.environ.get("FC_GPU_BUDGET_MB", "64"))
SIMPLIFY = os.environ.get("FC_SIMPLIFY", "on") == "on"
MERGE = os.environ.get("FC_MERGE", "0") == "1"
# The release half of the pressure controller (sec 13c.3). 0 reproduces
# the old immediate snap, which is the arm this harness was written to
# convict; the empty string leaves the parameter's own default alone.
RELEASE = os.environ.get("FC_RELEASE", "")
# Skipping the tessellation call that would rebuild nothing (sec 13e).
#
# /!\ "off" is the ONLY arm that can measure safety, and the asymmetry
# is structural: a call that is SKIPPED is never made, so nothing can
# say whether it would have rebuilt. With the feature on, the WRONG
# column counts only calls the check REFUSED, which makes a zero there
# guaranteed rather than earned. Read WRONG from the off arm or not at
# all.
MESH_SKIP = os.environ.get("FC_MESH_SKIP", "on")
# Whether a resident mesh FINER than the ask counts as adequate. This is
# the arm that must be judged by CONVERGED MEMORY rather than by how
# many calls it skipped: it declines a coarsening, and a coarsening that
# would have worked is memory the plan has to find elsewhere.
MESH_FINER = os.environ.get("FC_MESH_FINER", "off")
# The occlusion -> downgrade feed, measured LIVE (SceneStreaming 12.20):
# the software oracle's settled hidden verdicts widen the GPU downgrade
# sweep's free tier. FC_OCCLUSION arms the culling itself; FC_FEED arms
# the feed alone -- a demote streak of 0 leaves culling on and the feed
# off, which is the equal-everything-else arm this measurement needs.
# The third arm (occlusion off entirely) is the baseline that also
# prices the lastUsed accident: culled draws stop advancing lastUsed,
# so with shadows off their GPU buffers are collected after two frames
# whether or not any plan asked -- the on/off delta in uploaded at
# equal budget is that accident's size.
OCCLUSION = os.environ.get("FC_OCCLUSION", "off") == "on"
FEED = os.environ.get("FC_FEED", "on") == "on"
# The scene shadow map draws CASTERS, which frustum culling exempts by
# design -- on the inside camera that is half the scene. Diagnosing the
# 46<->122MB live bimodality needs this switchable (and always written:
# it defaults ON and had never been pinned by this harness before).
SHADOW = os.environ.get("FC_SHADOW", "on") == "on"
# The downgrade sweep's in-flight-credit ledger (sec 13c.4). The off
# arm restores the storming behaviour the ledger exists to kill, which
# is the A/B this harness measures.
LEDGER = os.environ.get("FC_LEDGER", "on") == "on"
# The hard-ceiling climb admission (sec 13c.5): at or over the budget
# no refine is admitted and in-flight climbs are aborted; under it,
# batched admission. Off restores unadmitted climbing.
CLIMB = os.environ.get("FC_CLIMB", "on") == "on"
# fit: the whole-assembly camera every converge run so far has used.
# inside: a perspective camera at the model centre (FarFieldProxies
# 10.3) -- the camera the occlusion mechanism is planned against, where
# a third of the instances are occluded rather than 99.9%.
CAMERA = os.environ.get("FC_CAMERA", "fit")
# How many consecutive quiet plans mean "settled". Two is not enough:
# the descent alternates passes, so a single quiet plan happens mid-run.
CONV_PLANS = int(os.environ.get("FC_CONV_PLANS", "4"))
CONV_TOL = float(os.environ.get("FC_CONV_TOL", "5"))     # percent
MAX_WAIT = float(os.environ.get("FC_MAX_WAIT", "600"))   # seconds
# How long a quiet ladder may say nothing at all before that
# silence is itself the answer. Generous: plans are ~15s apart
# even while the scene is still moving on the rack model.
CONV_QUIET_S = float(os.environ.get("FC_CONV_QUIET_S", "150"))

LINES = []

PLAN = re.compile(
    r"live ([0-9.]+)MB \(uploaded ([0-9.]+)MB, [0-9.]+MB stale in \d+ of "
    r"(\d+) entries\) \| cpu resident ([0-9.]+)MB \| displayed coarse "
    r"(\d+) exact (\d+) \| plan: refine (\d+) demote (\d+) downgrade (\d+)"
    r".*?refine tolerance ([0-9.]+|inf|nan)px( \(RAISED BY PRESSURE\))?")

# The downgrade sweep's own readout, where the feed's verdicts land:
# `occluded` is PlanDemoteStats::occludedFree, sources granted the free
# tier because the oracle proved them hidden for the whole streak.
DGPASS = re.compile(
    r"downgrade pass: considered (\d+).*?\| occluded (\d+) \|"
    r".*?want ([0-9.]+)MB freed ([0-9.]+)MB")


class Plan(object):
    def __init__(self, m):
        (self.live, self.uploaded, self.entries, self.cpu, self.coarse,
         self.exact, self.refine, self.demote, self.downgrade,
         self.tol) = (float(m.group(1)), float(m.group(2)), int(m.group(3)),
                      float(m.group(4)), int(m.group(5)), int(m.group(6)),
                      int(m.group(7)), int(m.group(8)), int(m.group(9)),
                      float(m.group(10)))
        self.pressure = bool(m.group(11))

    @property
    def quiet(self):
        return self.refine == 0 and self.demote == 0 and self.downgrade == 0

    def __str__(self):
        return ("live %.1f cpu %.1f entries %d coarse %d exact %d "
                "refine %d demote %d downgrade %d tol %.2f%s"
                % (self.live, self.cpu, self.entries, self.coarse, self.exact,
                   self.refine, self.demote, self.downgrade, self.tol,
                   " PRESSURE" if self.pressure else ""))


def emit(msg):
    LINES.append(msg)
    sys.stdout.write("CONV %s\n" % msg)
    sys.stdout.flush()


def tail(marker, since):
    """New log lines carrying `marker`, read BY BYTE OFFSET so a slice
    can never re-report the previous slice's plans -- the same rule
    cull_audit.py's tail_lines documents."""
    try:
        with open(LOG, errors="replace") as f:
            f.seek(since)
            body = f.read()
            return ([ln for ln in body.splitlines() if marker in ln],
                    since + len(body.encode("utf-8", "replace")))
    except Exception as exc:
        return (["(log unreadable: %s)" % exc], since)


def settled(window):
    """Whether these plans say the ladder has stopped moving."""
    if len(window) < CONV_PLANS:
        return False
    if not all(p.quiet for p in window):
        return False
    live = [p.live for p in window]
    lo, hi = min(live), max(live)
    if hi <= 0.0:
        return False                       # gate 2: nothing being drawn
    return (hi - lo) <= (CONV_TOL / 100.0) * hi


def settled_by_silence(window, quiet_for):
    """The other way a ladder says it has stopped: it stops SPEAKING.

    A plan fires on an event -- a camera settle, or the scene feed
    changing under a still camera. So a ladder that has genuinely
    settled emits FEWER plans, and eventually none at all, which is the
    one outcome the count-four-quiet-plans rule cannot observe: the
    better the fix, the less evidence it produces. A 420s run was
    already reported as "not converged" for exactly this reason.

    Silence only counts when what it follows is a quiet plan over a
    drawn scene, and it is reported as its own verdict, never merged
    with the other one -- an instrument must say which rule fired.
    """
    if not window or quiet_for < CONV_QUIET_S:
        return False
    last = window[-1]
    return last.quiet and last.entries > 0 and last.live > 0.0


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
        rp.SetBool("SimplifyExhausted", SIMPLIFY)
        rp.SetBool("SimplifyMergeParts", MERGE)
        # /!\ ALWAYS WRITTEN, never left to whatever the last run stored.
        # These live in the user parameter file, so an arm that omits one
        # INHERITS the previous arm's setting and reports it as
        # "default" -- which is how a strict-check audit here silently
        # became an accept-finer audit, with a WRONG count 753x the
        # strict arm's and no line saying the arm had changed.
        rp.SetBool("MeshSkipRedundant", MESH_SKIP != "off")
        rp.SetBool("MeshSkipFinerResident", MESH_FINER == "on")
        rp.SetBool("Occlusion", OCCLUSION)
        rp.SetBool("OcclusionSoftware", True)
        rp.SetInt("OcclusionDemoteStreak", 8 if FEED else 0)
        rp.SetBool("Shadow", SHADOW)
        rp.SetBool("DowngradeLedger", LEDGER)
        rp.SetBool("ClimbHardLimit", CLIMB)
        emit("arm: simplify=%s merge=%s budget=%dMB release=%s meshskip=%s "
             "finer=%s occlusion=%s feed=%s camera=%s shadow=%s ledger=%s "
             "climb=%s conv=%d plans within %.0f%% (or %.0fs of silence)"
             % (SIMPLIFY, MERGE, BUDGET, RELEASE or "default",
                "off" if MESH_SKIP == "off" else "on",
                "on" if MESH_FINER == "on" else "off",
                "on" if OCCLUSION else "off",
                "on" if FEED else "off", CAMERA,
                "on" if SHADOW else "off",
                "on" if LEDGER else "off",
                "on" if CLIMB else "off",
                CONV_PLANS, CONV_TOL, CONV_QUIET_S))

        Gui.getMainWindow().resize(1920, 1200)
        QtCore.QCoreApplication.processEvents()
        doc = App.openDocument(MODEL)
        App.setActiveDocument(doc.Name)
        v = Gui.ActiveDocument.ActiveView
        emit("opened %s: %d objects" % (MODEL, len(doc.Objects)))

        # /!\ Only params the render BRIDGE reads have view-property
        # overrides. Render_SimplifyExhausted and friends are read by
        # PartGui straight off RenderParams -- asking for them here
        # raises AttributeError and takes the rest of this block with
        # it, which is how an earlier harness died silently.
        v.Render_GpuMemoryBudgetMB = BUDGET
        v.Render_LevelDebug = True
        if RELEASE != "":
            v.Render_LevelPressureRelease = float(RELEASE)
        # The cull hidden-share line ("render culling: instances hidden
        # ...") is gated behind the per-view timing switch, and the
        # saved document pins it false -- the 2026-08-13 A/B/C arms ran
        # without it and could not prove the culling did anything at
        # all. Occlusion arms only: the switch is itself an
        # intervention, so the baseline arm stays clean.
        if OCCLUSION:
            v.RenderDebug_Timing = True

        # Gate 3: the camera is placed ONCE and never touched again.
        v.viewIsometric()
        Gui.SendMsgToActiveView("ViewFit")
        QtCore.QCoreApplication.processEvents()
        if CAMERA == "inside":
            # A perspective camera at the model centre, the cull_probe
            # recipe verbatim (FarFieldProxies 10.3). The centre comes
            # from the FITTED camera, never from o.Shape --
            # DeferShapeLoad is on, and touching a shape would
            # force-load every B-Rep in the document. An orthographic
            # zoom does NOT get inside: it shrinks the view height
            # without moving the camera.
            import pivy.coin as coin
            end = time.time() + 2
            while time.time() < end:
                try:
                    v.redraw()
                except Exception:
                    pass
                QtCore.QCoreApplication.processEvents()
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
            emit("inside camera at (%.1f %.1f %.1f), model span %.1f"
                 % (centre[0], centre[1], centre[2], span))

        since = os.path.getsize(LOG) if os.path.exists(LOG) else 0
        window, total, start = [], 0, time.time()
        sawScene = False
        lastPlanAt = time.time()
        bySilence = False
        # Whole-run work totals -- what converging COST, beside what it
        # converged TO. The feed's verdicts land in dgOccluded.
        churn = [0, 0, 0]
        dgPasses, dgOccluded, dgFreed = 0, 0, 0.0
        while time.time() - start < MAX_WAIT:
            deadline = time.time() + 0.5
            while time.time() < deadline:
                try:
                    v.redraw()
                except Exception:
                    pass
                QtCore.QCoreApplication.processEvents()
            fresh, since = tail("render levels:", since)
            for ln in fresh:
                dg = DGPASS.search(ln)
                if dg:
                    dgPasses += 1
                    dgOccluded += int(dg.group(2))
                    dgFreed += float(dg.group(4))
                    continue
                m = PLAN.search(ln)
                if not m:
                    continue
                p = Plan(m)
                total += 1
                # Gate 1: a plan over an empty cache is not a small
                # scene, it is no scene. It is exactly what polluted the
                # medians of the first attempt, so it is dropped here
                # rather than averaged in.
                if p.entries == 0:
                    continue
                sawScene = True
                lastPlanAt = time.time()
                churn[0] += p.refine
                churn[1] += p.demote
                churn[2] += p.downgrade
                window.append(p)
                window[:] = window[-CONV_PLANS:]
            if settled(window):
                break
            if settled_by_silence(window, time.time() - lastPlanAt):
                bySilence = True
                break

        elapsed = time.time() - start
        if bySilence:
            # Report only the trailing quiet run, never a median over a
            # window whose earlier plans were still moving the ladder.
            trailing = []
            for p in reversed(window):
                if not p.quiet:
                    break
                trailing.insert(0, p)
            window = trailing
        emit("plans seen %d, %d with a scene, %.0fs elapsed"
             % (total, len(window) and total or 0, elapsed))
        if not sawScene:
            emit("INVALID: no plan ever ran over a populated cache -- the "
                 "scene never reached the renderer")
        elif not settled(window) and not bySilence:
            emit("NOT CONVERGED in %.0fs -- this row is NOT a measurement" % elapsed)
            for p in window:
                emit("  tail: %s" % p)
        else:
            infinite = [p for p in window if p.tol != p.tol
                        or p.tol == float("inf")]
            if infinite:
                emit("INVALID: refine tolerance is %s -- the climb is "
                     "switched off, so this settled because nothing could "
                     "move, not because it converged" % infinite[-1].tol)
            live = sorted(p.live for p in window)
            upl = sorted(p.uploaded for p in window)
            cpu = sorted(p.cpu for p in window)
            last = window[-1]
            emit("CONVERGED after %.0fs over %d quiet plans%s"
                 % (elapsed, len(window),
                    " (BY SILENCE: no further plan for %.0fs -- a settled "
                    "ladder stops firing, so this is the rule that sees it)"
                    % (time.time() - lastPlanAt) if bySilence else ""))
            emit("  live gpu   %.1f MB  (min %.1f max %.1f)"
                 % (live[len(live) // 2], live[0], live[-1]))
            # Uploaded beside live, because their GAP is a finding: a
            # culled draw stops advancing lastUsed, so with shadows off
            # its buffers are collected two frames later whether or not
            # any plan asked -- the unmanaged accident the occlusion
            # arms exist to price (uploaded falling under the same live
            # is memory the collector took, not the plan).
            emit("  uploaded   %.1f MB" % upl[len(upl) // 2])
            emit("  cpu resident %.1f MB" % cpu[len(cpu) // 2])
            emit("  displayed  coarse %d exact %d" % (last.coarse, last.exact))
            emit("  tolerance  %.2f px%s"
                 % (last.tol, " (RAISED BY PRESSURE)" if last.pressure else ""))
            # Three outcomes, not two. "Inside the budget while holding
            # error back" is the EQUILIBRIUM of sec 13c.3, not a failure:
            # the ladder is fitting, and the raised tolerance is what
            # fitting costs on this scene. Reporting it as "NOT met"
            # (the old rule: live <= budget AND no pressure) would call
            # the intended steady state a defeat, and would have scored
            # the converging arm below the oscillating one.
            emit("  budget     %s (%d MB pinned)"
                 % ("MET at the camera's own tolerance"
                    if last.live <= BUDGET and not last.pressure
                    else "MET, holding %.2fpx of error" % last.tol
                    if last.live <= BUDGET
                    else "NOT met", BUDGET))
        # Whole-run counters, which are not plan state.
        emit("  churn: refine %d demote %d downgrade %d over %d plans"
             % (churn[0], churn[1], churn[2], total))
        emit("  downgrade passes %d, occluded-free verdicts %d, "
             "freed %.1fMB total" % (dgPasses, dgOccluded, dgFreed))
        try:
            body = open(LOG, errors="replace").read()
            emit("  boxes %d, decimated rungs %d"
                 % (body.count("bounding-box stand-in"),
                    body.count("decimated rung")))
        except Exception:
            pass
        emit("DONE")
    except Exception as exc:
        import traceback
        emit("FAILED %s" % exc)
        emit(traceback.format_exc())
    finally:
        with open(OUT, "w") as fp:
            fp.write("\n".join(LINES) + "\n")
        Gui.getMainWindow().close()


QtCore.QTimer.singleShot(500, run)
