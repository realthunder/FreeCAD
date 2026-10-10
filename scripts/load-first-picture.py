"""How long after a file is opened is its model on the screen, and what the wait is made of.

Run inside the GUI, one open in a fresh session:

    LOAD_SRC=/path/to/file.FCStd LOAD_OUT=/some/empty/dir FreeCAD scripts/load-first-picture.py

(with a configuration of its own -- FREECAD_USER_HOME, --user-cfg -- so that the result is the
build's and not a setting's). It writes LOAD_OUT/result.txt and ends the session. The file is
copied to LOAD_OUT and the copy is opened.

What it reports, in this order:

1. The clipboard: how long one question to it takes (formats(), and one hasFormat()). Read
   only. Where a session is refused the clipboard a question took 170 ms, and commands that ask
   it in isActive() made every pass over the commands 0.8 s (docs/HandsOnLog.md, entry 47).
2. One pass over every command's isActive(), with no document: the total and the dearest.
3. The open: how long openDocument() takes.
4. The engine's own frame (saveRenderDump) at LOAD_TIMES seconds after the open returned: how
   many pixels the geometry covers and the frame's average colour. A view that shows the
   background and the navigation cube alone covers a few thousand pixels; the times at which
   the count jumps and then stands are when the model is first there and when it is complete.
5. The pass over the commands again, with the document open.

With LOAD_LOG=1 the Gui and Part log is on, and the session's log has the load's own account:
"progressive restore <doc>: N view providers in S slices, T s ..." (Gui::Document's drain),
"progressive load <doc>: N of N visuals in S slices ..." and "pre-mesh <doc>: ..." (Part).
With LOAD_TRACE=<ms> every dispatch of the event loop slower than that is logged with its depth
("slow dispatch: depth D Nms event E to Class"): depth 0 is the event loop's own, deeper is an
event run from inside another -- a load's slice pumping the loop. Both cost time; take the
times of 4 from a run without them.

LOAD_TIMES: comma separated seconds, default 2,4,...,30,40. LOAD_FIT=0: no fit after the open.
"""
import json
import os
import shutil
import tempfile
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

SRC = os.environ.get("LOAD_SRC", "")
OUT = os.environ.get("LOAD_OUT") or os.environ.get("GT_OUT") or tempfile.mkdtemp(prefix="load-")
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
TIMES = [int(t) for t in os.environ.get(
    "LOAD_TIMES", "2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,40").split(",")]
FIT = os.environ.get("LOAD_FIT", "1") == "1"
STATE = {}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def sweep():
    """A modal box raised by the open (the recompute a migration asks for) is answered no."""
    box = QtWidgets.QApplication.activeModalWidget()
    if box is not None:
        note("box answered no: %s" % (box.text()[:80] if isinstance(box, QtWidgets.QMessageBox)
                                      else type(box).__name__))
        box.reject() if hasattr(box, "reject") else box.close()


SWEEPER = QtCore.QTimer()
SWEEPER.timeout.connect(sweep)
SWEEPER.start(400)


def clipboard():
    cb = QtWidgets.QApplication.clipboard()
    for turn in (1, 2):
        t = time.perf_counter()
        mime = cb.mimeData()
        formats = list(mime.formats()) if mime is not None else []
        t1 = time.perf_counter()
        if mime is not None:
            mime.hasFormat("application/x-documentobject")
        t2 = time.perf_counter()
        note("clipboard, turn %d: formats() %.1f ms (%d formats), hasFormat() %.1f ms" % (
            turn, (t1 - t) * 1000, len(formats), (t2 - t1) * 1000))


def commands(tag):
    names = FreeCADGui.Command.listAll()
    took = []
    t_all = time.perf_counter()
    for name in names:
        try:
            cmd = FreeCADGui.Command.get(name)
            t = time.perf_counter()
            cmd.isActive()
            took.append((time.perf_counter() - t, name))
        except Exception as e:
            took.append((0.0, name + " !" + type(e).__name__))
    total = time.perf_counter() - t_all
    took.sort(reverse=True)
    note("commands, %s: isActive() of all %d in %.3f s; the dearest: %s" % (
        tag, len(names), total,
        ", ".join("%s %.1f ms" % (name, t * 1000) for t, name in took[:4])))


def open_it():
    note("build: %s" % " ".join(FreeCAD.Version()[:8]))
    clipboard()
    commands("no document")
    if os.environ.get("LOAD_LOG") == "1":
        FreeCAD.setLogLevel("Gui", "Log")
        FreeCAD.setLogLevel("Part", "Log")
    if os.environ.get("LOAD_TRACE"):
        render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
        render.SetInt("LevelSlowBuildMS", int(os.environ["LOAD_TRACE"]))
        render.SetBool("LevelDebug", True)
        note("trace: dispatches over %s ms" % os.environ["LOAD_TRACE"])
    work = os.path.join(OUT, "copy-" + os.path.basename(SRC))
    shutil.copyfile(SRC, work)
    t = time.perf_counter()
    doc = FreeCAD.openDocument(work)
    STATE["t0"] = time.perf_counter()
    STATE["doc"] = doc.Name
    note("opened in %.1f s, %d objects" % (STATE["t0"] - t, len(doc.Objects)))
    if FIT:
        v = FreeCADGui.getDocument(doc.Name).mdiViewsOfType("Gui::View3DInventor")[0]
        v.setAnimationEnabled(False)
        v.viewIsometric()
        v.fitAll()
    schedule()


def schedule():
    if not TIMES:
        finish()
        return
    due = STATE["t0"] + TIMES.pop(0)
    QtCore.QTimer.singleShot(max(0, int((due - time.perf_counter()) * 1000)), sample)


def sample():
    try:
        at = time.perf_counter() - STATE["t0"]
        v = FreeCADGui.getDocument(STATE["doc"]).mdiViewsOfType("Gui::View3DInventor")[0]
        shot = os.path.join(OUT, "t%05.1f.png" % at)
        v.saveRenderDump(shot)
        with open(shot + ".json", encoding="utf-8") as f:
            stats = json.load(f)["stats"]
        note("%5.1f s: geometry pixels %s of %d; average colour %s" % (
            at, stats.get("geometryPixels"), stats["width"] * stats["height"],
            [round(c) for c in stats.get("avgColor", [])]))
    except Exception:
        note("sample: " + " | ".join(traceback.format_exc().splitlines()))
    schedule()


def finish():
    SWEEPER.stop()
    try:
        commands("the document open")
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
    except Exception:
        note("finish: " + " | ".join(traceback.format_exc().splitlines()))
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


def start():
    try:
        if not os.path.isfile(SRC):
            note("ABORT LOAD_SRC is not a file: %r" % SRC)
            finish()
            return
        open_it()
    except Exception:
        note("ABORT " + " | ".join(traceback.format_exc().splitlines()))
        finish()


QtCore.QTimer.singleShot(1500, start)
