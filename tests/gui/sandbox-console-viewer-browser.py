"""The Python console in the real viewer page rides the viewer's own connection.

docs/Sandbox.md 7.20, C4. tests/gui/sandbox-console-panel-browser.py uses the
panel on a gate page with no viewer, on a connection of its own. This serves
two documents with the built WASM viewer (build/wasm), opens the viewer page
itself with ?console in Chrome, and has scripts/console-drive.js inject
web/viewerconsole.js, which drives the panel the viewer chrome mounted.

What only the real page shows: the console's bridge frames ride the viewer's
scene socket, so the console is the viewer's connection. Checked here -- the
server never sees more than one client; the owner's view-only switch for that
client refuses the console's write and giving editing back restores it; the
viewer's document switch moves the console. The page asks for each host step
through the connection's roster label ("fcx-ask:<what>"), which a view-only
connection can still set; this side polls Gui.serveClients() and acts.

Not registered, for the reason docs/Testing.md gives. Needs the built viewer
(`cmake --build build/wasm`) and the variables of
tests/gui/sandbox-console-browser.py:

  PUPPETEER_PATH=~/works/sw/fcad-probes/node_modules/puppeteer-core \\
  CHROME=$(ls ~/.cache/puppeteer/chrome/linux-*/chrome-linux64/chrome) \\
  CHROME_LIBS=~/.cache/puppeteer/lib \\
  NODE=~/works/sw/emsdk-5.0.3/node/24.19.0_64bit/bin/node \\
  scripts/gui-test.sh tests/gui/sandbox-console-viewer-browser.py /tmp/console-viewer \\
      --timeout 600
"""

import json
import os
import shutil
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
DRIVER = os.path.join(REPO, "scripts", "console-drive.js")
DOC = "ConsoleViewerBrowser"
DOC2 = "ConsoleViewerSecond"
TOKEN = "c4-viewer-token"
RUN_WAIT_S = 420

# Before the server answers anything: it reads the bundle directory once,
# on the first request.
os.environ["FC_BGFX_VIEWER_BUILD"] = WASM

state = {
    "done": False,
    "proc": None,
    "thread": None,
    "t0": time.perf_counter(),
    "maxClients": 0,
    "answered": set(),
}


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


def missing():
    node = os.environ.get("NODE") or shutil.which("node")
    if not node:
        return "node (NODE or PATH)", None
    pp = os.environ.get("PUPPETEER_PATH", "")
    if not pp or not os.path.isdir(pp):
        return "a puppeteer-core install (PUPPETEER_PATH)", None
    chrome = os.environ.get("CHROME", "")
    if not chrome or not os.path.isfile(chrome):
        return "a Chrome binary (CHROME)", None
    for need in ("fcviewer.html", os.path.join("web", "viewerconsole.js")):
        if not os.path.isfile(os.path.join(WASM, need)):
            return "the built viewer (%s)" % os.path.join(WASM, need), None
    return None, node


def build():
    try:
        why, node = missing()
        if why:
            note("SKIP the browser leg needs " + why)
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
        url = "http://127.0.0.1:%d/?token=%s&doc=%s&doc2=%s&console" % (port, TOKEN, DOC, DOC2)
        note("NOTE " + url)
        log = open(os.path.join(OUT, "drive.log"), "w")
        state["proc"] = subprocess.Popen(
            [
                node,
                DRIVER,
                url,
                "fcxConsoleViewer",
                str((RUN_WAIT_S - 30) * 1000),
                "web/viewerconsole.js",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        state["lines"] = []

        def pump():
            for line in state["proc"].stdout:
                log.write(line)
                log.flush()
                state["lines"].append(line.rstrip("\n"))
            log.close()

        state["thread"] = threading.Thread(target=pump, daemon=True)
        state["thread"].start()
        QtCore.QTimer.singleShot(200, poll)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def answer():
    """Act on what the page asks for through its roster label."""
    clients = FreeCADGui.serveClients()
    state["maxClients"] = max(state["maxClients"], len(clients))
    for c in clients:
        label = c.get("client", "")
        if not label.startswith("fcx-ask:") or label in state["answered"]:
            continue
        state["answered"].add(label)
        what = label[len("fcx-ask:") :]
        if what in ("viewonly", "edit"):
            ok = FreeCADGui.serveSetClientMode(c["id"], what == "viewonly")
            note("NOTE client %s set %s: %s" % (c["id"], what, ok))


def poll():
    try:
        answer()
    except Exception:
        note("FAIL poll:\n" + traceback.format_exc())
    if state["proc"].poll() is None:
        if time.perf_counter() - state["t0"] > RUN_WAIT_S:
            check("the page reported", False, "still running after %ds" % RUN_WAIT_S)
            state["proc"].kill()
            finish()
            return
        QtCore.QTimer.singleShot(100, poll)
        return
    state["thread"].join(10)
    verify()


def verify():
    report = None
    for line in state["lines"]:
        if line.startswith("REPORT "):
            try:
                report = json.loads(line[len("REPORT ") :])
            except ValueError:
                pass
    if not check(
        "the page reported", report is not None, "exit %s, see drive.log" % state["proc"].returncode
    ):
        finish()
        return
    check("the page drove the console to the end", not report.get("error"), report.get("error", ""))
    for c in report.get("checks", []):
        check("page: " + c["name"], c["pass"], c["detail"])
    note("NOTE boot %s ms, bridge %s" % (report.get("bootMs"), json.dumps(report.get("stats"))))
    check("the page's verdict is ok", report.get("ok") is True)

    check(
        "the server saw one connection, the viewer's, all along",
        state["maxClients"] == 1,
        state["maxClients"],
    )
    check(
        "both host steps were asked for",
        {"fcx-ask:viewonly", "fcx-ask:edit"} <= state["answered"],
        sorted(state["answered"]),
    )
    box = FreeCAD.getDocument(DOC).getObject("Box")
    check(
        "the desktop has the console's writes and not the refused one",
        box.Length.Value == 40 and box.Height.Value == 12,
        "%s %s" % (box.Length, box.Height),
    )
    made = FreeCAD.getDocument(DOC2).getObject("FromViewer")
    check(
        "the desktop sees the object made after the viewer's switch",
        made is not None and made.Width.Value == 5,
        made and made.Width,
    )
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.serveStop()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(0, build)
