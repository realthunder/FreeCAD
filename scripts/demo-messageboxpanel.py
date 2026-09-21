"""A served scene whose task panel raises a QMessageBox (docs/Sandbox.md 7.22, W4).

The companion to demo-taskpanel.py and demo-sketcherpanel.py, and it exists
because neither of those can exercise W4. A form panel and an item view are
both content INSIDE the task panel root; a dialog is a root of its OWN --
`dialog:<n>` beside `panel:<n>` in the mirror's list (7.19 M3) -- and nothing
in the other two scenes ever opens one.

Clicking "Ask" runs a slot that blocks in `QMessageBox::exec()`. While it is
blocked the mirror walks the box on the tick after its Show and sends it as a
second root; a client answers by naming the standard button THROUGH the root,
which the host turns into the window's `done(button)`, so the slot's `exec()`
returns the browser's answer. The label under the button then reports what
came back -- which is the point of this scene: it makes the whole round trip
readable on screen instead of only in the log, the way demo-picturepanel.py
counts its clicks.

The panel is the same shape as Mod/Test/SandboxPanelMirror.py
`test_nested_messagebox`, the host-side gate for M3, so what the browser
drives here is a panel whose host half is already proven.

Run it through scripts/renderer-serve.sh, which supplies the port in
FC_BGFX_SERVE_SCENE and points FC_BGFX_VIEWER_BUILD at build/wasm so the one
port carries the viewer page, the scene stream and the widget stream alike:

    FC_SERVE_TOKEN=<secret> FC_SERVE_TRUST_PROXY=1 \\
      scripts/renderer-serve.sh scripts/demo-messageboxpanel.py 8080

Nothing here starts the panel mirror: it starts with the first client that
subscribes with `panels` (7.19) and stops with the last, which is what the
browser card does when it is opened.

Runs persistently (no auto-close) so it can be viewed and streamed.
"""

import os
import traceback

import FreeCAD
import FreeCADGui

_out = os.environ.get("SMOKE_RESULT")

# The panel is held here for as long as the scene runs: Control.showDialog
# does not own it, and a panel collected while the browser is looking at it
# takes the mirror's root with it.
_panel = None


def note(msg):
    FreeCAD.Console.PrintMessage("messageboxpanel: %s\n" % msg)
    if _out:
        with open(_out, "a") as handle:
            handle.write(str(msg) + "\n")


def build(doc):
    """One solid, so the viewer has something to draw behind the card."""
    box = doc.addObject("Part::Box", "Box")
    box.Length = 20
    box.Width = 12
    box.Height = 8
    doc.recompute()
    return box


class AskPanel:
    """A bare Python task panel: a button that asks, and a label that reports
    what the box last returned."""

    def __init__(self):
        from PySide import QtWidgets

        self.form = QtWidgets.QWidget()
        self.form.setObjectName("askForm")
        self.form.setWindowTitle("Ask")
        layout = QtWidgets.QVBoxLayout(self.form)
        button = QtWidgets.QPushButton("Ask", self.form)
        button.setObjectName("askButton")
        button.clicked.connect(self.ask)
        self.answered = QtWidgets.QLabel("No answer yet", self.form)
        self.answered.setObjectName("answeredLabel")
        layout.addWidget(button)
        layout.addWidget(self.answered)

    def ask(self):
        """Blocks in exec() until someone answers -- at the desktop or in a
        browser. The mirror keeps serving the socket inside the nested loop,
        which is what lets a client see the box at all."""
        from PySide import QtWidgets

        box = QtWidgets.QMessageBox(
            QtWidgets.QMessageBox.Icon.Question,
            "Really",
            "Proceed?",
            QtWidgets.QMessageBox.StandardButton.Yes
            | QtWidgets.QMessageBox.StandardButton.No,
            FreeCADGui.getMainWindow(),
        )
        box.setObjectName("askBox")
        code = box.exec()
        # 0 is what Escape and a close give: QDialog::reject(), no button.
        names = {0x4000: "Yes", 0x10000: "No", 0: "nothing"}
        self.answered.setText("exec() returned %s (0x%x)" % (names.get(code, "?"), code))
        note("exec() -> 0x%x" % code)


def open_panel():
    """Raise the panel.

    Deferred rather than called inline, for the reason demo-taskpanel.py
    gives: a startup script runs before the event loop has spun, and the task
    view wants a live loop behind it.
    """
    global _panel
    try:
        _panel = AskPanel()
        FreeCADGui.Control.showDialog(_panel)
        note("panel up: %s" % bool(FreeCADGui.Control.activeDialog()))
    except Exception:
        note("opening the panel failed:\n" + traceback.format_exc())


try:
    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)  # the renderer (bgfx) path, which streams
    view.SetBool("ShowNaviCube", True)

    doc = FreeCAD.newDocument("MessageBoxPanel")
    solid = build(doc)
    note("built %s" % solid.Name)

    # The port renderer-serve.sh was given. serveDocument is what puts this
    # document on the map as `?doc=MessageBoxPanel`; a headless serve
    # registers no group by itself (docs/ShareAccess.md 5.2).
    port = int(os.environ.get("FC_BGFX_SERVE_SCENE", "8080"))
    served = FreeCADGui.serveDocument(doc, port)
    note("serveDocument(%s, %d) -> %s" % (doc.Name, port, served))

    from PySide import QtCore

    QtCore.QTimer.singleShot(1500, open_panel)
    note("panel scheduled; viewer page at /fcviewer.html?doc=%s" % doc.Name)
except Exception:
    note("scene failed:\n" + traceback.format_exc())
