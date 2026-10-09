"""Time one GUI load of one document: when is it on the screen, and at what cost.

A measurement, not a test: it asserts nothing and ends with DONE. Run it
through scripts/gui-test.sh, one load a process, and never time the first
open of a file (the file cache, and a hardware driver's shaders):

    LT_DOC=~/works/sw/models/MiSTer.FCStd FC_LEVEL_DEBUG=1 \\
        scripts/gui-test.sh scripts/load-timing.py /tmp/lt-1 --timeout 500

    ... --window --gl nvidia    the same in a window on the desktop

What it writes to the result file, every time from the RETURN of the open
and on the steady clock:

  - the GL renderer drawing (read back from a context: say which display a
    number is for), the size of the view and of the main window;
  - the closing lines of the two progressive drains, whole;
  - when each shape drawn as a bounding box got its mesh;
  - the longest stretch the GUI thread stayed away from its event loop, by
    phase (a timer of 10 ms that looks at how late it is);
  - the level-of-detail landing pump's work, with FC_LEVEL_DEBUG=1.

Environment:

  LT_DOC      the document, or a STEP file (imported with ImportGui.open,
              the event loop timed inside the call)
  LT_QUIET    seconds without a landing or a build that end the run (12)
  LT_FIT      1: fit the view when the view providers exist. A document's
              saved camera may show almost nothing of it, and every
              level-of-detail figure is for what the camera sees
  LT_FRAMES   1: the renderer's frame timing (Render/DebugTiming) from the
              start; each frame line of a second or more is written with
              its breakdown
  LT_SPIN     seconds: once settled, roll the camera about its own axis for
              that long with the frame timing on; the median step is what a
              settled frame costs
  LT_LEISURE  0/1: Render/CoarseDeferAtLeisure
  LT_BUDGET   Render/ProgressiveLoadBudgetMS
  LT_WINDOW   WxH: the main window's size
  LT_SHOT     a path: save the view there at the end

docs/DocumentLoad.md sec 18.12 to 18.14 were measured with it.
"""
import os
import re
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

RESULT = os.environ["GT_RESULT"]
DOC = os.path.expanduser(os.environ["LT_DOC"])
QUIET = float(os.environ.get("LT_QUIET", "12"))
RENDER = "User parameter:BaseApp/Preferences/View/Render"
PUMP = re.compile(
    r"landings ([0-9.]+)s in (\d+) \+ hook bodies ([0-9.]+)s in (\d+).*worst turn (\d+)ms")
FRAME = re.compile(r"render frame: frames:(\d+) \S+ frame ([0-9.]+)ms")

S = {
    "t_call": 0.0, "t0": 0.0, "done": False, "last_poll": 0.0, "phase": "open",
    "last_event": 0.0, "gap": {"vp": 0.0, "visual": 0.0, "after": 0.0},
    "t_restore": None, "t_visual": None, "standins": 0, "resolved": [],
    "land_n": 0, "land_s": 0.0, "land_last": 0.0, "land_n_drain": 0, "land_s_drain": 0.0,
    "worst_turn": 0.0, "load_frames": [], "spin_frames": [], "spinning": False, "spun": False,
}


def note(line):
    with open(RESULT, "a") as f:
        f.write(line + "\n")


def observe(notifier, msg, level):
    try:
        now = time.monotonic()
        if msg.startswith("render "):
            if S["spinning"]:
                S["spin_frames"].append(msg.strip()[:700])
            elif os.environ.get("LT_FRAMES"):
                S["load_frames"].append((now - S["t0"], msg.strip()[:520]))
            return
        if "landing pump" in msg:
            m = PUMP.search(msg)
            if m:
                n = int(m.group(2)) + int(m.group(4))
                s = float(m.group(1)) + float(m.group(3))
                S["land_n"] += n
                S["land_s"] += s
                S["land_last"] = now - S["t0"]
                S["worst_turn"] = max(S["worst_turn"], int(m.group(5)) / 1000.0)
                if S["t_visual"] is None:
                    S["land_n_drain"] += n
                    S["land_s_drain"] += s
                S["last_event"] = now
        elif "stand-in resolved" in msg:
            S["resolved"].append(now - S["t0"])
            S["last_event"] = now
        elif "bounding-box stand-in" in msg:
            S["standins"] += 1
        elif "progressive restore" in msg and "view providers in" in msg:
            S["t_restore"] = now - S["t0"]
            S["phase"] = "visual"
            S["last_event"] = now
            if os.environ.get("LT_FIT"):
                # The view providers exist from here; a fit straight after
                # the open finds none to fit to
                QtCore.QTimer.singleShot(0, fit)
            note("LOG %.2f %s" % (now - S["t0"], msg.strip()[:900]))
        elif "visuals in" in msg:
            S["t_visual"] = now - S["t0"]
            S["phase"] = "after"
            S["last_event"] = now
            note("LOG %.2f %s" % (now - S["t0"], msg.strip()[:900]))
        elif "claimed shapes meshed" in msg or "shapes submitted" in msg:
            note("LOG %.2f %s" % (now - S["t0"], msg.strip()[:200]))
    except Exception:
        pass


def fit():
    try:
        FreeCADGui.SendMsgToActiveView("ViewFit")
        note("INFO view fitted %.2f s after the open returned" % (time.monotonic() - S["t0"]))
    except Exception:
        note("INFO fit failed: " + traceback.format_exc())


def report_load_frames():
    lines = S["load_frames"]
    slow = 0
    for i, (t, ln) in enumerate(lines):
        m = FRAME.search(ln)
        if not m or float(m.group(2)) < 1000.0:
            continue
        slow += 1
        note("SLOW %.1f %s" % (t, ln))
        for t2, ln2 in lines[i + 1:i + 4]:
            if ln2.startswith(("render cpu phases", "render outside")):
                note("SLOW %.1f   %s" % (t2, ln2))
    note("INFO frame lines during the load: %d, of them a second or more per frame: %d"
         % (len([1 for _, ln in lines if ln.startswith("render frame:")]), slow))


def finish():
    if S["done"]:
        return
    S["done"] = True
    if os.environ.get("LT_SHOT"):
        try:
            FreeCADGui.ActiveDocument.ActiveView.saveImage(
                os.environ["LT_SHOT"], 1280, 900, "White")
        except Exception:
            note("INFO shot failed: " + traceback.format_exc())
    try:
        view = FreeCADGui.ActiveDocument.ActiveView
        window = FreeCADGui.getMainWindow()
        note("INFO view %s px, main window %dx%d, platform %s"
             % (view.getSize(), window.width(), window.height(),
                QtCore.QCoreApplication.instance().platformName()))
    except Exception as e:
        note("INFO view size unknown (%r)" % (e,))
    if os.environ.get("LT_FRAMES"):
        report_load_frames()
    r = S["resolved"]
    note("INFO boxes resolved at: " + " ".join("%.1f" % t for t in sorted(r)))
    tv = S["t_visual"] if S["t_visual"] is not None else -1.0
    last_box = max(r) if r else 0.0
    note("INFO view provider drain ended %.2f | visual drain ended %.2f | boxed shapes %d, "
         "resolved %d, first %.2f last %.2f"
         % (S["t_restore"] if S["t_restore"] is not None else -1.0, tv, S["standins"], len(r),
            min(r) if r else 0.0, last_box))
    note("INFO longest stretch away from the event loop: view provider drain %.3f s, visual "
         "drain %.3f s, after it %.3f s" % (S["gap"]["vp"], S["gap"]["visual"], S["gap"]["after"]))
    note("INFO pump items %d in %.2f s, %d (%.2f s) reported before the visual drain ended, the "
         "last reported at %.1f, worst pump turn %.3f s"
         % (S["land_n"], S["land_s"], S["land_n_drain"], S["land_s_drain"], S["land_last"],
            S["worst_turn"]))
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


def spin_start():
    """The load has settled: roll the camera and time the frames."""
    FreeCAD.ParamGet(RENDER).SetBool("DebugTiming", True)
    S["spinning"] = True
    S["gaps"] = []
    S["spin_t0"] = time.monotonic()
    S["spin_last"] = S["spin_t0"]
    S["spin_n"] = 0
    S["spin_base"] = FreeCADGui.ActiveDocument.ActiveView.getCameraOrientation()
    QtCore.QTimer.singleShot(0, spin_step)


def spin_step():
    try:
        now = time.monotonic()
        S["gaps"].append(now - S["spin_last"])
        S["spin_last"] = now
        if now - S["spin_t0"] > float(os.environ["LT_SPIN"]):
            g = sorted(S["gaps"][1:]) or [0.0]
            note("INFO spin: %d steps in %.1f s | step median %.3f s, shortest %.3f, longest %.3f"
                 % (len(g), now - S["spin_t0"], g[len(g) // 2], g[0], g[-1]))
            for ln in S["spin_frames"][-6:]:
                note("FRAME " + ln)
            S["spinning"] = False
            finish()
            return
        S["spin_n"] += 1
        # About the view direction: what is in view stays in view
        FreeCADGui.ActiveDocument.ActiveView.setCameraOrientation(S["spin_base"].multiply(
            FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), 2.0 * (S["spin_n"] % 20))))
    except Exception:
        note("ABORT spin:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(5, spin_step)


def poll():
    try:
        now = time.monotonic()
        if S["last_poll"]:
            key = {"open": "vp", "visual": "visual", "after": "after"}[S["phase"]]
            S["gap"][key] = max(S["gap"][key], now - S["last_poll"])
        S["last_poll"] = now
        if FreeCADGui.isBuildingVisuals():
            S["last_event"] = now
        elif S["t_visual"] is not None and now - S["last_event"] > QUIET:
            if os.environ.get("LT_SPIN") and not S["spun"]:
                S["spun"] = True
                spin_start()
                return
            finish()
            return
        if now - S["t0"] > 420:
            note("INFO gave up waiting: %s" % {k: S[k] for k in ("t_restore", "t_visual")})
            finish()
            return
    except Exception:
        note("ABORT poll:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(10, poll)


def import_step():
    """An import runs inside the call and pumps events: time the loop there."""
    state = {"last": time.monotonic(), "gap": 0.0, "on": True}

    def tick():
        if not state["on"]:
            return
        now = time.monotonic()
        state["gap"] = max(state["gap"], now - state["last"])
        state["last"] = now
        QtCore.QTimer.singleShot(10, tick)

    QtCore.QTimer.singleShot(10, tick)
    import ImportGui
    ImportGui.open(DOC)
    state["on"] = False
    note("INFO import: longest stretch away from the event loop inside the call %.3f s, %d "
         "boxed and %d of them resolved inside it"
         % (state["gap"], S["standins"], len(S["resolved"])))
    if not FreeCADGui.isBuildingVisuals():
        S["t_visual"] = 0.0
        S["phase"] = "after"
    if os.environ.get("LT_FIT"):
        QtCore.QTimer.singleShot(0, fit)
    return FreeCAD.ActiveDocument


def start():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        view.SetBool("ShowNaviCube", False)
        view.SetBool("UseNavigationAnimations", False)
        render = FreeCAD.ParamGet(RENDER)
        if os.environ.get("LT_LEISURE") is not None:
            render.SetBool("CoarseDeferAtLeisure", os.environ["LT_LEISURE"] == "1")
        if os.environ.get("LT_BUDGET"):
            render.SetInt("ProgressiveLoadBudgetMS", int(os.environ["LT_BUDGET"]))
        if os.environ.get("LT_FRAMES"):
            render.SetBool("DebugTiming", True)
        note("INFO at leisure %s, drain budget %d ms"
             % (render.GetBool("CoarseDeferAtLeisure", True),
                render.GetInt("ProgressiveLoadBudgetMS", 100)))
        FreeCAD.setLogLevel("Part", "Log")
        FreeCAD.setLogLevel("Gui", "Log")
        FreeCAD.Console.AttachObserver(observe)
        try:
            from PySide import QtGui
            ctx = QtGui.QOpenGLContext()
            ctx.create()
            surface = QtGui.QOffscreenSurface()
            surface.create()
            ctx.makeCurrent(surface)
            note("INFO renderer %s, level debug %s"
                 % (ctx.functions().glGetString(0x1F01), bool(os.environ.get("FC_LEVEL_DEBUG"))))
            ctx.doneCurrent()
        except Exception as e:
            note("INFO renderer unknown (%r)" % (e,))
        if os.environ.get("LT_WINDOW"):
            width, height = [int(x) for x in os.environ["LT_WINDOW"].split("x")]
            window = FreeCADGui.getMainWindow()
            window.showNormal()
            window.resize(width, height)
            QtCore.QCoreApplication.processEvents()
        S["t_call"] = time.monotonic()
        S["t0"] = S["t_call"]
        if DOC.lower().endswith((".step", ".stp")):
            doc = import_step()
        else:
            doc = FreeCAD.openDocument(DOC)
        S["t0"] = time.monotonic()
        S["last_event"] = S["t0"]
        S["last_poll"] = 0.0
        note("INFO open returned after %.2f s, %d objects"
             % (S["t0"] - S["t_call"], len(doc.Objects)))
    except Exception:
        note("ABORT start:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(10, poll)


QtCore.QTimer.singleShot(1500, start)
