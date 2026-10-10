"""A document closed while its load is still building view providers.

A progressive load returns from the open with the view providers still to
be built, and builds them afterwards in slices on the GUI thread
(Gui::Document::runDeferredRestoreSlice). A slice says how it is going
through a progress sequence, and the progress bar runs events from inside
the slice. An event run there can close the document: a timer of a script
or a macro, a test. The close deleted the Gui document whose member
function the slice is, and the App document after it, and the slice went
on: into a reader that was null, a view that was gone, freed memory
(docs/HandsOnLog.md, entry 47: 5 closes of 6 on one branch, 1 of 12 on
another, by where the timer happened to fall). tests/gui/
close-during-visual-drain.py is the same for the drain that follows this
one, Part's.

Now such a close goes ahead -- the views, the tree, the application's
list -- and the two documents are deleted when the slice has unwound.

What is done: a document of some hundred solids is made and saved; then,
several times over in one process, it is opened, and closed from a timer
that fires INSIDE a slice: the timer runs every millisecond and closes the
first time it finds the document restoring -- which, after the open has
returned, it is only while a slice of the drain is on the stack. (The bar
runs events once in a fifth of a second, and this drain is through in a
few of those: there is no choosing a later one.) Once it is closed and
reopened in the same breath. After all of it the document is opened and
left to finish.

What is asserted:
  - every close came from inside a slice (the document was restoring when
    the timer closed it; a load that gave the timer no such moment is done
    again, up to six times in all);
  - the process lived, and came back to its event loop each time (a hang
    ends in the driver's timeout, with no DONE);
  - no document is left over after each close;
  - each closed document is deleted soon after: its Python object no
    longer answers;
  - a load after all of that still finishes: every object has its view
    provider, built.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout). Scored against the tree before the change (a810955c86): no check
reached, the process dead at the first close.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PATH = os.path.join(OUT, "drain.FCStd")
COUNT = 1500
# Loads closed from inside a slice, and one more closed and reopened there
CLOSES = 4

state = {"done": False, "cycle": -1, "armed": False, "inside": 0, "closed": 0, "left": 0,
         "reopen": False, "old": [], "gone": 0, "attempt": 0, "retries": 0}

WATCH = QtCore.QTimer()
WATCH.setInterval(1)


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def watch():
    """Every millisecond: between two slices, and from inside one when the bar runs events."""
    if not state["armed"]:
        return
    doc = FreeCAD.ActiveDocument
    if doc is None or not doc.Restoring:
        return
    state["armed"] = False
    state["inside"] += 1
    close_now(doc)


WATCH.timeout.connect(watch)


def close_now(doc=None):
    try:
        if doc is not None:
            state["old"].append(doc)
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
    QtCore.QTimer.singleShot(2500, next_cycle)


def deleted(doc):
    try:
        doc.Name
    except ReferenceError:
        return True
    except Exception:
        return True
    return False


def next_cycle():
    if state["done"]:
        return
    try:
        # The document closed in the cycle before has had its slice unwind
        state["gone"] = sum(1 for doc in state["old"] if deleted(doc))
        for name in list(FreeCAD.listDocuments().keys()):
            FreeCAD.closeDocument(name)
        state["cycle"] += 1
        if state["cycle"] > CLOSES:
            QtCore.QTimer.singleShot(200, last_load)
            return
        # The last cycle is the close-and-reopen
        state["reopen"] = state["cycle"] == CLOSES
        FreeCAD.openDocument(PATH)
        # From here on a restoring document is a slice on the stack
        state["armed"] = True
        state["attempt"] += 1
        attempt = state["attempt"]
        QtCore.QTimer.singleShot(8000, lambda: unseen(attempt))
    except Exception:
        note("ABORT cycle %d:\n%s" % (state["cycle"], traceback.format_exc()))
        finish()


def unseen(attempt):
    """A load whose drain went through with the bar never running events inside a slice: the
    fifth of a second can fall between two of them. Not a close of the kind wanted; the cycle is
    done again."""
    if not state["armed"] or state["attempt"] != attempt or state["done"]:
        return
    state["armed"] = False
    state["retries"] += 1
    note("INFO cycle %d: the timer was never inside a slice, again (%d)"
         % (state["cycle"], state["retries"]))
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    if state["retries"] <= 6:
        state["cycle"] -= 1
    QtCore.QTimer.singleShot(500, next_cycle)


def last_load():
    try:
        WATCH.stop()
        doc = FreeCAD.openDocument(PATH)
        state["last"] = doc.Name
    except Exception:
        note("ABORT last load:\n" + traceback.format_exc())
        finish()
        return
    QtCore.QTimer.singleShot(15000, verdict)


def verdict():
    try:
        cycles = CLOSES + 1
        check("every close came from inside a slice of the drain",
              state["inside"] == cycles, "%d of %d" % (state["inside"], cycles))
        check("the process lived through every close and came back to its event loop",
              state["closed"] == cycles, "%d of %d" % (state["closed"], cycles))
        check("no document was left over by a close", state["left"] == 0, state["left"])
        check("each closed document was deleted once its slice had unwound",
              state["gone"] == len(state["old"]) and state["gone"] == state["inside"],
              "%d of %d" % (state["gone"], len(state["old"])))
        doc = FreeCAD.getDocument(state["last"])
        built = 0
        for obj in doc.Objects:
            view = obj.ViewObject
            if view is None:
                continue
            box = view.getBoundingBox()
            if box.isValid() and box.DiagonalLength > 0:
                built += 1
        check("a load after all of it finishes: every view provider is there and built",
              len(doc.Objects) == COUNT and built == COUNT,
              "%d of %d objects, %d built" % (len(doc.Objects), COUNT, built))
    except Exception:
        note("ABORT verdict:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    WATCH.stop()
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
        doc = FreeCAD.newDocument("Drain")
        for i in range(COUNT):
            kind = i % 3
            if kind == 0:
                obj = doc.addObject("Part::Box", "B%d" % i)
                obj.Length = 20 + (i % 7)
            elif kind == 1:
                obj = doc.addObject("Part::Sphere", "S%d" % i)
                obj.Radius = 8 + 0.01 * i
            else:
                obj = doc.addObject("Part::Cylinder", "C%d" % i)
                obj.Radius = 6 + 0.01 * i
                obj.Height = 30
            obj.Placement.Base = FreeCAD.Vector(70 * (i % 40), 70 * (i // 40), 0)
        doc.recompute()
        doc.saveAs(PATH)
        FreeCAD.closeDocument(doc.Name)
        note("INFO %d objects saved" % COUNT)
    except Exception:
        note("ABORT build:\n" + traceback.format_exc())
        finish()


def start():
    build()
    if not state["done"]:
        WATCH.start()
        QtCore.QTimer.singleShot(300, next_cycle)


QtCore.QTimer.singleShot(1000, start)
