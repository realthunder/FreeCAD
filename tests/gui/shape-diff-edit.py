"""An object's last change is shown as an editing task, its two states in the
edit session's root.

docs/TransactionLog.md sec 32.19. The two states of a shape were drawn under
the object's root, in the document's scene: every view of the document had
them, and every client of a served one. Shown as the last change of an object
they are an edit mode of its view provider (Gui::ViewProvider::ValueDiff,
6): the row is the newest of the log that wrote the shape, the two states
hang in the session's editing root as a session node, and the panel of the
task says which change it is and has the mode, the slider, "Differences
only" and what was found.

A document with a 3D window: a box with a hole, made taller in a row of its
own. Asserted:

  - outside the edit, the two states asked of the view provider hang under
    the object's root, as before;
  - the edit is entered by its mode; what hung under the object's root is
    gone, and the two states are under the session's editing root, in the
    window's graph;
  - the task's panel is up: which row, and what became of the faces -- the
    bottom as it was, six changed;
  - the slider, "Differences only" and the mode are the view provider's
    options; by the material the volume added is said;
  - the panel as a client is sent it (the panel mirror) has the slider, and
    a client's write to it moves the real one and the two states;
  - leaving -- by the document, and by the panel's Close -- takes the two
    states and the panel away;
  - an object made and never changed since: its last change is its making,
    and all of it is new;
  - none of it wrote a row of the log or an undo step.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set.
"""
import json
import os
import tempfile
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ShapeDiffEdit"
MODE = 6  # Gui::ViewProvider::ValueDiff
CLIENT = 7
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    end = time.monotonic() + seconds
    while True:
        QtCore.QCoreApplication.processEvents()
        if time.monotonic() >= end:
            break
        time.sleep(0.01)


def gdoc():
    return FreeCADGui.getDocument(DOC)


def named(node, name):
    """How many nodes called `name` are under `node`."""
    from pivy import coin

    if node is None:
        return -1
    search = coin.SoSearchAction()
    search.setName(name)
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(node)
    return search.getPaths().getLength()


def graph():
    views = gdoc().mdiViewsOfType("Gui::View3DInventor")
    return views[0].getViewer().getSoRenderManager().getSceneGraph()


def editing_root():
    from pivy import coin

    search = coin.SoSearchAction()
    search.setName("EditingRoot")
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(graph())
    paths = search.getPaths()
    return paths[0].getTail() if paths.getLength() else None


def where(obj):
    """(under the object's root, under the editing root, in the window's graph)"""
    root = editing_root()
    return (named(obj.ViewObject.RootNode, "ShapeDiff"),
            named(root, "ShapeDiff") if root is not None else 0,
            named(graph(), "ShapeDiff"))


def part(name, kind=QtWidgets.QWidget):
    """A widget of the task's panel. By the panel and not by the main
    window: the log's own panel has a bar with parts of the same names."""
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "PartGui::ShapeDiffPanel":
            return w.findChild(kind, name)
    return None


def written(doc):
    return (len(doc.getTransactionLog()), doc.UndoCount, doc.RedoCount)


def run():
    try:
        p = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
        state["log"] = p.GetInt("TransactionLog", 2)
        p.SetInt("TransactionLog", 2)
        p.SetInt("AutoSaveTimeout", 0)
        doc = FreeCAD.newDocument(DOC)
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
        doc.saveAs(os.path.join(tempfile.mkdtemp(prefix="fc-sd-edit-"), "sd.FCStd"))
        doc.openTransaction("taller")
        box.Height = 16
        doc.recompute()
        doc.commitTransaction()
        settle(0.5)
        view = cut.ViewObject
        row = [r for r in doc.getTransactionLog() if r["name"] == "taller"][0]
        op = [o for o in doc.getTransactionOps(row["seq"])
              if o["prop"] == "Shape" and o["cid"] == cut.ID][0]
        before = written(doc)

        # Outside the edit, as it was
        view.showValueDiff("Shape", op["before"], op["after"])
        check("asked outside an edit, the two states hang under the object's root",
              where(cut)[0] == 1, where(cut))

        check("the last change is entered as an edit, by its mode",
              gdoc().setEdit(cut.Name, MODE) is True and gdoc().getInEdit() is not None,
              gdoc().getInEdit())
        settle(0.5)
        at = where(cut)
        check("what hung under the object's root is gone", at[0] == 0, at)
        check("the two states are under the session's editing root, in the window's graph",
              at[1] == 1 and at[2] == 1, at)
        found = view.getValueDiff()
        check("of the row that changed the shape last: the bottom as it was, six changed",
              found.get("same") == 1 and found.get("changed", 0) + found.get("moved", 0) == 6,
              {k: v for k, v in found.items() if "." not in k})

        what = part("diffWhat", QtWidgets.QLabel)
        said = part("diffFound", QtWidgets.QLabel)
        check("the task's panel is up, and says which row",
              bool(FreeCADGui.Control.activeDialog()) and what is not None
              and "taller" in what.text(), what.text() if what else None)
        check("and what became of the faces",
              said is not None and "1 as they were, 6 changed" in said.text(),
              said.text() if said else None)

        slider = part("diffPosition", QtWidgets.QSlider)
        only = part("diffOnly", QtWidgets.QCheckBox)
        mode = part("diffMode", QtWidgets.QComboBox)
        slider.setValue(25)
        settle(0.2)
        check("the slider is where between the two", view.getValueDiff().get("position") == 25,
              view.getValueDiff().get("position"))
        only.setChecked(True)
        settle(0.2)
        check("differences only leaves out what did not change",
              view.getValueDiff().get("differencesOnly") == 1
              and view.getValueDiff().get("shown.same") == 0,
              (view.getValueDiff().get("differencesOnly"), view.getValueDiff().get("shown.same")))
        only.setChecked(False)
        mode.setCurrentIndex(1)
        end = time.monotonic() + 30
        while view.getValueDiff().get("material.pending") and time.monotonic() < end:
            settle(0.2)
        settle(0.5)
        found = view.getValueDiff()
        check("by the material: what the taller box added, nothing removed",
              found.get("material") == 1 and abs(found.get("volume.added", 0) - (600 - 24 * 3.14159265))
              < 1.0 and found.get("volume.removed", 1) < 1e-6,
              (found.get("material"), found.get("volume.added"), found.get("volume.removed")))
        check("said in the panel", "added" in said.text(), said.text())
        at = where(cut)
        check("still the session's and not the document's", at[0] == 0 and at[1] == 1, at)
        mode.setCurrentIndex(0)
        settle(0.3)

        # The panel as a client is sent it
        fw = FreeCADGui.FormWidgets
        reply = json.loads(fw.control(json.dumps(
            {"id": 1, "op": "widgets.subscribe", "panels": True}), CLIENT))
        settle(0.6)
        mirrored = None
        for wid in fw.ids():
            if not wid.startswith("pw:"):
                continue
            snap = fw.snapshot(wid)
            if snap["state"].get("q_objectName") == "diffPosition":
                mirrored = (wid, snap)
        check("the panel mirrored to a client has the slider",
              reply.get("ok") and mirrored is not None
              and mirrored[1]["model"] == "QSliderModel"
              and mirrored[1]["state"].get("q_value") == 25,
              (reply.get("ok"), mirrored[1]["model"] if mirrored else None,
               mirrored[1]["state"].get("q_value") if mirrored else None))
        if mirrored:
            reply = json.loads(fw.control(json.dumps(
                {"id": 2, "op": "widgets.update", "target": mirrored[0],
                 "state": {"q_value": 80}}), CLIENT))
            settle(0.4)
            check("a client's write to it moves the real one, and the two states",
                  reply.get("ok") and slider.value() == 80
                  and view.getValueDiff().get("position") == 80,
                  (reply, slider.value(), view.getValueDiff().get("position")))
        fw.control(json.dumps({"id": 3, "op": "widgets.unsubscribe"}), CLIENT)
        settle(0.2)

        gdoc().resetEdit()
        settle(0.5)
        at = where(cut)
        check("left by the document: no edit, no panel, nothing of the two states left",
              gdoc().getInEdit() is None and not FreeCADGui.Control.activeDialog()
              and at[2] == 0 and view.getValueDiff() == {},
              (gdoc().getInEdit(), FreeCADGui.Control.activeDialog(), at))

        gdoc().setEdit(cut.Name, MODE)
        settle(0.5)
        check("entered again", where(cut)[1] == 1 and part("diffPosition") is not None, where(cut))
        close = None
        for box_ in FreeCADGui.getMainWindow().findChildren(QtWidgets.QDialogButtonBox):
            b = box_.button(QtWidgets.QDialogButtonBox.Close)
            if b is not None and b.isVisible():
                close = b
        if check("the panel has a Close", close is not None):
            close.click()
            settle(0.6)
            at = where(cut)
            check("left by the panel's Close: no edit, no panel, nothing left",
                  gdoc().getInEdit() is None and not FreeCADGui.Control.activeDialog()
                  and at[2] == 0, (gdoc().getInEdit(), FreeCADGui.Control.activeDialog(), at))

        # What was made with its shape and never changed: all of it is new
        gdoc().setEdit(cyl.Name, MODE)
        settle(0.5)
        found = cyl.ViewObject.getValueDiff()
        check("an object made and never changed: its last change is its making, all new",
              gdoc().getInEdit() is not None and found.get("added") == 3
              and found.get("same", 0) + found.get("changed", 0) == 0,
              {k: v for k, v in found.items() if "." not in k})
        gdoc().resetEdit()
        settle(0.4)

        check("none of it wrote a row of the log or an undo step", written(doc) == before,
              "%s, were %s" % (written(doc), before))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
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


QtCore.QTimer.singleShot(1500, run)
