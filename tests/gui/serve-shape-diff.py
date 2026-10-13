"""A shape diff shown on a served document reaches its clients, and leaves them.

docs/TransactionLog.md sec 32.18. Two states of an object's shape are drawn
over the object as an annotation under its view provider's root
(PartGui::ShapeDiffView), which is in the scene a served document
publishes: a client draws what the host is shown. The serving source
publishes when it hears of a change, and it hears by the document's
properties. Showing the overlay changes some -- its own views' -- so that
was published. Taking it away changes none: a client connected at the time
was sent nothing and went on drawing the two states over the object until
something else in the document changed. The view asks for a publish now
when what it draws is set and when it goes.

A document served with no window, one client on the socket that says hello
and counts the scene pushes it is sent. Asserted, each over a second and a
half:

  - nothing is pushed while nothing changes;
  - the overlay shown is pushed, and each place of the slider after it;
  - "differences only" is pushed;
  - the two states by their material is pushed;
  - the overlay taken away is pushed -- and nothing after it;
  - none of it wrote a row of the log or an undo step.

Scored against the tree before the change: the overlay taken away pushes
nothing.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set and this
directory on PYTHONPATH.
"""
import os
import tempfile
import threading
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

from wsclient import WS, free_port

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ServeShapeDiff"
STEP_MS = 1500
state = {"done": False}
counts = {"pushes": 0, "bytes": 0}
finished = threading.Event()


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


class Client(threading.Thread):
    """Says hello and counts the scene pushes."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.ready = threading.Event()

    def run(self):
        try:
            ws = WS(self.port)
            ws.hello("serve-shape-diff")
            if ws.next_binary(20.0) is None:
                raise RuntimeError("no snapshot")
            ws.drain(1.0)
            self.ready.set()
            while not finished.is_set():
                data = ws.next_binary(0.2)
                if data is not None:
                    counts["pushes"] += 1
                    counts["bytes"] += len(data)
            ws.close()
        except Exception:
            self.error = traceback.format_exc()
            self.ready.set()


def build():
    try:
        p = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
        state["log"] = p.GetInt("TransactionLog", 2)
        p.SetInt("TransactionLog", 2)
        p.SetInt("AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
            "Type", "bgfx - OpenGL")
        doc = FreeCAD.newDocument(DOC, hidden=True)
        state["doc"] = doc
        doc.UndoMode = 1
        doc.openTransaction("base")
        box = doc.addObject("Part::Box", "Box")
        cyl = doc.addObject("Part::Cylinder", "Cyl")
        cyl.Radius, cyl.Height = 2, 30
        cyl.Placement.Base = FreeCAD.Vector(5, 5, -10)
        cut = doc.addObject("Part::Cut", "Cut")
        cut.Base, cut.Tool = box, cyl
        doc.recompute()
        doc.commitTransaction()
        doc.saveAs(os.path.join(tempfile.mkdtemp(prefix="fc-serve-sd-"), "sd.FCStd"))
        doc.openTransaction("taller")
        box.Height = 16
        doc.recompute()
        doc.commitTransaction()
        row = [r for r in doc.getTransactionLog() if r["name"] == "taller"][0]
        op = [o for o in doc.getTransactionOps(row["seq"])
              if o["prop"] == "Shape" and o["cid"] == cut.ID][0]
        a, b = op["before"], op["after"]
        view = cut.ViewObject

        port = free_port()
        if not check("the document is served with no window",
                     FreeCADGui.serveDocument(doc, port), "port %d" % port):
            finish()
            return
        client = Client(port)
        state["client"] = client
        client.start()

        # (what, the call, whether a push is expected)
        steps = [
            ("nothing changes", lambda: None, False),
            ("the overlay shown", lambda: view.showValueDiff("Shape", a, b), True),
            ("the slider half way", lambda: view.showValueDiff("Shape", a, b, position=0.5), True),
            ("the slider at the earlier state",
             lambda: view.showValueDiff("Shape", a, b, position=0.0), True),
            ("differences only",
             lambda: view.showValueDiff("Shape", a, b, position=0.0, differencesOnly=True), True),
            ("the two by their material",
             lambda: view.showValueDiff("Shape", a, b, mode="material"), True),
            ("the overlay taken away", lambda: view.hideValueDiff(), True),
            ("nothing changes after it", lambda: None, False),
        ]

        def run(i):
            if client.error:
                check("the client ran", False, client.error)
                finish()
                return
            if i >= len(steps):
                verify()
                return
            what, call, pushed = steps[i]
            before = counts["pushes"]
            shown = None
            try:
                shown = call()
            except Exception:
                check(what, False, traceback.format_exc())

            def read():
                got = counts["pushes"] - before
                if pushed:
                    check("%s is pushed to the client" % what, got >= 1 and shown is not False,
                          "%d pushes, the call said %s" % (got, shown))
                else:
                    check("%s: nothing is pushed" % what, got == 0, "%d pushes" % got)
                run(i + 1)

            QtCore.QTimer.singleShot(STEP_MS, read)

        def start():
            if not client.ready.wait(0):
                if time.perf_counter() - state["t0"] > 60:
                    check("the client connected", False, client.error or "not in 60 s")
                    finish()
                    return
                QtCore.QTimer.singleShot(100, start)
                return
            # After the client is in: its coming is a row of its own (login)
            state["written"] = (len(doc.getTransactionLog()), doc.UndoCount, doc.RedoCount)
            run(0)

        state["t0"] = time.perf_counter()
        QtCore.QTimer.singleShot(100, start)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
        finish()


def verify():
    doc = state["doc"]
    now = (len(doc.getTransactionLog()), doc.UndoCount, doc.RedoCount)
    rows = doc.getTransactionLog()[state["written"][0]:]
    check("none of it wrote a row of the log or an undo step", now == state["written"],
          "%s, were %s: %s" % (now, state["written"], [
              (r.get("name"), r.get("kind"), [(o.get("object"), o.get("prop"))
                                               for o in doc.getTransactionOps(r["seq"])])
              for r in rows]))
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    finished.set()
    try:
        if "log" in state:
            FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
                "TransactionLog", state["log"])
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, build)
