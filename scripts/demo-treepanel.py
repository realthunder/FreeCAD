"""A served scene whose task panel carries a NESTED, COLOURED tree
(docs/Sandbox.md 7.22, W2's debts, driven in W5).

The fifth panel demo, and like the others it exists because none of the
earlier ones can exercise what it is for.  W2 built the item views with
nesting, the twisty and the expand op, and with `fg`/`bg` cells on the
wire -- and then proved none of it on screen, because the panel it was
driven against was Sketcher's constraint list, which never nests and
never colours a cell.  Neither does any other panel the other demos open.

So this panel is a real QTreeWidget with:

  * three levels, COLLAPSED as they arrive, so the card must draw a twisty
    and a click on it must reach the host -- the expand op, which no drive
    had ever pressed;
  * cells the panel colours to MEAN something, the way PartDesign's pick
    list paints an invalid feature red (`TaskFeaturePick.cpp`): a red
    foreground, a green one, and a yellow background with no foreground
    beside it -- that last one is the case the card has to decide for
    itself, because a light wash under the card's pale text would be
    unreadable;
  * two columns with a header, so the tracks the header and the rows share
    are visible on a tree rather than only on a list.

The label under the tree reports what the HOST saw: which row it expanded
or collapsed, and what is selected.  That is the point of the scene -- a
twisty that only opened in the page would look identical to one that
reached the desktop, and the whole of W2 turns on the difference (only an
item OP reaches the real widget; an event does not).

Run it through scripts/renderer-serve.sh:

    scripts/renderer-serve.sh scripts/demo-treepanel.py 8082

Nothing here starts the panel mirror: it starts with the first client that
subscribes with `panels` (7.19) and stops with the last.

Runs persistently (no auto-close) so it can be viewed and streamed.
"""

import os
import traceback

import FreeCAD
import FreeCADGui

_out = os.environ.get("SMOKE_RESULT")

# Qt objects the panel needs to outlive this script's locals.
_kept = []


def note(msg):
    FreeCAD.Console.PrintMessage("treepanel: %s\n" % msg)
    if _out:
        with open(_out, "a") as handle:
            handle.write(str(msg) + "\n")


def build_panel():
    """The form: a tree, and a label reporting what the host saw."""
    from PySide import QtGui, QtWidgets

    form = QtWidgets.QWidget()
    form.setObjectName("treeForm")
    form.setWindowTitle("Shape check")
    layout = QtWidgets.QVBoxLayout(form)

    tree = QtWidgets.QTreeWidget(form)
    tree.setObjectName("shapeTree")
    tree.setColumnCount(2)
    tree.setHeaderLabels(["Item", "Result"])

    def row(parent, name, result, fg=None, bg=None):
        item = QtWidgets.QTreeWidgetItem(parent)
        item.setText(0, name)
        item.setText(1, result)
        if fg is not None:
            item.setForeground(1, QtGui.QBrush(QtGui.QColor(fg)))
        if bg is not None:
            item.setBackground(1, QtGui.QBrush(QtGui.QColor(bg)))
        return item

    # Three levels. The colours are the ones a real panel uses for meaning:
    # red for a failure, green for a pass, and a yellow WASH with no
    # foreground of its own -- the case the card must resolve, because the
    # desktop's palette is light and the card is dark.
    solid = row(tree, "Solid", "ok", fg="#1e7d32")
    row(solid, "Shell 1", "ok", fg="#1e7d32")
    shell = row(tree, "Shell", "2 problems", fg="#c62828")
    face = row(shell, "Face 3", "open wire", bg="#ffe082")
    row(face, "Edge 7", "not on surface", bg="#ffe082")
    row(shell, "Face 9", "self intersect", fg="#c62828")
    row(tree, "Compound", "not checked")

    # Collapsed as they arrive: the twisty is the thing being proven, and a
    # tree that opens itself would prove nothing.
    tree.collapseAll()
    layout.addWidget(tree)

    seen = QtWidgets.QLabel("nothing expanded yet", form)
    seen.setObjectName("seenLabel")
    seen.setWordWrap(True)
    layout.addWidget(seen)

    # What the HOST saw. An expand that only happened in the page would
    # look the same on screen; these signals fire on the real tree, so the
    # label is the round trip rather than the page agreeing with itself.
    counts = {"expand": 0, "collapse": 0}

    def expanded(item):
        counts["expand"] += 1
        seen.setText(
            "host expanded %s (%d expands, %d collapses)"
            % (item.text(0), counts["expand"], counts["collapse"])
        )
        note("expanded %s" % item.text(0))

    def collapsed(item):
        counts["collapse"] += 1
        seen.setText(
            "host collapsed %s (%d expands, %d collapses)"
            % (item.text(0), counts["expand"], counts["collapse"])
        )
        note("collapsed %s" % item.text(0))

    def selected():
        picked = [i.text(0) for i in tree.selectedItems()]
        seen.setText("host selection: %s" % (", ".join(picked) or "none"))
        note("selection %s" % picked)

    tree.itemExpanded.connect(expanded)
    tree.itemCollapsed.connect(collapsed)
    tree.itemSelectionChanged.connect(selected)

    _kept.extend([form, tree, seen])
    return form


def open_panel():
    """Deferred, as the other demos defer theirs: a startup script runs
    before the event loop has spun, and the mirror wants a laid-out tree."""
    try:
        form = build_panel()
        if form is None:
            return

        class Panel:
            def __init__(self, form):
                self.form = form

        panel = Panel(form)
        _kept.append(panel)
        FreeCADGui.Control.showDialog(panel)
        note("panel up: %s" % bool(FreeCADGui.Control.activeDialog()))
    except Exception:
        note("opening the panel failed:\n" + traceback.format_exc())


try:
    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)  # the renderer (bgfx) path, which streams
    view.SetBool("ShowNaviCube", True)

    doc = FreeCAD.newDocument("TreePanel")
    box = doc.addObject("Part::Box", "Box")
    box.Length = 18
    box.Width = 10
    box.Height = 6
    doc.recompute()
    note("built %s" % box.Name)

    # The port renderer-serve.sh was given. serveDocument is what puts this
    # document on the map as `?doc=TreePanel`; a headless serve registers no
    # group by itself (docs/ShareAccess.md 5.2).
    port = int(os.environ.get("FC_BGFX_SERVE_SCENE", "8082"))
    served = FreeCADGui.serveDocument(doc, port)
    note("serveDocument(%s, %d) -> %s" % (doc.Name, port, served))

    from PySide import QtCore

    QtCore.QTimer.singleShot(1500, open_panel)
    note("panel scheduled; viewer page at /fcviewer.html?doc=%s" % doc.Name)
except Exception:
    note("scene failed:\n" + traceback.format_exc())
