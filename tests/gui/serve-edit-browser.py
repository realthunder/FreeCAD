"""A real browser enters a server-side edit mode, works in it, and leaves.

The browser half of docs/ThinClient.md sec 8.9 step 4.
tests/gui/serve-mirror-edit.py already checks the server side of this
against a synthetic socket client that speaks the wire itself -- the
ops, the binding to the connection's mirror, the 'E' replay, whose
selection a pick lands in. What no synthetic client can check is
src/Gui/Renderer/wasm/main.cpp: that a browser asks for the session at
all, that its pointer and keys become 'E' frames instead of orbiting its
own camera, and that it finds out when the session ends without it
asking.

So this serves a document with a sketch in it, serves the built viewer
over HTTP, and lets Chrome do the whole of it. Every reading is taken
HERE, in the serving process -- the document's own edit state, the
served graph, and the per-connection uplink counters
(Gui.serveClients()) -- except the two that are only answerable in the
page, which are what the VIEWER believed about the session and are
exactly the thing under test.

What is asserted:

  - the browser's own ask enters the session: the viewer sends the
    `edit` op, the document reports it is in edit, and the page's
    window.fcviewerEditing names the object. The camera goes with it:
    under the default uplink policy (sec 8.10b) a client that has not
    clicked has stated no camera, and the op is refused without one --
    so the viewer has to force one, and this is where that shows;
  - the session is bound to that connection's mirror, read as the served
    document having ZERO 3D views throughout. setEdit given no view does
    not refuse, it CREATES one, so a browser that had not been bound
    would leave a window and a GL context behind in this process;
  - entering edit takes the sketch's own geometry off this client's
    screen. In render-cache mode 3 -- what this runs in -- the sketch
    hands the editing root its own edit node and the edited occurrence is
    hidden per view, so the view provider's child count STAYS; in modes
    0-2 its children move under the editing root and the count goes to
    zero and comes back;
  - the edit geometry is drawn in the browser while the session runs and
    not before or after it: the serving source's overlay of the session's
    editing root, tagged with the session (8.12 item J), counted as
    magenta pixels -- the edited-edge colour this sets;
  - the pointer stream arrives: a drag and a hover across the canvas
    are counted as 'E' frames on this connection, and the hover half is
    the one that never travels in view mode (sec 8.2a stops at the edit
    boundary);
  - what the browser clicks while editing is its own and not the room's.
    The connection starts on the `none` selection route (sec 8.11a), and
    the session's selections are routed under its initiator's route --
    this browser's. That the click selected at all is read from the
    `selection` push it was told, so an empty room is the route working
    and not a click that missed;
  - and Escape, which the sketcher handles itself, ends the session AND
    the viewer is told: the server pushes the leaving edge to the client
    whose view it was, and window.fcviewerEditing goes back to null. A
    client that had to guess would go on sending its left button into a
    session that is not there.

Needs the built viewer (build/wasm/fcviewer.html; `ninja -C build/wasm`),
node, a puppeteer-core install named by PUPPETEER_PATH, and a Chrome
named by CHROME. Skips rather than fails when any is missing.

  PUPPETEER_PATH=/path/to/node_modules/puppeteer-core \
  CHROME=~/.cache/puppeteer/chrome/*/chrome-linux64/chrome \
  scripts/gui-test.sh tests/gui/serve-edit-browser.py /tmp/edit-web \
      --timeout 600

EDIT_REAL=1 runs it headful on the WSLg desktop (the real GPU) instead
of headless swiftshader; nothing here is judged by pixels, so either
tier answers the question.
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
from PySide import QtCore

from wsclient import free_port

clock = time.perf_counter

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
REPO = os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__))))
WASM = os.path.join(REPO, "build", "wasm")
DRIVER = os.path.join(REPO, "scripts", "edit-drive.js")
DOC = "ServeEditBrowser"
OBJ = "Sketch"
SETTLE_MS = 8000
RUN_WAIT_S = 300

state = {"doc": None, "port": 0, "http": 0, "done": False, "run": None,
         "seen_phases": 0, "samples": {}, "t0": clock()}


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
    """The browser, driven from off the GUI thread.

    Its log and its phase markers go to files rather than through a
    pipe: node buffers stdout to a pipe, so the markers -- which are
    when this side samples its counters -- would all arrive at the end
    and charge the whole run to the last phase.
    """

    def __init__(self, url):
        super().__init__(daemon=True)
        self.url = url
        self.error = None
        self.log_path = os.path.join(OUT, "drive.log")
        self.phase_path = os.path.join(OUT, "phases.txt")
        self.result_path = os.path.join(OUT, "drive-result.json")

    def run(self):
        try:
            env = dict(os.environ)
            # The bundled Chrome needs libasound from the conda env this
            # build runs against, and conda activation replaces
            # LD_LIBRARY_PATH before we get here (scripts/wasm-chrome.js).
            env["LD_LIBRARY_PATH"] = (
                os.path.join(REPO, ".conda", "freecad", "lib")
                + os.pathsep + env.get("LD_LIBRARY_PATH", ""))
            env["EDIT_PHASES"] = self.phase_path
            env["EDIT_RESULT"] = self.result_path
            if os.environ.get("EDIT_REAL"):
                # The real-GPU tier draws on the WSLg desktop, and this
                # harness runs under xvfb-run, which pointed DISPLAY at
                # its own headless server.
                env["DISPLAY"] = os.environ.get("EDIT_DISPLAY", ":0")
                env.pop("WAYLAND_DISPLAY", None)
            with open(self.log_path, "w") as log:
                proc = subprocess.Popen(
                    ["node", DRIVER, self.url, OBJ, str(SETTLE_MS)],
                    stdout=log, stderr=subprocess.STDOUT, env=env)
                proc.wait()
            if proc.returncode != 0:
                self.error = "node exited %d:\n%s" % (proc.returncode,
                                                      self.tail())
        except Exception:
            self.error = traceback.format_exc()

    def tail(self, lines=25):
        try:
            with open(self.log_path) as f:
                return "".join(f.readlines()[-lines:])
        except OSError:
            return "(no driver log)"

    def phases(self):
        try:
            with open(self.phase_path) as f:
                return [line.strip() for line in f if line.strip()]
        except OSError:
            return []

    def result(self):
        try:
            with open(self.result_path) as f:
                return jsonlib.load(f)
        except Exception:
            return {}


def serve_viewer():
    """The built viewer over HTTP, in this process -- one less child to
    leak, and it dies with us."""
    handler = functools.partial(http.server.SimpleHTTPRequestHandler,
                                directory=WASM)
    handler.log_message = lambda *a, **k: None
    port = free_port()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    state["httpd"] = server
    return port


def in_edit():
    return FreeCADGui.getDocument(DOC).getInEdit() is not None


def root_children():
    return state["doc"].getObject(OBJ).ViewObject.RootNode.getNumChildren()


def views_3d():
    """3D views this document has. Zero throughout, and that is the
    discriminating reading for the binding: setEdit given no view to
    bind to does not fail, it CREATES one."""
    return len(FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor"))


def room_selection():
    """What the ROOM has selected. FreeCADGui.Selection resolves to the
    current instance, and on the GUI thread outside any replayed event
    that is the room (docs/ThinClient.md sec 8.4)."""
    return sorted((s.ObjectName, tuple(s.SubElementNames))
                  for s in FreeCADGui.Selection.getSelectionEx(DOC))


def viewer_client():
    """This connection's counters, as the SERVER counted them. A client
    counting its own sends would be the client marking its own work."""
    rows = [c for c in FreeCADGui.serveClients() if c.get("viewer")]
    return rows[-1] if rows else None


def sample():
    return {"edit": in_edit(), "children": root_children(),
            "views": views_3d(), "room": room_selection(),
            "client": viewer_client()}


def build():
    try:
        import Part

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

        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        # The edited-edge colour, pure magenta: what the page counts to
        # see the edit overlay (scripts/edit-drive.js).
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetUnsigned(
            "EditedEdgeColor", 0xFF00FFFF)
        doc = FreeCAD.newDocument(DOC, hidden=True)
        state["doc"] = doc
        sketch = doc.addObject("Sketcher::SketchObject", OBJ)
        # The selection singleton's trace, read back from the Report view
        # at the end: which instance heard what, in order.
        ow = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/OutputWindow")
        for key in ("checkLogging", "checkWarning", "checkError", "checkMessage"):
            ow.SetBool(key, True)
        FreeCAD.setLogLevel("Selection", "Trace")
        sketch.addGeometry(Part.LineSegment(FreeCAD.Vector(-5, 0, 0),
                                            FreeCAD.Vector(5, 0, 0)), False)
        doc.recompute()
        state["children_before"] = root_children()

        port = free_port()
        state["port"] = port
        if not check("the document is served headless",
                     FreeCADGui.serveDocument(doc, port), "port %d" % port):
            finish()
            return
        state["http"] = serve_viewer()
        note("viewer on %d, scene on %d" % (state["http"], port))
        url = ("http://127.0.0.1:%d/fcviewer.html?scene=http://127.0.0.1:%d"
               % (state["http"], port))
        state["run"] = Run(url)
        state["t0"] = clock()
        state["run"].start()
        QtCore.QTimer.singleShot(50, poll)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def poll():
    run = state["run"]
    # Sampled as the browser marks its phases, and between them: the end
    # state alone would show only the last one, and every claim here is
    # about a transition.
    marked = run.phases()
    while len(marked) > state["seen_phases"]:
        name = marked[state["seen_phases"]]
        state["seen_phases"] += 1
        state["samples"][name] = sample()
    client = viewer_client()
    state.setdefault("trace", []).append(
        (clock() - state["t0"], in_edit(), root_children(), views_3d(),
         room_selection(), client["inputMsgs"] if client else 0,
         client["pickMsgs"] if client else 0))
    if run.is_alive():
        if clock() - state["t0"] > RUN_WAIT_S:
            check("the browser run finished", False,
                  "still running after %ds" % RUN_WAIT_S)
            note("---- driver log")
            note(run.tail(25))
            finish()
            return
        QtCore.QTimer.singleShot(50, poll)
        return
    # The connection is gone by now; the close handler runs on the GUI
    # thread, so give it a turn before reading what it left behind.
    QtCore.QTimer.singleShot(600, verify)


def verify():
    run = state["run"]
    try:
        marked = run.phases()
        said = run.result()
        trace = state.get("trace", [])
        note("phases: %s" % (marked,))
        note("trace (t, edit, children, views, room, inputMsgs, pickMsgs):")
        for row in trace:
            note("  %6.2f %s" % (row[0], row[1:]))
        note("the page said: %s" % (said,))
        if not check("the browser run had no error", run.error is None,
                     run.error or ""):
            note("---- driver log")
            note(run.tail(25))

        for phase in ("settled", "viewclick", "entered", "editclick", "drawn",
                      "escaped"):
            if phase not in state["samples"]:
                check("the run reached '%s'" % phase, False,
                      "phases: %s" % (marked,))
                note("---- driver log")
                note(run.tail(25))
                finish()
                return

        settled = state["samples"]["settled"]
        viewclick = state["samples"]["viewclick"]
        entered = state["samples"]["entered"]
        drawn = state["samples"]["drawn"]
        editclick = state["samples"]["editclick"]
        escaped = state["samples"]["escaped"]

        check("the browser connected as a viewer",
              settled["client"] is not None, settled["client"])

        # 1. A click in view mode is the client's own (8.11): the room
        # does not move. This is also the control for the reading in 5. Judged on the trace rather
        # than on the phase sample: a phase sample is taken when the GUI
        # thread next runs the poll, and entering an edit blocks it long
        # enough that two markers can be read in one pass and given the
        # same, later, reading.
        # Strictly the samples BEFORE the session started -- not every
        # sample that is not in edit, which also takes in the ones after
        # it ended, when the pointer stream has of course been counted
        # and the room has been left as leaving it left it.
        first_edit = next((i for i, s in enumerate(trace) if s[1]), len(trace))
        before_edit = trace[:first_edit]
        check("a click in view mode is the client's own: the room does not move",
              not any(s[4] for s in before_edit),
              [s[4] for s in before_edit[:60]])

        # 2. The browser's own ask entered the session -- on both sides:
        # the document is in edit, and the viewer believes it is.
        check("the browser asked for the edit and the document entered it",
              entered["edit"] is True, entered)
        check("and the viewer believes it is editing that object",
              said.get("entered") is True
              and said.get("editingAfterEnter") == OBJ,
              (said.get("entered"), said.get("editingAfterEnter")))

        # 2a. The edit geometry reaches the browser and leaves with the
        # session. It is the serving source's overlay of the session's
        # root, tagged with the session, and the viewer draws it once the
        # `edit` push has told it the session is its own (docs/
        # ThinClient.md 8.12 item J). Magenta is the edited-edge colour
        # set in build(); the sketch's own occurrence is hidden in the
        # session's views, so magenta is the overlay alone.
        check("no edit geometry is drawn in view mode",
              said.get("magentaView", 1 << 30) < 20, said.get("magentaView"))
        check("the edit overlay is drawn in the editing browser",
              said.get("magentaEdit", 0) > 100, said.get("magentaEdit"))
        check("and is gone once the session ends",
              said.get("magentaEscaped", 1 << 30) < 20,
              said.get("magentaEscaped"))

        # 3. The binding: no 3D view was ever created for this document.
        check("the session bound to the mirror rather than making a 3D view",
              all(s[3] == 0 for s in trace),
              sorted({s[3] for s in trace}))

        # 4. The sketch's own geometry during the edit. Mode 3 keeps it
        # where it is and hides the edited occurrence per view (the edit
        # draws its own node); modes 0-2 move it under the editing root.
        # serve-mirror-edit.py reads the same fork.
        before = state["children_before"]
        check("the sketch had a scene graph to begin with", before > 0, before)
        params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        if params.GetInt("RenderCache", 3) == 3:
            check("mode 3: entering edit kept the view provider's children",
                  any(s[1] for s in trace)
                  and all(s[2] == before for s in trace if s[1]),
                  [(s[1], s[2]) for s in trace[:80]])
        else:
            check("entering edit emptied the view provider's root",
                  any(s[1] and s[2] == 0 for s in trace),
                  [(s[1], s[2]) for s in trace[:80]])

        # 5. The pointer stream. Counted on this side, per connection:
        # the drag and the hover after it are 'E' frames, and in view
        # mode there were none at all.
        before_input = entered["client"]["inputMsgs"] if entered["client"] else 0
        after_input = drawn["client"]["inputMsgs"] if drawn["client"] else 0
        view_input = viewclick["client"]["inputMsgs"] if viewclick["client"] else 0
        moves = (said.get("gesture") or {}).get("moves", 0)
        check("no input event travels in view mode",
              all(s[5] == 0 for s in before_edit),
              [(round(s[0], 2), s[5]) for s in before_edit[-10:]])
        check("the pointer stream reached the server while editing",
              after_input - before_input >= 10,
              "%d frames for %d dispatched moves"
              % (after_input - before_input, moves))

        # 6. A click while editing lands in the session's instance, the
        # mirror's, and stays there: the session's selections are routed
        # under the initiator's route (sec 8.11a), and this browser -- the
        # initiator -- is on `none`, where a connection admitted below full
        # control starts. serve-selection-echo.py and serve-shared-edit.py
        # pin what the other routes forward.
        check("entering edit left the room selecting nothing",
              not entered["room"], entered["room"])
        route = (editclick["client"] or {}).get("selection")
        check("the browser is on the `none` selection route", route == "none",
              route)
        check("a click while editing stays out of the room (route none)",
              not editclick["room"]
              and not any(s[4] for s in trace if s[1]),
              editclick["room"])
        # And what the server told the browser, both times: its own
        # instance, whole, as a `selection` message the viewer handed to
        # the DOM layer.
        view_sel = said.get("viewSelection") or {}
        edit_sel = said.get("editSelection") or {}
        check("the view-mode click was told back to the browser",
              any(i.get("obj") == OBJ for i in view_sel.get("items", [])), view_sel)
        check("the in-edit click was told back to the browser",
              any(i.get("obj") == OBJ for i in edit_sel.get("items", [])), edit_sel)
        note("INFO selection messages the page saw: %s" % (said.get("selections"),))
        try:
            from PySide import QtWidgets
            mw = FreeCADGui.getMainWindow()
            for w in mw.findChildren(QtWidgets.QTextEdit) + mw.findChildren(QtWidgets.QPlainTextEdit):
                for line in w.toPlainText().splitlines():
                    if "Selection.cpp" in line:
                        note("INFO " + line[:160])
        except Exception:
            note("INFO report view unreadable: " + traceback.format_exc()[-200:])

        # 7. Escape: handled by the sketcher, so the client never asked.
        # Both halves -- the session ended, and the viewer was told.
        check("Escape from the browser ended the session",
              escaped["edit"] is False, escaped)
        check("and the server told the viewer the session was over",
              said.get("leftOnEscape") is True
              and not said.get("editingAfterEscape"),
              (said.get("leftOnEscape"), said.get("editingAfterEscape")))
        check("leaving edit gave the view provider its graph back",
              escaped["children"] >= before,
              (escaped["children"], before))
        check("no client's selection outlived its edit session",
              not room_selection(), room_selection())
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.getDocument(DOC).resetEdit()
    except Exception:
        pass
    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    try:
        state["httpd"].shutdown()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, build)
