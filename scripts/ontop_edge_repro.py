"""In-FreeCAD repro + capture driver for the show-on-top hidden-edge
dimming harness (judge with scripts/ontop_edge_judge.py).

Scene: 10mm box + r2 h12 cylinder overlapping its origin corner; box
hidden and brought on top, Edge2 (top-left hexagon edge) selected. The
cylinder is REQUIRED: with every object invisible/on-top the scene feed
is empty, BGFXRenderer::canSkipInternal() turns false and the internal
GL renderer paints over the backend frame — such captures judge a
mixture, not the backend.

Captures <out>/ontop_bgfx_e2.png (current renderer type) and
<out>/ontop_gl_e2.png (Type temporarily switched to "Default"), main
window resized to 1600x837 so the judge's edge coordinates apply.
Output dir: FC_ONTOP_OUT env var, else the system temp dir.

Run it in a live GUI FreeCAD (e.g. over the MCP console, or pass it to
scripts/renderer-desktop.sh). The window is grabbed, not offscreen-
rendered — offscreen saveImage is empty under the bgfx backend.
"""

import os
import tempfile
import time

import FreeCAD
import FreeCADGui as Gui
from PySide6 import QtWidgets

OUT = os.environ.get("FC_ONTOP_OUT", tempfile.gettempdir())

pr = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
rtype = pr.GetString("Type", "")

doc = FreeCAD.newDocument("OnTopEdgeRepro")
box = doc.addObject("Part::Box", "Box")
box.Length = box.Width = box.Height = 10
cyl = doc.addObject("Part::Cylinder", "Cylinder")
cyl.Radius = 2
cyl.Height = 12
cyl.Placement.Base = FreeCAD.Vector(0, 0, -1)
doc.recompute()

v = Gui.getDocument(doc.Name).activeView()
Gui.getMainWindow().resize(1600, 837)


def settle(n=6):
    for _ in range(n):
        v.redraw()
        QtWidgets.QApplication.processEvents()
        time.sleep(0.08)


def grab(name):
    path = os.path.join(OUT, name + ".png")
    Gui.getMainWindow().grab().save(path)
    print("captured", path)


settle()
v.viewAxonometric()
v.fitAll()
settle()
box.ViewObject.Visibility = False
v.addObjectOnTop(box)
Gui.Selection.addSelection(doc.Name, box.Name, "Edge2")
settle()
grab("ontop_bgfx_e2")

pr.SetString("Type", "Default")
settle()
grab("ontop_gl_e2")
pr.SetString("Type", rtype)
settle()
print("done")
