"""A real browser sends a copy larger than one message carries, in pieces.

The browser half of docs/TransactionLog.md sec 32.36.
scripts/transaction-log-request-check.py checks the ops -- `upload.begin`,
`upload.piece`, the op naming the upload, and every refusal -- from the
host's own Python. What it cannot check is the page (web/src/control.ts
sendFile, main.tsx): that the launcher's "Send my copy to merge..." sends a
file past the 46 MB one frame of the socket took whole, says how far it has
got while it goes, and says it was sent at the end.

So this serves a document under a share whose upload limit is raised past
the file, serves the built viewer over HTTP, and lets Chrome pick a 60 MB
file through scripts/request-drive.js. Asserted:

  - the page has the row, and its alert says the file was sent;
  - while it went, the page said how far it had got, more than once, and
    ended at the whole; nothing of it is left on the page after;
  - the host has it waiting, unread, under the page's name, and its bytes
    are the file's;
  - nothing of the pieces is held after.

Needs the built viewer (build/wasm/fcviewer.html; `ninja -C build/wasm`),
node, a puppeteer-core install named by PUPPETEER_PATH, and a Chrome named
by CHROME. Skips rather than fails when any is missing.

  PUPPETEER_PATH=/path/to/node_modules/puppeteer-core \\
  CHROME=~/.cache/puppeteer/chrome/*/chrome-linux64/chrome \\
  scripts/gui-test.sh tests/gui/serve-request-browser.py /tmp/request-web \\
      --timeout 600
"""
import functools
import hashlib
import http.server
import json as jsonlib
import os
import subprocess
import tempfile
import threading
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

import wsclient
from wsclient import free_port

clock = time.perf_counter

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
REPO = os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__))))
WASM = os.path.join(REPO, "build", "wasm")
DRIVER = os.path.join(REPO, "scripts", "request-drive.js")
DOC = "ServeRequestBrowser"
SIZE = 60 * 1024 * 1024
RUN_WAIT_S = 300
TOKEN = "request-browser-invite"
CLIENT = "request-browser"

state = {"doc": None, "done": False, "run": None, "t0": clock()}


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


class Run(threading.Thread):
    """The browser, driven from off the GUI thread."""

    def __init__(self, url, path):
        super().__init__(daemon=True)
        self.url = url
        self.path = path
        self.error = None
        self.code = None
        self.log_path = os.path.join(OUT, "drive.log")

    def run(self):
        try:
            env = dict(os.environ)
            env["LD_LIBRARY_PATH"] = (
                os.path.join(REPO, ".conda", "freecad", "lib")
                + os.pathsep + env.get("LD_LIBRARY_PATH", ""))
            with open(self.log_path, "w") as log:
                proc = subprocess.Popen(
                    ["node", DRIVER, self.url, self.path, "120000"],
                    stdout=log, stderr=subprocess.STDOUT, env=env)
                proc.wait()
            self.code = proc.returncode
        except Exception:
            self.error = traceback.format_exc()

    def tail(self, lines=25):
        try:
            with open(self.log_path) as f:
                return "".join(f.readlines()[-lines:])
        except OSError:
            return "(no driver log)"

    def report(self):
        try:
            with open(self.log_path) as f:
                for line in f:
                    if line.startswith("REPORT "):
                        return jsonlib.loads(line[len("REPORT "):])
        except Exception:
            pass
        return {}


def serve_viewer():
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=WASM)
    handler.log_message = lambda *a, **k: None
    port = free_port()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    state["httpd"] = server
    return port


def build():
    try:
        if not os.path.exists(os.path.join(WASM, "fcviewer.html")):
            skip("no built viewer at %s (ninja -C build/wasm)" % WASM)
            return
        if not os.path.exists(DRIVER):
            skip("no driver at %s" % DRIVER)
            return
        if not os.environ.get("PUPPETEER_PATH"):
            skip("PUPPETEER_PATH is unset (a puppeteer-core install)")
            return
        if not os.environ.get("CHROME"):
            skip("CHROME is unset (a Chrome executable)")
            return

        p = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
        share = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/SceneShare")
        state["log"] = p.GetInt("TransactionLog", 2)
        state["limit"] = share.GetInt("UploadLimitMB", 16)
        state["total"] = share.GetInt("RequestsTotalMB", 64)
        p.SetInt("TransactionLog", 2)
        p.SetInt("AutoSaveTimeout", 0)
        share.SetInt("UploadLimitMB", 100)
        share.SetInt("RequestsTotalMB", 200)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        where = tempfile.mkdtemp(prefix="fc-request-browser-")
        state["where"] = where
        doc = FreeCAD.newDocument(DOC, hidden=True)
        state["doc"] = doc
        doc.UndoMode = 1
        doc.openTransaction("base")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        doc.commitTransaction()
        doc.saveAs(os.path.join(where, "served.FCStd"))

        # The host keeps a sent file unread (H6), so what it holds need not
        # be a document: bytes no two runs share, compared as they come.
        sent = os.path.join(where, "large.FCStd")
        data = os.urandom(SIZE)
        state["sha"] = hashlib.sha1(data).hexdigest()
        with open(sent, "wb") as f:
            f.write(data)
        del data

        port = free_port()
        if not check("the document is served headless",
                     FreeCADGui.serveDocument(doc, port), "port %d" % port):
            finish()
            return
        http_port = serve_viewer()
        note("viewer on %d, scene on %d" % (http_port, port))
        FreeCADGui.serveSetGrants(wsclient.invitation(TOKEN, CLIENT))
        url = ("http://127.0.0.1:%d/fcviewer.html?scene=http://127.0.0.1:%d"
               "&token=%s&client=%s" % (http_port, port, TOKEN, CLIENT))
        state["run"] = Run(url, sent)
        state["t0"] = clock()
        state["run"].start()
        QtCore.QTimer.singleShot(50, poll)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def poll():
    run = state["run"]
    if run.is_alive():
        if clock() - state["t0"] > RUN_WAIT_S:
            check("the browser run finished", False, "still running after %ds" % RUN_WAIT_S)
            note("---- driver log")
            note(run.tail(25))
            finish()
            return
        QtCore.QTimer.singleShot(50, poll)
        return
    QtCore.QTimer.singleShot(600, verify)


def verify():
    try:
        run = state["run"]
        out = run.report()
        if run.error or run.code != 0:
            note("---- driver log")
            note(run.tail(25))
        check("the page has the row, and says the file was sent",
              out.get("row") is True and (out.get("alert") or "").startswith("Sent large.FCStd")
              and run.code == 0, (run.code, out.get("alert"), run.error))
        progress = out.get("progress") or []
        check("while it went, the page said how far it had got, and ended at the whole",
              len(progress) >= 3 and progress[-1].endswith("60.0 of 60.0 MB")
              and any(" of 60.0 MB" in line and not line.endswith("60.0 of 60.0 MB")
                      for line in progress),
              progress[:3] + ["..."] + progress[-2:])
        check("nothing of it is left on the page after", out.get("after") is False,
              out.get("after"))

        doc = state["doc"]
        waiting = FreeCADGui.serveRequests(doc.Name)
        check("the host has it waiting, unread, under the page's name",
              len(waiting) == 1 and waiting[0]["name"] == "large.FCStd"
              and waiting[0]["size"] == SIZE and waiting[0]["sender"] == CLIENT
              and waiting[0]["inLog"] is True,
              [(r["name"], r["size"], r["sender"]) for r in waiting])
        if waiting:
            copy = os.path.join(state["where"], "kept.FCStd")
            doc.writeTransactionSentFile(waiting[0]["id"], copy)
            with open(copy, "rb") as f:
                kept = hashlib.sha1(f.read()).hexdigest()
            check("its bytes are the file's", kept == state["sha"], (kept, state["sha"]))
        check("nothing of the pieces is held after", FreeCADGui.serveUploads() == (0, 0),
              FreeCADGui.serveUploads())
    except Exception:
        note("FAIL verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        if "log" in state:
            FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
                "TransactionLog", state["log"])
            share = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/SceneShare")
            share.SetInt("UploadLimitMB", state["limit"])
            share.SetInt("RequestsTotalMB", state["total"])
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, build)
