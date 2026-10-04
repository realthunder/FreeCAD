"""A real browser boots the sandbox guest from the FreeCAD serving it.

docs/Sandbox.md 7.20, C1. Serves a document headless with a token grant as
the door and FC_BGFX_VIEWER_BUILD at build/wasm, then scripts/console-drive.js
opens web/console-test.html?token=... in Chrome. The page fetches
/pyodide/boot.json with the token, boots pyodide and the fcx_image wheel from
/pyodide/ with the bundled wheels and the package set, evaluates, and leaves
its verdict on window.fcxConsole; every check of the page is copied here as a
PASS/FAIL line of its own, and the boot and memory figures as a NOTE.

tests/gui/sandbox-console-serve.py checks the endpoints themselves without a
browser, and is the registered one. This is not registered, for the reason
docs/Testing.md gives: it needs the web bundle (`npm run build` in
src/Gui/Renderer/web, into build/wasm/web), node, a puppeteer-core install
named by PUPPETEER_PATH and a Chrome named by CHROME, and skips rather than
fails when any is missing. NODE names node when it is not on PATH;
CHROME_LIBS a directory for Chrome's LD_LIBRARY_PATH (a Chrome for Testing
on a box without libasound).

  PUPPETEER_PATH=~/works/sw/fcad-probes/node_modules/puppeteer-core \\
  CHROME=$(ls ~/.cache/puppeteer/chrome/linux-*/chrome-linux64/chrome) \\
  CHROME_LIBS=~/.cache/puppeteer/lib \\
  NODE=~/works/sw/emsdk-5.0.3/node/24.19.0_64bit/bin/node \\
  scripts/gui-test.sh tests/gui/sandbox-console-browser.py /tmp/console-web \\
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
PAGE = os.path.join(WASM, "web", "console-test.html")
DRIVER = os.path.join(REPO, "scripts", "console-drive.js")
DOC = "SandboxConsoleBrowser"
TOKEN = "c1-console-token"
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
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL"
        )
        doc = FreeCAD.newDocument(DOC, hidden=True)
        box = doc.addObject("Part::Box", "Box")
        doc.recompute()
        port = free_port()
        if not check(
            "the document is served headless", FreeCADGui.serveDocument(doc, port), "port %d" % port
        ):
            finish()
            return
        FreeCADGui.serveSetGrants([{"token": TOKEN}])
        url = "http://127.0.0.1:%d/web/console-test.html?token=%s" % (port, TOKEN)
        note("NOTE " + url)
        log = open(os.path.join(OUT, "drive.log"), "w")
        state["proc"] = subprocess.Popen(
            [node, DRIVER, url, "fcxConsole", str((RUN_WAIT_S - 30) * 1000)],
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
    check("the page booted the guest", not report.get("error"), report.get("error", ""))
    check("the browser has JSPI", report.get("jspi") is True)
    for c in report.get("checks", []):
        check("page: " + c["name"], c["pass"], c["detail"])
    note(
        "NOTE pyodide %s, runtime %s ms, wheels %s ms, guest memory %s MB"
        % (
            report.get("version"),
            report.get("runtimeMs"),
            report.get("wheelsMs"),
            report.get("memoryMB"),
        )
    )
    check("the page's verdict is ok", report.get("ok") is True)
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
