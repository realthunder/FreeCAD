"""A served scene with a real task panel open (docs/Sandbox.md 7.22, G7).

Pad's own C++ task dialog, up on a headless serve, so the browser tier's
panel card has something to mirror: a body with a rectangle sketch padded
10 mm, served to streaming viewers, and the Pad edited so `Control` holds
its dialog open.

Run it through scripts/renderer-serve.sh, which supplies the port in
FC_BGFX_SERVE_SCENE and points FC_BGFX_VIEWER_BUILD at build/wasm so the
one port carries the viewer page, the scene stream and the widget stream
alike:

    FC_SERVE_TOKEN=<secret> FC_SERVE_TRUST_PROXY=1 \\
      scripts/renderer-serve.sh scripts/demo-taskpanel.py 8077

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
    FreeCAD.Console.PrintMessage("taskpanel: %s\n" % msg)
    if _out:
        with open(_out, "a") as handle:
            handle.write(str(msg) + "\n")


def build(doc):
    """A body with a rectangle sketch padded 10 mm -- the shape the panel
    mirror's own gate uses, so what the browser draws is a panel we have
    already proven the host side of."""
    import TestSketcherApp

    body = doc.addObject("PartDesign::Body", "Body")
    sketch = doc.addObject("Sketcher::SketchObject", "SketchPad")
    sketch.Support = (doc.XY_Plane, [""])
    sketch.MapMode = "FlatFace"
    body.addObject(sketch)
    TestSketcherApp.CreateRectangleSketch(sketch, (0, 0), (20, 10))
    doc.recompute()
    pad = doc.addObject("PartDesign::Pad", "Pad")
    pad.Profile = sketch
    pad.Length = 10
    body.addObject(pad)
    doc.recompute()
    return body, pad


def open_panel(doc_name, pad_name):
    """Raise Pad's task dialog.

    Deferred rather than called inline: a startup script runs before the
    event loop has spun, and `setEdit` wants a live loop behind it -- called
    too early it returns False and leaves no panel to mirror.
    """
    try:
        gui_doc = FreeCADGui.getDocument(doc_name)
        pad = FreeCAD.getDocument(doc_name).getObject(pad_name)
        FreeCADGui.activateWorkbench("PartDesignWorkbench")
        opened = gui_doc.setEdit(pad, 0)
        note("setEdit -> %s; dialog up: %s"
             % (opened, bool(FreeCADGui.Control.activeDialog())))
    except Exception:
        note("opening the panel failed:\n" + traceback.format_exc())


try:
    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)  # the renderer (bgfx) path, which streams
    view.SetBool("ShowNaviCube", True)

    doc = FreeCAD.newDocument("TaskPanel")
    body, pad = build(doc)
    note("built %s with %d faces" % (pad.Name, len(pad.Shape.Faces)))

    # The port renderer-serve.sh was given. serveDocument is what puts this
    # document on the map as `?doc=TaskPanel`; a headless serve registers no
    # group by itself (docs/ShareAccess.md 5.2).
    port = int(os.environ.get("FC_BGFX_SERVE_SCENE", "8077"))
    served = FreeCADGui.serveDocument(doc, port)
    note("serveDocument(%s, %d) -> %s" % (doc.Name, port, served))

    from PySide import QtCore

    QtCore.QTimer.singleShot(1500, lambda: open_panel(doc.Name, pad.Name))
    note("panel scheduled; viewer page at /fcviewer.html?doc=%s" % doc.Name)
except Exception:
    note("scene failed:\n" + traceback.format_exc())
