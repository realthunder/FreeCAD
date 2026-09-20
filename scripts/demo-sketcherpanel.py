"""A served scene with Sketcher's edit panel open (docs/Sandbox.md 7.22, W2).

The companion to demo-taskpanel.py, and it exists because Pad's panel cannot
exercise W2: its Profile list is flat, unchecked and single-column. Sketcher's
edit panel carries both shapes the item views need --

  * Elements, a `SketcherGui::ElementView` (QTreeWidgetModel): five columns, so
    a real header, and rows that expand into their sub-elements;
  * Constraints, a `SketcherGui::ConstraintView` (QListWidgetModel): a check on
    every row, which is the write path (a check there moves the constraint into
    virtual space, which is what Mod/Test/SandboxPanelMirror.py
    `test_sketcher_constraints` asserts on the host side).

Run it through scripts/renderer-serve.sh, which supplies the port in
FC_BGFX_SERVE_SCENE and points FC_BGFX_VIEWER_BUILD at build/wasm so the one
port carries the viewer page, the scene stream and the widget stream alike:

    FC_SERVE_TOKEN=<secret> FC_SERVE_TRUST_PROXY=1 \\
      scripts/renderer-serve.sh scripts/demo-sketcherpanel.py 8078

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


def note(msg):
    FreeCAD.Console.PrintMessage("sketcherpanel: %s\n" % msg)
    if _out:
        with open(_out, "a") as handle:
            handle.write(str(msg) + "\n")


def build(doc):
    """A rectangle sketch -- the same one the panel mirror's own gate uses,
    so what the browser draws is a panel whose host side is already proven.
    A rectangle brings its own constraints (the four coincidences and the
    horizontal/vertical pairs), which is what fills the constraint list."""
    import TestSketcherApp

    sketch = doc.addObject("Sketcher::SketchObject", "SketchC")
    TestSketcherApp.CreateRectangleSketch(sketch, (0, 0), (20, 10))
    doc.recompute()
    return sketch


def open_panel(doc_name, sketch_name):
    """Raise Sketcher's edit panel.

    Deferred rather than called inline, for the reason demo-taskpanel.py
    gives: a startup script runs before the event loop has spun, and
    `setEdit` wants a live loop behind it -- called too early it returns
    False and leaves no panel to mirror.
    """
    try:
        gui_doc = FreeCADGui.getDocument(doc_name)
        sketch = FreeCAD.getDocument(doc_name).getObject(sketch_name)
        FreeCADGui.activateWorkbench("SketcherWorkbench")
        opened = gui_doc.setEdit(sketch, 0)
        note("setEdit -> %s; dialog up: %s; %d constraints"
             % (opened, bool(FreeCADGui.Control.activeDialog()),
                len(sketch.Constraints)))
    except Exception:
        note("opening the panel failed:\n" + traceback.format_exc())


try:
    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)  # the renderer (bgfx) path, which streams
    view.SetBool("ShowNaviCube", True)

    doc = FreeCAD.newDocument("SketcherPanel")
    sketch = build(doc)
    note("built %s with %d constraints" % (sketch.Name, len(sketch.Constraints)))

    # The port renderer-serve.sh was given. serveDocument is what puts this
    # document on the map as `?doc=SketcherPanel`; a headless serve registers
    # no group by itself (docs/ShareAccess.md 5.2).
    port = int(os.environ.get("FC_BGFX_SERVE_SCENE", "8078"))
    served = FreeCADGui.serveDocument(doc, port)
    note("serveDocument(%s, %d) -> %s" % (doc.Name, port, served))

    from PySide import QtCore

    QtCore.QTimer.singleShot(1500, lambda: open_panel(doc.Name, sketch.Name))
    note("panel scheduled; viewer page at /fcviewer.html?doc=%s" % doc.Name)
except Exception:
    note("scene failed:\n" + traceback.format_exc())
