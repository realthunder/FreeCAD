"""The Python console panel, used in a real browser page against the served document.

docs/Sandbox.md 7.20, C4. Serves two documents headless with a token grant as
the door and FC_BGFX_VIEWER_BUILD at build/wasm, then scripts/console-drive.js
opens web/console-panel-test.html?token=...&doc=...&doc2=...&drive=1 in Chrome.
The page mounts the console panel with no WASM viewer, boots the guest the
first time the panel opens, and drives the panel through DOM events the way a
person does: lines entered, a block, an error, Tab, the history, a paste, the
Interrupt button on a loop reaching the host, a switch to the second document.
Its verdict is left on window.fcxConsolePanel; every check is copied here as
a PASS/FAIL line, the bridge figures as a NOTE, and the desktop's documents are
checked for what the console wrote.

Not registered, for the reason docs/Testing.md gives; the environment is the
one tests/gui/sandbox-console-browser.py names:

  PUPPETEER_PATH=~/works/sw/fcad-probes/node_modules/puppeteer-core \\
  CHROME=$(ls ~/.cache/puppeteer/chrome/linux-*/chrome-linux64/chrome) \\
  CHROME_LIBS=~/.cache/puppeteer/lib \\
  NODE=~/works/sw/emsdk-5.0.3/node/24.19.0_64bit/bin/node \\
  scripts/gui-test.sh tests/gui/sandbox-console-panel-browser.py /tmp/console-panel \\
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
PAGE = os.path.join(WASM, "web", "console-panel-test.html")
DRIVER = os.path.join(REPO, "scripts", "console-drive.js")
DOC = "ConsolePanelBrowser"
DOC2 = "ConsolePanelSecond"
TOKEN = "c4-console-token"
RUN_WAIT_S = 420

# Before the server answers anything: it reads the bundle directory once,
# on the first request.
os.environ["FC_BGFX_VIEWER_BUILD"] = WASM

state = {"done": False, "proc": None, "thread": None, "t0": time.perf_counter()}


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
    if not os.path.isfile(PAGE):
        return "the web bundle (%s)" % PAGE, None
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
        url = "http://127.0.0.1:%d/web/console-panel-test.html?token=%s&doc=%s&doc2=%s&drive=1" % (
            port,
            TOKEN,
            DOC,
            DOC2,
        )
        note("NOTE " + url)
        log = open(os.path.join(OUT, "drive.log"), "w")
        state["proc"] = subprocess.Popen(
            [node, DRIVER, url, "fcxConsolePanel", str((RUN_WAIT_S - 30) * 1000)],
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


def poll():
    if state["proc"].poll() is None:
        if time.perf_counter() - state["t0"] > RUN_WAIT_S:
            check("the page reported", False, "still running after %ds" % RUN_WAIT_S)
            state["proc"].kill()
            finish()
            return
        QtCore.QTimer.singleShot(200, poll)
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
