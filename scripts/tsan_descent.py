# A mass-descent CHURN workload for a ThreadSanitizer build.
#
# This is the opposite instrument to level_converge.py: that one holds
# the camera still and waits for the ladder to stop moving, because it
# measures WHERE the plan settles. This one exists to make the ladder
# move as much as possible, because the thing being measured is not the
# plan at all -- it is the RACE WINDOW between the exact-refine worker
# (meshedCopy reading live TShape triangulation handles,
# MeshLevelBuild.cpp) and the GUI thread mutating those same handles
# (transferMeshLevels on a climb, demoteMeshLevels under a CPU ceiling,
# inline BRepMesh in updateVisual). TSan does the observing; this
# script's only job is to keep both hands in the drawer:
#
#   * a pinned GPU budget keeps the downgrade/refine sweeps alive;
#   * camera flips (view flip + a zoom run) keep queueing and
#     cancelling exact refines, so workers are mid-copy often;
#   * Render_LevelCeilingSimulateMB is raised WHILE copies are in
#     flight -- the ceiling observation fires the hidden-rung drop and
#     demote sweeps against shapes a worker may be reading -- and then
#     cleared so the climb (transferMeshLevels) runs against the next
#     wave of copies.
#
# The toggle order matters: a job checks the simulated floor BEFORE it
# starts its copy (MeshLevelSource.cpp refineLoop), so a ceiling that is
# up first just refuses the builds and there is nothing in flight to
# race with. Raise it only after a refine phase has had time to start
# copies.
#
# /!\ Params PERSIST between runs (user parameter file). Everything this
# script sets is set on BOTH arms of its toggles, and the simulated
# ceiling is force-cleared in the finally block -- a leftover ceiling
# would silently poison every later harness run on this box.
#
# /!\ This is a correctness workload, NEVER a benchmark: the TSan build
# is 5-15x slower and the phases are paced by wall clock, not by
# convergence. No number this run prints is a measurement.
#
# Run (real GPU per the standing rule; TSAN_OPTIONS set by the caller):
#   export LD_LIBRARY_PATH=~/opt/virtualgl/usr/lib:$LD_LIBRARY_PATH
#   cd ~/works/sw/fcad && env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb \
#     TSAN_OPTIONS="suppressions=scripts/tsan_suppressions.txt \
#       log_path=/tmp/tsan-descent history_size=7 second_deadlock_stack=1" \
#     xvfb-run -a --server-args='-screen 0 1920x1200x24' \
#     ~/opt/virtualgl/opt/VirtualGL/bin/vglrun -d egl0 \
#     .conda/run.sh build/conda-tsan-801/bin/FreeCAD \
#     --log-file /tmp/tsan-descent-fc.log scripts/tsan_descent.py
#
# FC_MODEL, FC_GPU_BUDGET_MB, FC_CYCLES, FC_PHASE_S, FC_CEILING_MB,
# FC_OUT, FC_LOG.
import os
import re
import sys
import time

import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore

MODEL = os.path.expanduser(
    os.environ.get("FC_MODEL", "~/works/sw/models/server_imported.FCStd"))
LOG = os.path.expanduser(os.environ.get("FC_LOG", "/tmp/tsan-descent-fc.log"))
OUT = os.path.expanduser(os.environ.get("FC_OUT", "/tmp/tsan_descent.txt"))
BUDGET = int(os.environ.get("FC_GPU_BUDGET_MB", "64"))
CYCLES = int(os.environ.get("FC_CYCLES", "6"))
PHASE_S = float(os.environ.get("FC_PHASE_S", "25"))
# High enough that floor > available always holds while raised: the
# refine loop compares against Render::MemoryBudget::availableMemory().
CEILING_MB = int(os.environ.get("FC_CEILING_MB", "1000000"))

LINES = []

PLAN = re.compile(
    r"plan: refine (\d+) demote (\d+) downgrade (\d+)")


def emit(msg):
    LINES.append(msg)
    sys.stdout.write("TSAN-DESCENT %s\n" % msg)
    sys.stdout.flush()


def tail(marker, since):
    """New log lines carrying `marker`, by byte offset (cull_audit rule:
    a slice can never re-report the previous slice)."""
    try:
        with open(LOG, errors="replace") as f:
            f.seek(since)
            body = f.read()
            return ([ln for ln in body.splitlines() if marker in ln],
                    since + len(body.encode("utf-8", "replace")))
    except Exception as exc:
        return (["(log unreadable: %s)" % exc], since)


def pump(seconds, view):
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            view.redraw()
        except Exception:
            pass
        QtCore.QCoreApplication.processEvents()


def run():
    rp = App.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    totals = [0, 0, 0]
    try:
        App.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
            "AutoSaveEnabled", False)
        App.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        App.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)
        rp.SetString("Type", "bgfx - OpenGL")
        rp.SetBool("LevelDebug", True)
        # Always written, never inherited from the previous run's arm
        # (the level_converge.py lesson).
        rp.SetBool("SimplifyExhausted", True)
        rp.SetBool("SimplifyMergeParts", False)
        rp.SetBool("MeshSkipRedundant", True)
        rp.SetBool("MeshSkipFinerResident", False)
        rp.SetInt("LevelCeilingSimulateMB", 0)
        emit("arm: budget=%dMB cycles=%d phase=%.0fs ceiling=%dMB"
             % (BUDGET, CYCLES, PHASE_S, CEILING_MB))

        Gui.getMainWindow().resize(1920, 1200)
        QtCore.QCoreApplication.processEvents()
        doc = App.openDocument(MODEL)
        App.setActiveDocument(doc.Name)
        v = Gui.ActiveDocument.ActiveView
        emit("opened %s: %d objects" % (MODEL, len(doc.Objects)))

        v.Render_GpuMemoryBudgetMB = BUDGET
        v.Render_LevelDebug = True

        v.viewIsometric()
        Gui.SendMsgToActiveView("ViewFit")
        pump(PHASE_S, v)  # initial load + first descent under budget

        since = os.path.getsize(LOG) if os.path.exists(LOG) else 0
        flips = ["viewFront", "viewTop", "viewAxonometric", "viewRear",
                 "viewLeft", "viewIsometric"]
        for cycle in range(CYCLES):
            # Refine phase: fresh framing + a zoom run, so a band of
            # objects crosses the exact-rung threshold and workers start
            # copying.
            getattr(v, flips[cycle % len(flips)])()
            Gui.SendMsgToActiveView("ViewFit")
            pump(1, v)
            for _ in range(6):
                Gui.SendMsgToActiveView("ZoomIn")
                pump(0.3, v)
            pump(PHASE_S, v)

            # Ceiling raised while those copies are in flight: the next
            # observation drops hidden rungs and demotes -- GUI-thread
            # mutation of triangulations a worker may be reading.
            rp.SetInt("LevelCeilingSimulateMB", CEILING_MB)
            for _ in range(6):
                Gui.SendMsgToActiveView("ZoomOut")
                pump(0.3, v)
            pump(PHASE_S, v)

            # Ceiling cleared: the climb re-queues refines and applies
            # transferMeshLevels against the next wave of copies.
            rp.SetInt("LevelCeilingSimulateMB", 0)
            pump(PHASE_S / 2, v)

            fresh, since = tail("render levels: gpu budget", since)
            moved = [0, 0, 0]
            for ln in fresh:
                m = PLAN.search(ln)
                if m:
                    for i in range(3):
                        moved[i] += int(m.group(i + 1))
                        totals[i] += int(m.group(i + 1))
            emit("cycle %d: refine %d demote %d downgrade %d"
                 % (cycle, moved[0], moved[1], moved[2]))

        emit("totals: refine %d demote %d downgrade %d"
             % tuple(totals))
        if sum(totals) == 0:
            emit("INVALID: the ladder never moved -- this run exercised "
                 "nothing and says nothing about the race")
        emit("DONE")
    except Exception as exc:
        import traceback
        emit("FAILED %s" % exc)
        emit(traceback.format_exc())
    finally:
        # A leftover simulated ceiling would poison every later run.
        try:
            rp.SetInt("LevelCeilingSimulateMB", 0)
        except Exception:
            pass
        try:
            with open(OUT, "w") as fp:
                fp.write("\n".join(LINES) + "\n")
        except Exception:
            pass
        Gui.getMainWindow().close()
        # Under TSan the teardown after close() can hang the process
        # (observed: the first run's FreeCAD outlived DONE until
        # killed). The results are already on disk -- OUT is written
        # and TSan flushes each report as it fires -- so a hard exit
        # loses nothing and leaves no orphan behind xvfb.
        QtCore.QTimer.singleShot(30000, lambda: os._exit(0))


QtCore.QTimer.singleShot(500, run)
