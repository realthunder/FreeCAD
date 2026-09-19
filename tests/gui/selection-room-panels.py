"""A desktop selection still reaches the panels that observe it.

The oracle for docs/ThinClient.md section 8.4. Gui::Selection() used to
be one static object; it is now the innermost open Gui::SelectionScope's
instance, falling back to the room. Nothing opens a scope yet, so on the
desktop the current instance IS the room and this test must pass exactly
as it did before the stack existed -- that is the whole claim of stage 2
of 8.9, and this is where it is checked against real panels rather than
against the class.

It earns its keep at stage 3, when a mirror's replayed pick starts
running under a scope: the same assertions then say that the mirror's
own picks do NOT land here, and only what a mirror commits into the room
does.

What is asserted:
  - a selection made through Gui::Selection reaches the tree view (the
    document object's item is selected) and the property editor (it
    shows the object's properties), both of them SelectionObservers now
    pinned to the room instance;
  - clearing the selection reaches them too, so what is being watched is
    the notification and not a one-shot initial paint;
  - and the process lived to write DONE.

The property editor is judged by what it shows, not by how much: with
nothing selected it does not go blank, it falls back to the properties
of the document the tree has selected (PropertyView::onTimer). So the
marker is a property only the object has. It also answers on a timer
rather than on the signal, which is what the pump is for.

The panels that observe through Base::Subject::Attach rather than
through SelectionObserver -- nine task panels and dialogs -- were
retargeted at the room in the same change. They are not opened here:
while no scope is open the two accessors return the same object, so
there is nothing a desktop run could tell apart. They need a mirror,
which is stage 3.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SelectionRoomPanels"
OBJ = "Box"
# A property the box has and the document does not, so that showing it
# means the editor followed the selection rather than fell back.
MARKER = "Length"
TREE_WAIT_S = 60

state = {"doc": None, "done": False, "t0": time.monotonic()}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def pump(turns=20):
    """Let the observers run: a selection change is delivered on the
    signal, but the panels repaint from the event loop."""
    for _ in range(turns):
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def document_trees():
    mw = FreeCADGui.getMainWindow()
    return [t for t in mw.findChildren(QtWidgets.QTreeWidget)
            if t.metaObject().className() == "Gui::TreeWidget"]


def walk(model, parent=QtCore.QModelIndex()):
    for row in range(model.rowCount(parent)):
        index = model.index(row, 0, parent)
        yield index
        for deeper in walk(model, index):
            yield deeper


def tree_labels():
    """Every label the tree is showing. Read through the model rather
    than through QTreeWidgetItem: the tree destroys and rebuilds its
    items as the document changes, and a Python wrapper that outlives
    one of them raises on the next touch."""
    labels = []
    for tree in document_trees():
        model = tree.model()
        if model is not None:
            labels += [index.data() for index in walk(model)]
    return labels


def tree_selected():
    """Labels of the rows the tree has selected."""
    labels = []
    for tree in document_trees():
        picker = tree.selectionModel()
        if picker is None:
            continue
        labels += [i.data() for i in picker.selectedIndexes() if i.column() == 0]
    return labels


def property_labels():
    """Property names the property editor is showing, over every editor
    in the window (there is one per combo view, data and view)."""
    mw = FreeCADGui.getMainWindow()
    labels = []
    for view in mw.findChildren(QtWidgets.QTreeView):
        if "PropertyEditor" not in view.metaObject().className():
            continue
        model = view.model()
        if model is not None:
            labels += [index.data() for index in walk(model)]
    return labels


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    # Every document closed before the quit: a modified one left open
    # puts a save prompt in the way and the process never leaves.
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


def run():
    try:
        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        doc.addObject("Part::Box", OBJ)
        doc.recompute()
        pump()
        wait_tree()
    except Exception:
        note("ABORT setup:\n" + traceback.format_exc())
        finish()


def wait_tree():
    if OBJ not in tree_labels():
        if time.monotonic() - state["t0"] > TREE_WAIT_S:
            check("the tree itemised the object", False, tree_labels())
            finish()
            return
        QtCore.QTimer.singleShot(50, wait_tree)
        return
    measure()


def measure():
    doc = state["doc"]
    try:
        # Nothing selected: the panels start from a clean state, so a row
        # count taken after the pick cannot be a leftover.
        FreeCADGui.Selection.clearSelection()
        pump()
        check("the property editor does not start on the object",
              MARKER not in property_labels(), property_labels())
        check("the tree starts unselected", OBJ not in tree_selected(), tree_selected())

        FreeCADGui.Selection.addSelection(doc.Name, OBJ)
        pump()
        check("Gui::Selection holds the object",
              len(FreeCADGui.Selection.getSelection(doc.Name)) == 1)
        check("the tree view heard it", OBJ in tree_selected(), tree_selected())
        check("the property editor heard it", MARKER in property_labels(), property_labels())

        FreeCADGui.Selection.clearSelection()
        pump()
        check("the tree view heard the clear", OBJ not in tree_selected(), tree_selected())
        check("the property editor heard the clear",
              MARKER not in property_labels(), property_labels())
    except Exception:
        note("ABORT measure:\n" + traceback.format_exc())
    finish()


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
