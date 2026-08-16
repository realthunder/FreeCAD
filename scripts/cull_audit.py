# What did occlusion culling actually delete? (docs/FarFieldProxies.md §12.9)
#
# Every earlier measurement of the culling compared two PICTURES and reported
# how many pixels differ -- which says something is wrong without saying what,
# and cannot separate "culled less" from "got luckier". This drives the cull
# audit instead: the scene is re-rasterized with the cull mask ignored and each
# draw writing its own identity, so the ids owning a pixel are an exact answer
# to which draws reach the screen, and their intersection with the mask is a
# list of proven over-culls -- each a named draw with a pixel count.
#
# /!\ THE INSTRUMENT IS VALIDATED BEFORE ITS VERDICT IS READ. An audit that
# reports "0 over-culls" is also exactly what a BROKEN id pass reports, and
# that failure mode has already cost this workstream two sessions (a box test
# that answered "hidden" for everything looked like a spectacular result). So:
#
#   1. culling OFF must report over-cull 0 -- nothing is masked, so anything
#      else means the mask snapshot or the id decode is wrong;
#   2. covered pixels must be a large fraction of the viewport in every row,
#      or the id image is empty and no row below it means anything;
#   3. the acid test: the audit is run at the same visibleTtl values whose
#      PIXEL differences are already known (§12.7 measured 0 px at ttl 10^6,
#      1666 at 60, 13714 at 6). The audit must move the same way. If it says
#      "clean" where the picture says 13714 pixels differ, the audit is lying
#      and nothing it reports is usable.
#
# Run on the real GPU with the monitor OFF (gpu-tests-monitor-off recipe):
#
#   export LD_LIBRARY_PATH=~/opt/virtualgl/usr/lib:$LD_LIBRARY_PATH
#   cd ~/works/sw/fcad && env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb \
#     xvfb-run -a --server-args='-screen 0 1920x1200x24' \
#     ~/opt/virtualgl/opt/VirtualGL/bin/vglrun -d egl0 \
#     .conda/run.sh build/conda-relwithdebinfo-801/bin/FreeCAD \
#     --log-file ~/cull_audit.log scripts/cull_audit.py
#
# FC_MODEL selects the document (default ~/works/sw/models/server_imported.FCStd,
# 5455 objects -- it is not in this repository); FC_ROWS the settings to
# measure, FC_SETTLE the seconds per row, FC_OUT/FC_LOG/FC_SHOTS where the
# results go. A row is `<ttl>/<confirm>` for the hardware oracle or
# `sw/<divisor>/<tris>/<threads>/<simd>/<coarse>/<level>/<bias>/<perinst>`
# for the software one (#12.12, coarse hulls #12.16, per-instance #12.17).
#
# /!\ GIVE EVERY FIELD OF A SOFTWARE ROW EXPLICITLY. The rows set view
# properties and nothing resets them, so an omitted field silently
# inherits the previous row's value -- `sw//0` after `sw///1` is one
# thread AND no triangles, which is not a row anybody asked for.
#
# /!\ A COARSE ROW IS NOT READABLE UNTIL ITS HULLS ARE BUILT. They are
# built a few per frame and cached, so the first frames of such a row
# rasterize the meshes the earlier rows did. The row prints `hulls` with
# a pending count beside it: pending must be 0, or the row is measuring
# the warm-up and not the mechanism.
#
# /!\ TIMINGS ARE READ FROM THE SPREAD, NEVER FROM THE LAST LINE. Two runs
# of an identical configuration reported raster 9.00ms and 4.31ms; a 5%
# effect was once published off single samples of that quantity
# (docs/FarFieldProxies.md #12.14, corrected in #12.15). Every timing
# field is reported min/med/max over the window, like the over-cull
# pixels above it.
import re
import os
import time
import traceback

import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore

MODEL = os.path.expanduser(
    os.environ.get("FC_MODEL", "~/works/sw/models/server_imported.FCStd"))
OUT = os.path.expanduser(os.environ.get("FC_OUT", "~/cull_audit.txt"))
LOG = os.path.expanduser(os.environ.get("FC_LOG", "~/cull_audit.log"))
DIR = os.path.expanduser(os.environ.get("FC_SHOTS", "~/cullshots"))

lines = []


def emit(s):
    lines.append(s)
    App.Console.PrintMessage("CULLAUDIT %s\n" % s)
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")


# /!\ THE SLEEP IS INSIDE THE MEASURED FRAME. bgfx times a frame from
# one bgfx::frame() to the next, so everything this loop does between
# two redraws -- the sleep, processEvents(), the Python itself -- lands
# in the frame line's `outside` term, which is exactly the term this
# workstream set out to explain. At 5 ms against a 33 ms frame that is
# 15% of the frame belonging to the harness, not the renderer.
# Set FC_SPIN_SLEEP=0 for any row whose `outside` is being read.
SPIN_SLEEP = float(os.environ.get("FC_SPIN_SLEEP", "0.005"))


def spin(seconds, v=None):
    end = time.time() + seconds
    while time.time() < end:
        if v is not None:
            try:
                v.redraw()
            except Exception:
                pass
        QtCore.QCoreApplication.processEvents()
        if SPIN_SLEEP > 0:
            time.sleep(SPIN_SLEEP)


def compare(a, b):
    """Differing pixels between two PNGs, and the worst channel delta."""
    from PySide6.QtGui import QImage

    ia, ib = QImage(a), QImage(b)
    if ia.isNull() or ib.isNull() or ia.size() != ib.size():
        return None
    import numpy as np

    ia = ia.convertToFormat(QImage.Format_RGB888)
    ib = ib.convertToFormat(QImage.Format_RGB888)
    w, h = ia.width(), ia.height()

    def arr(img):
        # /!\ PySide6's constBits() is a memoryview with no asstring();
        # bytesPerLine may pad the rows.
        bpl = img.bytesPerLine()
        raw = np.frombuffer(bytes(img.constBits()), dtype=np.uint8)[: bpl * h]
        return raw.reshape(h, bpl)[:, : w * 3].reshape(h, w, 3).astype(np.int16)

    d = np.abs(arr(ia) - arr(ib)).max(axis=2)
    return (int((d > 0).sum()), int((d > 64).sum()), w * h)


def subwindow():
    from PySide6.QtWidgets import QMdiSubWindow
    from PySide6.QtOpenGLWidgets import QOpenGLWidget

    gl = Gui.getMainWindow().findChildren(QOpenGLWidget)
    w = gl[0] if gl else None
    while w is not None and not isinstance(w, QMdiSubWindow):
        w = w.parentWidget()
    return w


def tail_lines(marker, since):
    """Log lines carrying `marker` that were written after byte offset
    `since`, plus the new offset. Reading by offset rather than taking the
    last match is what keeps a row from quoting the PREVIOUS row's report
    when its own never arrived -- a silent way to attribute one setting's
    numbers to another."""
    try:
        with open(LOG, errors="replace") as f:
            f.seek(since)
            body = f.read()
            return ([ln.strip() for ln in body.splitlines() if marker in ln],
                    since + len(body.encode("utf-8", "replace")))
    except Exception as exc:
        return (["(log unreadable: %s)" % exc], since)


def log_size():
    try:
        return os.path.getsize(LOG)
    except Exception:
        return 0


def run():
    try:
        os.makedirs(DIR, exist_ok=True)
        App.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
            "AutoSaveEnabled", False)
        App.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        App.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        App.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)
        # FC_VIEW_SCALE: linear scale of the 3D viewport, applied to the
        # maximized size below. 0.5 = a quarter of the pixels with the
        # same triangles -- the ablation that separates a fill-bound
        # frame from a geometry-bound one.
        # /!\ Read the ACTUAL viewport off the `render frame:` line, never
        # this value -- a run that asked for one size and drew at another
        # is the 400x300-reporting-1920x1200 trap, and it has already
        # happened once here.
        win_scale = float(os.environ.get("FC_VIEW_SCALE", "1"))
        Gui.getMainWindow().resize(1920, 1200)
        QtCore.QCoreApplication.processEvents()

        doc = App.openDocument(MODEL)
        App.setActiveDocument(doc.Name)
        v = Gui.ActiveDocument.ActiveView
        emit("model %s (%d objects)" % (MODEL, len(doc.Objects)))

        sub = subwindow()
        if sub:
            sub.showMaximized()
            QtCore.QCoreApplication.processEvents()
            if win_scale != 1.0:
                # /!\ Size the MDI SUBWINDOW, not the main window: the
                # main window is maximized by the window manager and
                # quietly ignores resize(). One arm of the fill ablation
                # was run that way and came back at the full 1791x880,
                # reading as "shrinking the viewport does nothing".
                # /!\ Scale from the MAXIMIZED size so the aspect ratio
                # is preserved exactly -- ViewFit reframes on aspect, and
                # an arm that reframes is not drawing the same triangles.
                full_w, full_h = sub.width(), sub.height()
                sub.showNormal()
                QtCore.QCoreApplication.processEvents()
                sub.resize(int(full_w * win_scale), int(full_h * win_scale))
        QtCore.QCoreApplication.processEvents()
        v.viewIsometric()
        Gui.SendMsgToActiveView("ViewFit")
        converge = float(os.environ.get("FC_CONVERGE", "150"))
        spin(converge, v)
        # /!\ FIT AGAIN. The first fit runs while the geometry is still
        # arriving, so it frames whatever had loaded -- one smoke run fitted
        # a partial model and left the finished one covering 3% of the
        # viewport, which silently divides every coverage number here by
        # thirty. Same family as the 400x300-reporting-1920x1200 trap.
        Gui.SendMsgToActiveView("ViewFit")
        spin(converge * 0.25, v)
        emit("re-fitted after convergence; camera fixed from here on")

        settle = float(os.environ.get("FC_SETTLE", "30"))
        # Every knob below is a GLOBAL RenderParams parameter
        # (2026-08-14); the per-view Render_*/RenderDebug_* copies were
        # retired because saved overrides shadowed the globals.
        rp = App.ParamGet("User parameter:BaseApp/Preferences/View/Render")
        # /!\ FC_NO_AUDIT=1 measures the frame WITHOUT the instrument.
        # The audit re-renders every scene draw into the id image
        # (ViewDebugScene), which on the rack model is ~12.7ms of CPU
        # and ~12.9ms of GPU -- about half the frame. Any timing quoted
        # as "what a frame costs" must be taken with this OFF, or it is
        # a measurement of the measuring apparatus. The cull numbers
        # still need it ON, so the two cannot come from one row.
        # /!\ Read the polarity twice: unset gives "", which IS in the
        # tuple, so the audit defaults ON. It read as "off by default"
        # for a while and every frame timing this workstream quoted was
        # inflated ~3x on submit as a result.
        audit = os.environ.get("FC_NO_AUDIT", "") in ("", "0")
        rp.SetBool("DebugCullAudit", audit)
        # Both arms announce themselves. Only the OFF arm used to, so a
        # run whose timings were inflated by the instrument said nothing
        # at all -- and silence reads as "clean" to whoever greps the log
        # a week later. A default that changes the numbers must be as
        # loud as the flag that overrides it.
        if audit:
            emit("cull audit ON (default; FC_NO_AUDIT=1 turns it off) -- "
                 "cull/over-cull numbers are valid, but the id pass adds "
                 "the whole scene draw list to every frame, so FRAME "
                 "TIMINGS IN THIS RUN ARE NOT A REAL FRAME")
        # Announced in both arms, like the audit above and for the same
        # reason: it is part of the frame being timed, so a row that
        # carried it must say so where the numbers are read.
        emit("spin sleep %.4fs per redraw (FC_SPIN_SLEEP)%s"
             % (SPIN_SLEEP,
                " -- THIS IS INSIDE THE FRAME's `outside` TERM" if SPIN_SLEEP
                else " -- outside is free of the harness's own sleep"))
        if not audit:
            emit("cull audit OFF -- frame timings are clean, cull/over-cull "
                 "numbers are NOT available in this run")
        # The culler's own account of the same frames, on the same cadence.
        # Without it, "0 masked rows" cannot be told apart from "the mask
        # snapshot is broken" -- and one of those is a clean bill of health
        # while the other invalidates the entire run.
        # FC_PROXYCUT=1: what a far-field cut would cost this camera,
        # generating nothing (docs/FarFieldProxies.md 11.1). Off by
        # default because it builds a partition over every drawn
        # instance on the frames it reports, so its rows are diagnostic
        # rows, not timing rows.
        proxycut = os.environ.get("FC_PROXYCUT", "") not in ("", "0")
        if proxycut:
            rp.SetBool("DebugProxyCut", True)
            emit("proxy-cut diagnostic ON -- it partitions every drawn "
                 "instance on the frames it reports, so FRAME TIMINGS IN "
                 "THIS RUN ARE NOT CLEAN")

        # FC_GPU_BUDGET_MB / FC_LEVEL_CEILING_MB: simulate memory
        # pressure (docs/SceneStreaming.md #13). The premise of the
        # coarse-first ladder is a model that does not fit, and on a box
        # with memory to spare neither half of it ever descends -- on
        # desktop OpenGL the automatic GPU budget is 0 outright, because
        # bgfx's GL renderer reports no limit.
        budget_mb = int(os.environ.get("FC_GPU_BUDGET_MB", "0"))
        ceiling_mb = int(os.environ.get("FC_LEVEL_CEILING_MB", "0"))
        # The global parameter is the only knob now: per-view Render_*
        # copies of these were retired (2026-08-14) precisely because a
        # view carrying its own value silently WON over the parameter --
        # a saved 0 once made the renderer report "budget NONE" as if
        # the knob did not exist. Cost one run to find.
        rp.SetBool("LevelDebug", True)
        if budget_mb:
            rp.SetInt("GpuMemoryBudgetMB", budget_mb)
            emit("GPU budget PINNED to %d MB -- simulating a model that "
                 "does not fit; the plan may downgrade displayed meshes"
                 % budget_mb)
        else:
            rp.SetInt("GpuMemoryBudgetMB", 0)
            emit("GPU budget automatic = NONE on OpenGL -- the downgrade "
                 "half of the level plan will not run in this row")
        if ceiling_mb:
            rp.SetInt("LevelCeilingSimulateMB", ceiling_mb)
            emit("CPU memory ceiling SIMULATED at %d MB -- exact "
                 "re-tessellations will be refused" % ceiling_mb)
        else:
            rp.SetInt("LevelCeilingSimulateMB", 0)

        rp.SetBool("DebugTiming", True)
        # FC_TIGHT=1: also ask what a TIGHTER OCCLUDEE VOLUME would have
        # culled (#12.19). Off by default because its per-triangle arm
        # costs far more than a frame -- it runs only on the audit's
        # frame, but a row measured with it on cannot be read for
        # timings. Turn it on for the diagnostic run and off for every
        # row whose clock matters.
        tight = os.environ.get("FC_TIGHT", "") not in ("", "0")
        if tight:
            rp.SetBool("DebugCullBounds", True)
            emit("tight-bound diagnostic ON -- timings in these rows are "
                 "NOT comparable with rows measured without it")

        def audit_row(label):
            """One audit reading, taken only from log written after this
            point."""
            mark = log_size()
            spin(settle, v)
            got, _ = tail_lines("render cull audit:", mark)
            err, _ = tail_lines("id image is empty", mark)
            if err:
                emit("ROW %-22s INSTRUMENT BROKEN: %s" % (label, err[-1]))
                return None
            if not got:
                emit("ROW %-22s no audit line in %.0fs -- the readback never "
                     "landed; this row measures NOTHING" % (label, settle))
                return None
            emit("ROW %-22s %s"
                 % (label, got[-1].split("render cull audit:")[-1].strip()))
            # /!\ AND THE WHOLE WINDOW, not just that last line. The culled
            # frame is an OSCILLATOR (§12.6: two captures of the same static
            # scene 30 s apart differed as much as culling-on differed from
            # culling-off), so one audit sample is one draw from a wide
            # distribution -- which is exactly how the previous session's
            # table came to quote 36629 px for a row that also produces 13.
            # The spread IS the finding; the last sample is not.
            px = [int(m.group(1))
                  for m in (re.search(r"masked rows, (\d+) px", ln)
                            for ln in got) if m]
            if px:
                px.sort()
                emit("    over-cull px across %d samples: min %d med %d max %d"
                     % (len(px), px[0], px[len(px) // 2], px[-1]))
            # Which VERDICT deleted those rows, and what kind of test
            # produced it (§12.10). The audit line says a named draw was
            # wrongly removed; this one says whether the test that removed
            # it was taken while the draw's own geometry was in the depth
            # buffer -- the only observable that separates §12.6's
            # own-contents tie from every other account of the failure.
            attr, _ = tail_lines("render cull attribution:", mark)
            if attr:
                emit("    attribution: %s"
                     % attr[-1].split("render cull attribution:")[-1].strip())
                # Summed over the window, for the same reason. These are
                # RATIOS taken inside each sample, so they are far steadier
                # than the magnitude -- but summing removes the last doubt
                # that one lucky frame carried them.
                #
                # /!\ There is deliberately no "was the node being drawn
                # when it was tested" number here. Only an already-hidden
                # node is re-offered from the hidden set, so the answer that
                # FIRST hides a node is always of the drawn kind; asking
                # would return 100% for every scene, including the ones
                # where the explanation is wrong.
                def total(pat):
                    return sum(int(m.group(1)) for m in
                               (re.search(pat, ln) for ln in attr) if m)
                fresh = total(r"fresh \(<=\d+f\) (\d+) px")
                old_ = total(r"older (\d+) px")
                flip = total(r"flipped >=\d+ times: (\d+) px")
                fr = total(r"not occlusion: \d+ rows (\d+) px")
                tot = fresh + old_
                emit("    attribution over %d samples: fresh verdict %d px "
                     "(%.1f%%), older %d px, from flipping nodes %d px "
                     "(%.1f%%), frustum %d px"
                     % (len(attr), fresh, 100.0 * fresh / tot if tot else 0.0,
                        old_, flip, 100.0 * flip / tot if tot else 0.0, fr))
            cull, _ = tail_lines("render culling:", mark)
            emit("    culler: %s"
                 % (cull[-1].split("render culling:")[-1].strip() if cull
                    else "(no culling line -- the mechanism did not run)"))
            # /!\ AND THE SAME TREATMENT FOR THE CLOCK, for the same reason
            # the over-cull pixels get it. The line above is ONE sample:
            # two runs of an identical configuration reported raster 9.00ms
            # and 4.31ms, a spread wider than any effect that has been
            # measured against it (§12.14 quoted a 5% SIMD saving off two
            # such samples and it was not distinguishable from this). A
            # timing here is a median over the window or it is not a
            # measurement.
            def dist(name, pat):
                vals = [float(m.group(1)) for m in
                        (re.search(pat, ln) for ln in cull) if m]
                if not vals:
                    return None
                vals.sort()
                return "%s %.2f/%.2f/%.2f" % (name, vals[0],
                                              vals[len(vals) // 2], vals[-1])
            parts = [dist("raster", r"raster ([\d.]+)ms"),
                     dist("select", r"select ([\d.]+)"),
                     dist("shard", r"shard ([\d.]+)"),
                     dist("merge", r"shard [\d.]+ merge ([\d.]+)"),
                     dist("wclear", r"worst clear ([\d.]+)"),
                     dist("wraster", r"worst clear [\d.]+ raster ([\d.]+)"),
                     dist("wmerge",
                          r"worst clear [\d.]+ raster [\d.]+ merge ([\d.]+)"),
                     dist("sumraster", r"sum raster ([\d.]+)"),
                     dist("hullbuild", r"MB, ([\d.]+)ms\)"),
                     dist("walk", r"walk ([\d.]+)ms")]
            parts = [p for p in parts if p]
            if parts:
                emit("    timing ms over %d samples, min/med/max: %s"
                     % (len(cull), " | ".join(parts)))
            # What the coarse path was doing while that was measured
            # (#12.16). /!\ `pending` is the reason this is printed at
            # all: hulls are built a few per frame, so a row read while
            # the cache is still filling measures a mixture of the two
            # arms and reads as a weak version of the mechanism. Any
            # non-zero pending here invalidates the row, exactly like a
            # non-zero over-cull invalidates the culling.
            def total(pat, over=cull):
                vals = [int(m.group(1)) for m in
                        (re.search(pat, ln) for ln in over) if m]
                return vals
            # /!\ WHAT THE ROW IS FOR, over the window rather than from the
            # last line. The over-cull pixels and every timing get this
            # treatment already; the hidden counts did not, and they are
            # the benefit half of every comparison made here -- a change
            # is only worth reporting against the spread of the quantity
            # it moved. (§12.16 nearly published a 3% culling gain off
            # one sample of each arm.)
            def spread(name, pat):
                vals = [int(m.group(1)) for m in
                        (re.search(pat, ln) for ln in cull) if m]
                if not vals:
                    return None
                vals.sort()
                return "%s %d/%d/%d" % (name, vals[0], vals[len(vals) // 2],
                                        vals[-1])
            got_parts = [spread("hidden", r"instances hidden (\d+)"),
                         spread("drawn", r"instances hidden \d+ / drawn (\d+)"),
                         spread("nodeshidden", r"nodes visited \d+ hidden (\d+)"),
                         spread("occluders", r"occluders (\d+) of \d+ draws"),
                         # What the per-instance pass added on its own
                         # (#12.17), and what it cost to ask.
                         spread("perinsthid", r"perinst tested \d+ hid (\d+)"),
                         spread("perinsttested", r"perinst tested (\d+)")]
            got_parts = [p for p in got_parts if p]
            if got_parts:
                emit("    culled over %d samples, min/med/max: %s"
                     % (len(cull), " | ".join(got_parts)))
            hulls = total(r"hulls (\d+) of \d+ draws")
            pend = total(r"pending (\d+),")
            held = total(r"held (\d+),")
            saved = total(r"saved (\d+) tris")
            if hulls:
                hulls.sort()
                saved.sort()
                emit("    hulls used per frame min/med/max: %d/%d/%d "
                     "| tris saved med %d | held %d | pending max %d "
                     "(must be 0, else the row is a warm-up)"
                     % (hulls[0], hulls[len(hulls) // 2], hulls[-1],
                        saved[len(saved) // 2] if saved else -1,
                        held[-1] if held else -1,
                        max(pend) if pend else -1))
            # What a TIGHTER OCCLUDEE VOLUME would have culled (#12.19),
            # when FC_TIGHT asked for it. Three arms against one image:
            # the world box that ships, the mesh's own box through its
            # model matrix, and every triangle asked separately (the
            # ceiling -- nothing occludee-side beats asking the geometry).
            #
            # /!\ THE `RISK` COLUMN IS THE GATE, exactly as over-cull px
            # is the gate on the culling itself. It counts rows an arm
            # would have culled that OWN PIXELS, so a non-zero value
            # means the arm is not conservative and its prize is not a
            # prize. Read it before reading anything else on the line.
            tb, _ = tail_lines("render tight-bound audit:", mark)
            if tb:
                emit("    tight bounds: %s"
                     % tb[-1].split("render tight-bound audit:")[-1].strip())

                def arm(name, pat):
                    vals = [int(m.group(1)) for m in
                            (re.search(pat, ln) for ln in tb) if m]
                    if not vals:
                        return None
                    vals.sort()
                    return "%s %d/%d/%d" % (name, vals[0],
                                            vals[len(vals) // 2], vals[-1])
                # Spread for the same reason #12.16 gives: the CPU oracle
                # is deterministic, so these SHOULD be flat -- and a
                # spread here would mean the arms are reading a buffer
                # that is not the image's, which is the one way this
                # diagnostic could quietly measure the wrong frame.
                # /!\ `over control` IS THE ANSWER, not `prize`. The
                # control re-runs the world box that already ships, so a
                # row it also culls was never asked by the pass -- a
                # coverage gap, and nothing to do with how tight the
                # bound is. Reading `prize` instead would publish that
                # gap as a win for whichever arm was being built.
                arms = [arm("ctrlprize",
                            r"control world AABB cull \d+ \((\d+) prize"),
                        arm("obbover",
                            r"OBB corners cull .*?, (\d+) over control"),
                        arm("obbrisk",
                            r"OBB corners cull \d+ \(\d+ prize \+ (\d+) RISK"),
                        arm("primover",
                            r"per-primitive cull .*?, (\d+) over control"),
                        arm("primrisk",
                            r"per-primitive cull \d+ \(\d+ prize \+ (\d+) RISK"),
                        arm("invisible", r"drawn, (\d+) invisible"),
                        # /!\ The ceiling's denominator. A row the arms
                        # were never offered cannot be evidence that
                        # they find nothing -- the first run of this
                        # divided by all 7574 invisible rows while
                        # judging 2803 of them.
                        arm("judgedinvis", r"invisible \((\d+) of them judged")]
                arms = [a for a in arms if a]
                if arms:
                    emit("    tight arms over %d samples, min/med/max: %s"
                         % (len(tb), " | ".join(arms)))
            elif tight:
                emit("    tight bounds: NO LINE -- the diagnostic did not "
                     "run (needs the software pass and the audit both on)")
            return got[-1]

        # 1. Instrument validation: nothing masked, so over-cull must be 0.
        emit("--- validation: culling OFF, nothing may be reported over-culled")
        rp.SetBool("Occlusion", False)
        audit_row("culling off")

        # 2. A picture of the id image itself, for the eye. Saved once; a
        # screen of flat black here means the id pass drew nothing, which is
        # the same disease the guard above catches numerically.
        try:
            rp.SetInt("DebugViewMode", 11)  # InstanceId
            spin(min(settle, 10.0), v)
            shot = os.path.join(DIR, "id_image.png")
            v.saveImage(shot, 1600, 900, "Current")
            emit("id image written to %s" % shot)
            rp.SetInt("DebugViewMode", 0)
            spin(3, v)
        except Exception as exc:
            emit("id image capture failed: %s" % exc)

        # 3. Each setting measured BOTH ways in the same row, same framing,
        # same settle: what the picture says (the old measurement) and what
        # the audit says (the new one). Reported side by side because they
        # are not the same claim -- a draw removed from in front of another
        # draw of similar colour is a proven over-cull that barely moves a
        # pixel -- and the ratio between them is itself a finding.
        #
        # /!\ The (ttl 10^6, confirm 1) row exists to test a specific
        # suspicion: hiddenConfirm needs N CONSECUTIVE hidden answers, and
        # an unreachable visibleTtl means a node is never asked twice, so
        # confirm >= 2 there may make culling structurally impossible.
        # If that is so, the "pixel-exact at ttl 10^6" result is pixel-exact
        # because it culls NOTHING, and the hidden count it was quoted with
        # came from a different configuration.
        emit("--- per setting: picture difference AND audit, same frames")
        #
        # A row of the form "sw" (or "sw/<divisor>", or "sw/<divisor>/<tris>")
        # selects the CPU masked software oracle of §12.12 instead. It reads
        # none of ttl/confirm -- those exist to contain a latency it does not
        # have -- so the two row kinds are not two settings of one mechanism
        # and only the left half of the readout compares between them.
        rows = os.environ.get("FC_ROWS", "1000000/1,1000000/2,60/2,6/2")
        for spec in [r.strip() for r in rows.split(",") if r.strip()]:
            software = spec.split("/")[0] in ("sw", "soft", "software")
            if software:
                parts = spec.split("/")
                div = parts[1] if len(parts) > 1 and parts[1] else ""
                tris = parts[2] if len(parts) > 2 and parts[2] else ""
                thr = parts[3] if len(parts) > 3 and parts[3] else ""
                # The vector pre-pass of §12.14, as an explicit 0/1 arm.
                # It can only discard triangles that cover no pixel, so
                # the two arms are meant to differ in raster ms and in
                # nothing else -- which is exactly why it is worth having
                # both in one run, at one framing, on one camera.
                simd = parts[4] if len(parts) > 4 and parts[4] else ""
                # The coarse occluder hulls of #12.16, as their own arm.
                # A hull may only make the buffer claim LESS than the mesh
                # it stands for, so the pair (coarse 0, coarse 1) at one
                # camera is the whole gate: the over-cull px of the second
                # must be no worse than the first, and 0 in both.
                coarse = parts[5] if len(parts) > 5 and parts[5] else ""
                level = parts[6] if len(parts) > 6 and parts[6] else ""
                bias = parts[7] if len(parts) > 7 and parts[7] else ""
                # Per-instance testing (#12.17): ask the occlusion
                # question of each object rather than of the group it
                # was partitioned into. Same buffer, same frame, finer
                # granularity -- so the over-cull gate applies to it
                # exactly as it does to everything else here.
                perinst = parts[8] if len(parts) > 8 and parts[8] else ""
                label = "software%s%s%s%s%s%s%s%s" % (
                    " div %s" % div if div else "",
                    " tris %s" % tris if tris else "",
                    " thr %s" % thr if thr else "",
                    " simd %s" % simd if simd else "",
                    " coarse %s" % coarse if coarse else "",
                    " lvl %s" % level if level else "",
                    " bias %s" % bias if bias else "",
                    " perinst %s" % perinst if perinst else "")
            else:
                ttl, _, confirm = spec.partition("/")
                label = "ttl %s confirm %s" % (ttl, confirm or "-")
            rp.SetBool("Occlusion", False)
            spin(settle, v)
            a = os.path.join(DIR, "a_%s.png" % spec.replace("/", "_"))
            v.saveImage(a, 1600, 900, "Current")
            rp.SetBool("OcclusionSoftware", bool(software))
            if software:
                if div:
                    rp.SetInt("OcclusionResolution", int(div))
                if tris:
                    rp.SetInt("OcclusionOccluderTris", int(tris))
                if thr:
                    rp.SetInt("OcclusionThreads", int(thr))
                if simd:
                    rp.SetBool("OcclusionSimd", bool(int(simd)))
                if coarse:
                    rp.SetBool("OcclusionCoarse", bool(int(coarse)))
                if level:
                    rp.SetInt("OcclusionCoarseLevel", int(level))
                if bias:
                    rp.SetInt("OcclusionCoarseBias", int(bias))
                if perinst:
                    rp.SetBool("OcclusionPerInstance", bool(int(perinst)))
            else:
                rp.SetInt("OcclusionVisibleTtl", int(ttl))
                if confirm:
                    rp.SetInt("OcclusionConfirm", int(confirm))
            rp.SetBool("Occlusion", True)
            audit_row(label)
            b = os.path.join(DIR, "b_%s.png" % spec.replace("/", "_"))
            v.saveImage(b, 1600, 900, "Current")
            r = compare(a, b)
            if r is None:
                emit("    picture: captures unreadable")
            else:
                diff, big, total = r
                emit("    picture: %d of %d px differ (%.4f%%), %d by >64"
                     % (diff, total, 100.0 * diff / total, big))
        rp.SetBool("Occlusion", False)
        emit("DONE")
    except Exception:
        emit("FAIL")
        emit(traceback.format_exc())
    os._exit(0)


QtCore.QTimer.singleShot(2000, run)
