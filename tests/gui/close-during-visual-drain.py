"""A document closed while its load is still building visuals.

A progressive load parks every Part visual and builds them afterwards in
slices on the GUI thread (docs/DocumentLoad.md sec 13). A slice says how it
is going through a progress sequence, and the progress bar pumps events
from inside the slice. An event run there can close the document: a timer
of a script or a macro, a test. The close used to erase the slice's own
queue under it and destroy the progress sequence it was calling into, and
the slice went on with an iterator into the map node that was gone --
a segmentation fault in one of three places, or the GUI thread spinning in
the map walk for good (docs/DocumentLoad.md sec 18.8). The user's own
ways to close are not acted on while the progress bar runs; a script's are.

What is done: a document of some hundred solids is made and saved; then,
several times over in one process, it is opened and closed again from a
timer a few tens of milliseconds after the drain has handed its shapes to
the pre-mesh -- a different delay each time, so that the close lands in
different places of the drain -- and once closed and reopened in the same
breath. After all of it the document is opened and left to finish.

What is asserted:
  - the drain was running when the closes came (the test read the drain's
    own "parked shapes submitted" line each time), so the closes were
    closes during a load;
  - the process lived, and came back to its event loop each time (a hang
    ends in the driver's timeout, with no DONE);
  - no document is left over after each close;
  - a load after all of that still finishes: every object's visual is
    built and has a bounding box.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout). Scored against the tree before the change (b57f74c576): of six
runs of the close alone, four died in the drain and one hung.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PATH = os.path.join(OUT, "drain.FCStd")
COUNT = 600
DELAYS = [0, 20, 40, 70, 100, 140, 190, 250]

state = {"done": False, "cycle": -1, "armed": False, "seen": 0, "closed": 0, "left": 0,
         "reopen": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def observe(notifier, msg, level):
    """The drain's own line: its first working slice has run."""
    if state["armed"] and "parked shapes submitted" in msg:
        state["armed"] = False
        state["seen"] += 1
        delay = DELAYS[state["cycle"] % len(DELAYS)]
        QtCore.QTimer.singleShot(delay, close_now)


def close_now():
    try:
        for name in list(FreeCAD.listDocuments().keys()):
            FreeCAD.closeDocument(name)
        state["closed"] += 1
        state["left"] += len(FreeCAD.listDocuments())
        if state["reopen"]:
            # In the same breath: the same name is a new document's now
            FreeCAD.openDocument(PATH)
    except Exception:
        note("ABORT closing:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(1500, next_cycle)


def next_cycle():
    if state["done"]:
        return
    try:
        for name in list(FreeCAD.listDocuments().keys()):
            FreeCAD.closeDocument(name)
        state["cycle"] += 1
        if state["cycle"] > len(DELAYS):
            QtCore.QTimer.singleShot(200, last_load)
            return
        # The cycle past the delays is the close-and-reopen
        state["reopen"] = state["cycle"] == len(DELAYS)
        state["armed"] = True
        FreeCAD.openDocument(PATH)
        # A load whose line never came is not left waiting
        cycle = state["cycle"]
        QtCore.QTimer.singleShot(15000, lambda: unseen(cycle))
    except Exception:
        note("ABORT cycle %d:\n%s" % (state["cycle"], traceback.format_exc()))
        finish()


def unseen(cycle):
    if state["armed"] and state["cycle"] == cycle and not state["done"]:
        state["armed"] = False
        note("INFO cycle %d: the drain's line was not seen" % cycle)
        close_now()


def last_load():
    try:
        doc = FreeCAD.openDocument(PATH)
        state["last"] = doc.Name
    except Exception:
        note("ABORT last load:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(9000, verdict)


def verdict():
    try:
        cycles = len(DELAYS) + 1
        check("the drain was running at every close",
              state["seen"] == cycles, "%d of %d" % (state["seen"], cycles))
        check("the process lived through every close and came back to its event loop",
              state["closed"] == cycles, "%d of %d" % (state["closed"], cycles))
        check("no document was left over by a close", state["left"] == 0, state["left"])
        doc = FreeCAD.getDocument(state["last"])
        built = 0
        for obj in doc.Objects:
            box = obj.ViewObject.getBoundingBox()
            if box.isValid() and box.DiagonalLength > 0:
                built += 1
        check("a load after all of it finishes: every visual is built",
              len(doc.Objects) == COUNT and built == COUNT,
              "%d of %d objects, %d built" % (len(doc.Objects), COUNT, built))
    except Exception:
        note("ABORT verdict:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


def build():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        view.SetBool("ShowNaviCube", False)
        view.SetBool("UseNavigationAnimations", False)
        # The line the closes are timed by is a log line of the Part module
        FreeCAD.setLogLevel("Part", "Log")
        FreeCAD.Console.AttachObserver(observe)
        doc = FreeCAD.newDocument("Drain")
        for i in range(COUNT):
            kind = i % 3
            if kind == 0:
                obj = doc.addObject("Part::Torus", "T%d" % i)
                obj.Radius1 = 20 + (i % 7)
                obj.Radius2 = 3 + 0.01 * i
            elif kind == 1:
                obj = doc.addObject("Part::Sphere", "S%d" % i)
                obj.Radius = 8 + 0.01 * i
            else:
                obj = doc.addObject("Part::Cylinder", "C%d" % i)
                obj.Radius = 6 + 0.01 * i
                obj.Height = 30
            obj.Placement.Base = FreeCAD.Vector(70 * (i % 25), 70 * (i // 25), 0)
        doc.recompute()
        doc.saveAs(PATH)
        FreeCAD.closeDocument(doc.Name)
        note("INFO %d objects saved" % COUNT)
    except Exception:
        note("ABORT build:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(1000, next_cycle)


QtCore.QTimer.singleShot(1500, build)
