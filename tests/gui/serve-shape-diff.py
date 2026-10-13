"""What the host looks at of a shape's two states stays the host's: the
clients of a served document are sent none of it.

docs/TransactionLog.md sec 32.24 (ruled 2026-10-13), which takes back what
sec 32.18 measured. The desktop's log panel and its merge dialog draw two
states of an object's shape over the object, as an annotation under its
view provider's root (PartGui::ShapeDiffView) -- the root every view of
the document draws and a served document publishes. So a client drew what
the host was looking at, unasked, and was sent a push for every move of
the host's slider. The annotation is marked the host's own to see now
(SoFCPathAnnotation::hostOnly): the views draw it, the snapshot a served
document publishes leaves it out, and it asks for no publish.

That a client DRAWS none of it is a browser's to say, and
tests/gui/serve-shape-diff-browser.py says it. This is what is still sent,
which a socket can count. A look set on the overlay's own views is
announced through the object they are views of, which the document reports
as the object changed, so a publish still goes out for it -- carrying the
object as it was. What changes no look of theirs sends nothing.

A document served with no window, one client on the socket that says hello
and counts the scene pushes it is sent. Asserted, each over a second and a
half:

  - nothing is pushed while nothing changes;
  - the overlay is shown on the host -- the view provider says what it
    found -- and no more than one push goes out for it, as for a place of
    the slider and for the two states by their material;
  - "differences only" and the overlay taken away, which were pushed
    because the view asked, push nothing;
  - a change of the document is pushed still;
  - none of it but that change wrote a row of the log or an undo step.

The same two states shown as an edit (sec 32.19) are the edit session's
and do reach the clients in it: tests/gui/serve-shape-diff-edit.py.

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

        def taller():
            doc.openTransaction("taller still")
            box.Height = 20
            doc.recompute()
            doc.commitTransaction()
            state["changed"] = (len(doc.getTransactionLog()), doc.UndoCount, doc.RedoCount)
            return True

        def found():
            return {k: v for k, v in dict(view.getValueDiff()).items() if "." not in k}

        # (what, the call, the pushes there may be: None is one or more)
        steps = [
            ("nothing changes", lambda: None, 0),
            ("the overlay shown", lambda: view.showValueDiff("Shape", a, b) and found(), 1),
            ("the slider half way", lambda: view.showValueDiff("Shape", a, b, position=0.5), 1),
            ("the slider at the earlier state",
             lambda: view.showValueDiff("Shape", a, b, position=0.0), 1),
            ("differences only",
             lambda: view.showValueDiff("Shape", a, b, position=0.0, differencesOnly=True), 0),
            ("the two by their material",
             lambda: view.showValueDiff("Shape", a, b, mode="material"), 1),
            ("the overlay taken away", lambda: view.hideValueDiff(), 0),
            ("nothing changes after it", lambda: None, 0),
            ("a change of the document", taller, None),
        ]

        def run(i):
            if client.error:
                check("the client ran", False, client.error)
                finish()
                return
            if i >= len(steps):
                verify()
                return
            what, call, most = steps[i]
            before = (counts["pushes"], counts["bytes"])
            if most is None:
                state["written_before_change"] = (len(doc.getTransactionLog()), doc.UndoCount,
                                                  doc.RedoCount)
            shown = None
            try:
                shown = call()
            except Exception:
                check(what, False, traceback.format_exc())

            def read():
                got = counts["pushes"] - before[0]
                size = counts["bytes"] - before[1]
                said = "%d pushes, %d bytes, the call said %s" % (got, size, shown)
                if most is None:
                    check("%s is pushed to the client" % what, got >= 1 and shown is not False,
                          said)
                elif most == 0:
                    check("%s: nothing is pushed" % what, got == 0 and shown is not False, said)
                else:
                    check("%s: on the host, and no more than one push goes out" % what,
                          got <= most and bool(shown), said)
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
    before = state.get("written_before_change")
    check("none of it but the change wrote a row of the log or an undo step",
          before == state["written"]
          and (len(doc.getTransactionLog()), doc.UndoCount, doc.RedoCount)
          == state.get("changed"),
          "%s, were %s; after the change %s" % (before, state["written"], state.get("changed")))
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
