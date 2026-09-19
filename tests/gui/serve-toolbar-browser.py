"""A real browser draws the desktop's tool bars and runs a sketch tool from one.

The browser half of docs/ThinClient.md 8.11 item 4. src/Mod/Test/
SandboxToolBarMirror.py checks the stream itself against an injected
sender. What it cannot check is the client in
src/Gui/Renderer/web/src/toolbar.tsx on a real connection, which is what
this drives (scripts/toolbar-drive.js):

  - the page subscribes by itself and draws the bars the desktop shows,
    in the desktop's order, with icons fetched by name -- compared with
    the desktop's own mirror at the same moment;
  - the server refuses a command that is not a sketch tool, as a group
    member and as a plain command, and every button the page ENABLES is
    one it would run; the others are drawn disabled and say why;
  - entering the sketch switches the desktop's workbench and the bars
    follow it;
  - a group's face starts its default tool in the browser's view (the
    tool's on-view parameters arrive), and a member chosen from the
    drop-down runs and moves the group's default the desktop's way, which
    comes back through the stream as the face's new command;
  - the launcher's switch unsubscribes -- a later change on the desktop
    pushes nothing -- and remembers the choice; on again brings a fresh
    snapshot.

Needs the built viewer with its DOM bundle (build/wasm, `ninja -C
build/wasm`), node, PUPPETEER_PATH (a puppeteer-core install) and CHROME.
Skips rather than fails when any is missing.

  PUPPETEER_PATH=/path/to/node_modules/puppeteer-core \\
  CHROME=~/.cache/puppeteer/chrome/*/chrome-linux64/chrome \\
  scripts/gui-test.sh tests/gui/serve-toolbar-browser.py /tmp/toolbar-web \\
      --timeout 600
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
from PySide import QtCore, QtWidgets

from wsclient import free_port

clock = time.perf_counter

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
REPO = os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__))))
WASM = os.path.join(REPO, "build", "wasm")
DRIVER = os.path.join(REPO, "scripts", "toolbar-drive.js")
DOC = "ServeToolbarBrowser"
OBJ = "Sketch"
SETTLE_MS = 90000
RUN_WAIT_S = 420
REFUSED_TIP = "Runs from the desktop until dialogs are mirrored"
REF = "IPY_MODEL_"

state = {"doc": None, "done": False, "run": None, "t0": clock(),
         "phases": [], "expected": {}}


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


def browser_safe(name):
    """The server's allowlist (SceneControl.cpp isBrowserSafeCommand)."""
    return (name.startswith("Sketcher_Create") or name == "Sketcher_External"
            or name == "Sketcher_CarbonCopy")


class Run(threading.Thread):
    """The browser, driven from off the GUI thread. Log, phase markers and
    acks are files: node buffers stdout to a pipe."""

    def __init__(self, url):
        super().__init__(daemon=True)
        self.url = url
        self.error = None
        self.log_path = os.path.join(OUT, "drive.log")
        self.phase_path = os.path.join(OUT, "phases.txt")
        self.ack_path = os.path.join(OUT, "acks.txt")
        self.result_path = os.path.join(OUT, "drive-result.json")
        for path in (self.phase_path, self.ack_path):
            open(path, "w").close()

    def run(self):
        try:
            env = dict(os.environ)
            env["LD_LIBRARY_PATH"] = (
                os.path.join(REPO, ".conda", "freecad", "lib")
                + os.pathsep + env.get("LD_LIBRARY_PATH", ""))
            env["TOOLBAR_PHASES"] = self.phase_path
            env["TOOLBAR_ACK"] = self.ack_path
            env["TOOLBAR_RESULT"] = self.result_path
            with open(self.log_path, "w") as log:
                proc = subprocess.Popen(
                    ["node", DRIVER, self.url, OBJ, str(SETTLE_MS)],
                    stdout=log, stderr=subprocess.STDOUT, env=env)
                proc.wait()
            if proc.returncode != 0:
                self.error = "node exited %d:\n%s" % (proc.returncode, self.tail())
        except Exception:
            self.error = traceback.format_exc()

    def phases(self):
        try:
            with open(self.phase_path) as f:
                return [line.strip() for line in f if line.strip()]
        except OSError:
            return []

    def ack(self, name):
        with open(self.ack_path, "a") as f:
            f.write(name + "\n")

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


def plain(text):
    return text.replace("&&", "\0").replace("&", "").replace("\0", "&") if text else ""


def expected_bars():
    """What the page should draw, from the desktop's own mirror: the order
    object's bars, visible, not the status or menu bar's, top area first."""
    fw = FreeCADGui.FormWidgets
    order = fw.snapshot("toolbars") or {}
    rows = []
    for item in (order.get("layout") or {}).get("items", []):
        ref = item.get("widget")
        if not isinstance(ref, str) or not ref.startswith(REF):
            continue
        bid = ref[len(REF):]
        snap = fw.snapshot(bid) or {}
        st = snap.get("state") or {}
        area = st.get("q_area", "")
        if st.get("q_visible") is False or area == "statusbar" or area.startswith("menubar"):
            continue
        rows.append((area, plain(st.get("q_windowTitle", "")) or bid))
    return [t for a, t in rows if a == "top"] + [t for a, t in rows if a != "top"]


def build():
    try:
        import Part

        if not os.path.exists(os.path.join(WASM, "fcviewer.html")):
            skip("no built viewer at %s (ninja -C build/wasm)" % WASM)
            return
        if not os.path.exists(os.path.join(WASM, "web", "inspector.js")):
            skip("no DOM bundle at %s/web (the bars are drawn by it)" % WASM)
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
        # every on-view box, so a tool shows one before its first click --
        # they are how the page proves the tool runs in its view
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/Tools").SetInt(
                "OnViewParameterVisibility", 2)
        FreeCADGui.activateWorkbench("PartWorkbench")
        doc = FreeCAD.newDocument(DOC, hidden=True)
        state["doc"] = doc
        sketch = doc.addObject("Sketcher::SketchObject", OBJ)
        sketch.addGeometry(Part.LineSegment(FreeCAD.Vector(-5, 0, 0),
                                            FreeCAD.Vector(5, 0, 0)), False)
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


def on_phase(name):
    run = state["run"]
    if name in ("settled", "entered"):
        state["expected"][name] = expected_bars()
        state["workbench"] = state.get("workbench", {})
        state["workbench"][name] = FreeCADGui.activeWorkbench().name()
        run.ack(name)
    elif name == "toggled":
        # A change the stream would push to a subscriber: a bar hidden and
        # shown again. The page has switched off; it must see nothing.
        bars = [tb for tb in FreeCADGui.getMainWindow().findChildren(QtWidgets.QToolBar)
                if tb.isVisible()]
        state["toggled_bar"] = bars[0].objectName() if bars else None
        if bars:
            bars[0].hide()

        def show_again():
            if bars:
                bars[0].show()
            QtCore.QTimer.singleShot(400, lambda: run.ack(name))

        QtCore.QTimer.singleShot(400, show_again)


def poll():
    run = state["run"]
    for name in run.phases()[len(state["phases"]):]:
        state["phases"].append(name)
        try:
            on_phase(name)
        except Exception:
            note("FAIL phase %s:\n%s" % (name, traceback.format_exc()))
            run.ack(name)
    if run.is_alive():
        if clock() - state["t0"] > RUN_WAIT_S:
            check("the browser finished", False,
                  "still running after %ds\n%s" % (RUN_WAIT_S, run.tail()))
            finish()
            return
        QtCore.QTimer.singleShot(100, poll)
        return
    QtCore.QTimer.singleShot(700, verify)


def verify():
    run = state["run"]
    out = run.result()
    exp = state["expected"]
    try:
        if not check("the browser ran without error", run.error is None, run.error or ""):
            note(run.tail())
        check("the driver reported no error", not out.get("error"), out.get("error", ""))
        note("workbenches %s, toggled bar %s" % (state.get("workbench"),
                                                 state.get("toggled_bar")))

        check("the page subscribed and drew bars by itself",
              out.get("firstBarsMs") is not None, out.get("firstBarsMs"))
        check("the bars are the desktop's, in its order",
              bool(exp.get("settled")) and out.get("bars1") == exp.get("settled"),
              (out.get("bars1"), exp.get("settled")))
        buttons = out.get("buttons1") or []
        with_icon = [b for b in buttons if b.get("icon")]
        check("the buttons have their icons", buttons and len(with_icon) >= len(buttons) * 0.8,
              "%d of %d" % (len(with_icon), len(buttons)))
        check("the panels on the top edge clear the strip",
              out.get("topInset", "").strip() not in ("", "0px"), out.get("topInset"))

        for key in ("refusedMember", "refusedPlain"):
            reply = out.get(key) or {}
            check("the server refuses " + key, reply.get("ok") is False
                  and reply.get("code") == "CommandRefused", reply)

        check("the browser entered the sketch", out.get("entered") is True)
        check("the sketch tools arrived, enabled", out.get("sketchTools") is True)
        check("the bars followed the switch, in the desktop's order",
              bool(exp.get("entered")) and out.get("bars2") == exp.get("entered")
              and out.get("bars2") != out.get("bars1"),
              (out.get("bars2"), exp.get("entered")))
        buttons = out.get("buttons2") or []
        enabled = [b["cmd"] for b in buttons if not b.get("disabled")]
        check("every enabled button is one the server runs",
              enabled and all(browser_safe(c) for c in enabled), enabled)
        refused = [b for b in buttons if b.get("disabled") and REFUSED_TIP in b.get("title", "")]
        check("the others are drawn, disabled, saying why", len(refused) > 0,
              len(refused))

        face = out.get("face") or {}
        check("a group's face runs its tool in this view",
              face.get("cmd") and not face.get("error") and (face.get("onview") or 0) > 0,
              face)
        member = out.get("member") or {}
        check("a member from the drop-down runs and moves the default",
              member.get("chosen") and member.get("after") == member.get("chosen")
              and not member.get("error") and member.get("menuClosed"), member)

        off = out.get("off") or {}
        check("switching off removes the strip",
              off.get("ok") and off.get("bars") == 0, off)
        check("and the panels move back up", off.get("topInset", "").strip() in ("0px", ""),
              off.get("topInset"))
        check("and is remembered", off.get("stored") == "0", off.get("stored"))
        check("and unsubscribes: a desktop change pushes nothing",
              state.get("toggled_bar") and off.get("pushesAfter") == off.get("pushes"),
              (off.get("pushes"), off.get("pushesAfter")))
        on = out.get("on") or {}
        check("switching on again brings a fresh snapshot",
              on.get("ok") and (on.get("bars") or 0) > 0 and on.get("stored") == "1", on)
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
    try:
        FreeCADGui.getDocument(DOC).resetEdit()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, build)
