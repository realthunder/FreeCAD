# Does the occlusion culling pay for itself at frame level? (12.13)
#
# Drives Render_OcclusionBenefitProbe -- the frame-level A/B inside the
# renderer (CullBenefit.h): stretches of frames with the whole occlusion
# block on and off, median frame cost compared, verdict printed on the
# frame-stats cadence. This script only arranges the conditions and
# reads the answer back out of the log:
#
#   * the scene must be CONVERGED before the probe is trusted -- the
#     ladder moving under one arm and not the other is not a
#     measurement (level_converge.py's lesson);
#   * the camera is fixed per row; two rows, the fitted exterior
#     framing every prior culling number used, then a zoomed framing;
#   * the cull audit stays OFF: it re-renders the scene into an id
#     image (~half the frame on the rack model), and a probe that
#     includes the instrument measures the instrument.
#
# Run (real GPU, monitor-off recipe):
#   export LD_LIBRARY_PATH=~/opt/virtualgl/usr/lib:$LD_LIBRARY_PATH
#   cd ~/works/sw/fcad && env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb \
#     xvfb-run -a --server-args='-screen 0 1920x1200x24' \
#     ~/opt/virtualgl/opt/VirtualGL/bin/vglrun -d egl0 \
#     .conda/run.sh build/conda-relwithdebinfo-801/bin/FreeCAD \
#     --log-file /tmp/cull_benefit.log scripts/cull_benefit_probe.py
#
# FC_MODEL, FC_CONVERGE, FC_ROW_S, FC_OUT, FC_LOG.
import os
import sys
import time

import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore

MODEL = os.path.expanduser(
    os.environ.get("FC_MODEL", "~/works/sw/models/server_imported.FCStd"))
LOG = os.path.expanduser(os.environ.get("FC_LOG", "/tmp/cull_benefit.log"))
OUT = os.path.expanduser(os.environ.get("FC_OUT", "/tmp/cull_benefit.txt"))
CONVERGE = float(os.environ.get("FC_CONVERGE", "150"))
ROW_S = float(os.environ.get("FC_ROW_S", "120"))

LINES = []


def emit(msg):
    LINES.append(msg)
    sys.stdout.write("BENEFIT %s\n" % msg)
    sys.stdout.flush()


def tail(marker, since):
    try:
        with open(LOG, errors="replace") as f:
            f.seek(since)
            body = f.read()
            return ([ln for ln in body.splitlines() if marker in ln],
                    since + len(body.encode("utf-8", "replace")))
    except Exception as exc:
        return (["(log unreadable: %s)" % exc], since)


def spin(seconds, view):
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            view.redraw()
        except Exception:
            pass
        QtCore.QCoreApplication.processEvents()


def run():
    rp = App.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    try:
        App.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
            "AutoSaveEnabled", False)
        App.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        App.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "ShowNaviCube", False)
        rp.SetString("Type", "bgfx - OpenGL")
        # Always written, never inherited (the level_converge lesson).
        rp.SetBool("Occlusion", True)
        rp.SetBool("OcclusionSoftware", True)
        rp.SetBool("OcclusionBenefitProbe", True)
        rp.SetInt("LevelCeilingSimulateMB", 0)
        # The readout cadence: the benefit line prints with the frame
        # stats. Set as the GLOBAL param -- new params have no
        # view-property override, and v.RenderDebug_X on a view that
        # lacks it kills this whole block (the harness trap the
        # decimation rung already paid for).
        rp.SetBool("DebugTiming", True)
        emit("arm: occlusion=sw probe=on converge=%.0fs row=%.0fs"
             % (CONVERGE, ROW_S))

        Gui.getMainWindow().resize(1920, 1200)
        QtCore.QCoreApplication.processEvents()
        doc = App.openDocument(MODEL)
        App.setActiveDocument(doc.Name)
        v = Gui.ActiveDocument.ActiveView
        emit("opened %s: %d objects" % (MODEL, len(doc.Objects)))
        # The document may carry a SAVED per-view RenderDebug_Timing
        # override (this one does: false), which beats the global param
        # and silences the readout on exactly the view that has the
        # scene. Flip it where it exists; a view without the property
        # is governed by the global already set above.
        try:
            v.RenderDebug_Timing = True
            emit("per-view RenderDebug_Timing override flipped on")
        except AttributeError:
            pass

        v.viewIsometric()
        Gui.SendMsgToActiveView("ViewFit")
        spin(CONVERGE, v)
        # Re-fit: the first fit framed a scene still arriving.
        Gui.SendMsgToActiveView("ViewFit")
        spin(CONVERGE * 0.25, v)
        emit("converged; camera fixed per row from here")

        rows = [("exterior-fit", 0), ("zoomed", 8)]
        since = os.path.getsize(LOG) if os.path.exists(LOG) else 0
        for name, zooms in rows:
            for _ in range(zooms):
                Gui.SendMsgToActiveView("ZoomIn")
                spin(0.3, v)
            spin(ROW_S, v)
            fresh, since = tail("render culling benefit", since)
            emit("row %s: %d benefit lines" % (name, len(fresh)))
            for ln in fresh[-4:]:
                emit("  %s" % ln[ln.find("render culling benefit"):].strip())
        emit("DONE")
    except Exception as exc:
        import traceback
        emit("FAILED %s" % exc)
        emit(traceback.format_exc())
    finally:
        # Params persist between runs: leave none of this row's arms
        # behind for a harness that does not write them.
        try:
            rp.SetBool("OcclusionBenefitProbe", False)
            rp.SetBool("Occlusion", False)
            rp.SetBool("DebugTiming", False)
        except Exception:
            pass
        try:
            with open(OUT, "w") as fp:
                fp.write("\n".join(LINES) + "\n")
        except Exception:
            pass
        Gui.getMainWindow().close()
        QtCore.QTimer.singleShot(30000, lambda: os._exit(0))


QtCore.QTimer.singleShot(500, run)
