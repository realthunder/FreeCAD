"""The Python console panel in Safari, which has no JSPI: the guest in a worker.

docs/Sandbox.md 7.20, C6. The twin of tests/gui/sandbox-console-panel-browser.py
-- the same page, the same drive, the same checks -- for the browser no driver
attaches to. Safari cannot suspend a wasm stack on a promise, so the console
boots the guest in a Worker and parks it in Atomics.wait while the page carries
each op over the socket; that needs a cross-origin isolated page, which the
served pages are (SceneServer.cpp sets COOP/COEP).

No WebDriver: `open -a Safari` opens the gate page, the page drives the panel
itself (?drive=1) and POSTs its verdict to a collector this test runs
(?report=). ?guest=worker forces the transport, so a browser that HAS JSPI
still gates the worker path.

macOS only, and not registered (docs/Testing.md). Run it by hand:

  OUT=/tmp/gt-safari; mkdir -p "$OUT/.iso/cache" "$OUT/.iso/config"
  XDG_CACHE_HOME=$OUT/.iso/cache XDG_CONFIG_HOME=$OUT/.iso/config \\
  GT_OUT=$OUT GT_RESULT=$OUT/result.txt \\
  .conda/run.sh build/mac-relwithdebinfo-801/bin/FreeCAD \\
      --user-cfg "$OUT/.iso/user.cfg" tests/gui/sandbox-console-safari.py

SAFARI_BROWSER names another browser for `open -a` (any browser the page can
reach); SAFARI_KEEP=1 leaves the tab open at the end.
"""

import http.server
import json
import os
import platform
import socket
import subprocess
import threading
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WASM = os.path.join(REPO, "build", "wasm")
PAGE = os.path.join(WASM, "web", "console-panel-test.html")
BROWSER = os.environ.get("SAFARI_BROWSER", "Safari")
DOC = "ConsoleSafari"
DOC2 = "ConsoleSafariSecond"
TOKEN = "c6-console-token"
RUN_WAIT_S = 600

# Before the server answers anything: it reads the bundle directory once,
# on the first request.
os.environ["FC_BGFX_VIEWER_BUILD"] = WASM

state = {"done": False, "report": None, "t0": time.perf_counter(), "url": ""}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class Collector(http.server.BaseHTTPRequestHandler):
    """One POST, the page's verdict. CORS open because the page is another
    origin (the FreeCAD server's port), and the isolated page's fetch is a
    CORS request."""

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.end_headers()

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        try:
            state["report"] = json.loads(body.decode("utf-8"))
        except ValueError:
            state["report"] = {"error": "the page posted something that is not JSON"}
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")

    def log_message(self, *args):
        pass


def missing():
    if platform.system() != "Darwin":
        return "macOS, where `open -a %s` is the driver" % BROWSER
    if not os.path.isfile(PAGE):
        return "the web bundle (%s)" % PAGE
    return None


def build():
    try:
        why = missing()
        if why:
            note("SKIP the Safari leg needs " + why)
            finish()
            return
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("AutoSaveTimeout", 0)
        doc = FreeCAD.newDocument(DOC, hidden=True)
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        doc2 = FreeCAD.newDocument(DOC2, hidden=True)
        doc2.recompute()

        port = free_port()
        if not check(
            "the document is served headless", FreeCADGui.serveDocument(doc, port), "port %d" % port
        ):
            finish()
            return
        check("a second document is served", FreeCADGui.serveDocument(doc2, port))
        FreeCADGui.serveSetGrants([{"token": TOKEN}])

        back = free_port()
        server = http.server.ThreadingHTTPServer(("127.0.0.1", back), Collector)
        state["server"] = server
        threading.Thread(target=server.serve_forever, daemon=True).start()

        state["url"] = (
            "http://127.0.0.1:%d/web/console-panel-test.html"
            "?token=%s&doc=%s&doc2=%s&drive=1&guest=worker&report=%s"
            % (port, TOKEN, DOC, DOC2, "http://127.0.0.1:%d/report" % back)
        )
        note("NOTE " + state["url"])
        subprocess.run(["open", "-a", BROWSER, state["url"]], check=True)
        QtCore.QTimer.singleShot(500, poll)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def poll():
    if state["report"] is None:
        if time.perf_counter() - state["t0"] > RUN_WAIT_S:
            check("the page reported", False, "nothing posted after %ds" % RUN_WAIT_S)
            finish()
            return
        QtCore.QTimer.singleShot(500, poll)
        return
    verify()


def verify():
    report = state["report"]
    check("the page reported", True, report.get("ua", ""))
    check(
        "the console ran the guest in a worker",
        report.get("transport") == "worker",
        report.get("transport"),
    )
    check("the page drove the panel to the end", not report.get("error"), report.get("error", ""))
    for c in report.get("checks", []):
        check("page: " + c["name"], c["pass"], c["detail"])
    note("NOTE boot %s ms, bridge %s" % (report.get("bootMs"), json.dumps(report.get("stats"))))
    check("the page's verdict is ok", report.get("ok") is True)

    box = FreeCAD.getDocument(DOC).getObject("Box")
    check("the desktop sees the console's write", box.Length.Value == 30, box.Length)
    made = FreeCAD.getDocument(DOC2).getObject("FromConsole")
    check(
        "the desktop sees the object made after the switch",
        made is not None and made.Height.Value == 3,
        made and made.Height,
    )
    finish()


def close_tab():
    """Leave the browser as it was found: the gate's tab goes."""
    if os.environ.get("SAFARI_KEEP") or BROWSER != "Safari":
        return
    script = (
        'tell application "Safari" to close (every tab of every window '
        'whose URL contains "console-panel-test")'
    )
    try:
        subprocess.run(["osascript", "-e", script], timeout=20,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception:
        pass


def finish():
    if state["done"]:
        return
    state["done"] = True
    close_tab()
    if state.get("server"):
        state["server"].shutdown()
    try:
        FreeCADGui.serveStop()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(0, build)
