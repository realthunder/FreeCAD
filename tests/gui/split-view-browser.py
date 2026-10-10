"""The browser viewer's split view follows the desktop's view cells.

docs/HandsOnQueue.md entry 57: "btw, do the same view cell logic in
browser" -- after entries 29 and 56 (docs/SplitViews.md sec 21 and 22). The
chrome in src/Gui/Renderer/web/src/splitview.tsx split, joined and resized
LIVE, as the pointer moved; it now shows a drag as frames over the cells it
will change and carries it out at the release of the primary button.

None of that needs a scene: the built viewer page is served as plain files
and driven in a real browser by scripts/splitview-drive.js, whose claims
are this test's:

  split   - a corner zone dragged into its cell: still one cell, two
            frames, "kept" and "fresh" (with a plus), that tile it with the
            border under the cursor; dragged back to where it was pressed
            the frames go; released, two cells of the frames' sizes;
  look    - a frame's face is the accent at 0.3, inside a white border two
            pixels wide, inside a thin dark line;
  cancel  - Escape, the right button, the middle button and the window
            losing the front each take the frames away, and the release
            after them changes nothing; the right click brings no menu;
  border  - a dragged border changes no cell while the button is down,
            frames both cells at their new sizes, and moves at the release;
  minimum - the border stops where a cell would go under 300 pixels;
  close   - dragged well past that the cell is shown as going, red and
            crossed out, the other framed over both; Escape gives it up;
            released, the cell is closed;
  row     - three cells in a row: a border takes room from the cell next
            to it and from no other -- its frames are those two cells', the
            third has none -- and past that cell's minimum the drag closes
            it, the first cell taking its room and the third staying where
            and as wide as it was ("do not move the other splitter in case
            the next view size limit is reached. change it to view close
            action when size limit reached");
  join    - a corner dragged out into the neighbor: the cell that stays
            framed over both, the cell that goes framed red and crossed
            out with the other's face left off it; released, one cell;
  refusal - a corner only creates: a split that would leave a cell under
            the minimum shows no frame but the forbidden cursor, says why
            at each turn of the cursor, on the page and as an error, and
            splits nothing.

Needs the built viewer with its DOM bundle (build/wasm, `ninja -C
build/wasm`), node, PUPPETEER_PATH (a puppeteer-core install) and CHROME.
Skips rather than fails when any is missing.
"""
import functools
import http.server
import json
import os
import shutil
import subprocess
import threading
import time
import traceback

import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WASM = os.path.join(REPO, "build", "wasm")
DRIVER = os.path.join(REPO, "scripts", "splitview-drive.js")
RUN_WAIT_S = 240

state = {"done": False, "httpd": None, "run": None, "t0": time.monotonic()}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        if state["httpd"] is not None:
            state["httpd"].shutdown()
    except Exception:
        pass
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


def skip(why):
    note("SKIP " + why)
    finish()


class Run(threading.Thread):
    """The browser, driven from off the GUI thread; its log is a file,
    node buffers what it writes to a pipe."""

    def __init__(self, url):
        super().__init__(daemon=True)
        self.url = url
        self.error = None
        self.log_path = os.path.join(OUT, "drive.log")
        self.result_path = os.path.join(OUT, "drive-result.json")

    def run(self):
        try:
            node = os.environ.get("NODE") or shutil.which("node") or "node"
            with open(self.log_path, "w") as log:
                proc = subprocess.Popen([node, DRIVER, self.url, self.result_path],
                                        stdout=log, stderr=subprocess.STDOUT)
                proc.wait()
            if proc.returncode != 0:
                self.error = "node exited %d" % proc.returncode
        except Exception:
            self.error = traceback.format_exc()


def poll():
    run = state["run"]
    if run.is_alive():
        if time.monotonic() - state["t0"] > RUN_WAIT_S:
            check("the browser drive ends in %d s" % RUN_WAIT_S, False)
            finish()
            return
        QtCore.QTimer.singleShot(500, poll)
        return
    try:
        with open(run.result_path) as f:
            data = json.load(f)
    except Exception:
        data = {}
    results = data.get("results", [])
    if not check("the browser was driven and reported", bool(results),
                 run.error or "no result file; see drive.log"):
        finish()
        return
    for row in results:
        check(row.get("name", "?"), row.get("ok"), row.get("detail", "")[:600])
    finish()


def start():
    try:
        if not os.path.exists(os.path.join(WASM, "fcviewer.html")):
            skip("no built viewer at %s (ninja -C build/wasm)" % WASM)
            return
        if not os.path.exists(os.path.join(WASM, "web", "inspector.js")):
            skip("no DOM bundle at %s/web (the split view is drawn by it)" % WASM)
            return
        if not os.environ.get("PUPPETEER_PATH"):
            skip("PUPPETEER_PATH is unset (a puppeteer-core install)")
            return
        if not os.environ.get("CHROME"):
            skip("CHROME is unset (a Chrome executable)")
            return
        handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=WASM)
        handler.log_message = lambda *a, **k: None
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        state["httpd"] = server
        port = server.server_address[1]
        note("NOTE the viewer page on port %d" % port)
        state["run"] = Run("http://127.0.0.1:%d/fcviewer.html" % port)
        state["run"].start()
        QtCore.QTimer.singleShot(500, poll)
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
        finish()


QtCore.QTimer.singleShot(1500, start)
