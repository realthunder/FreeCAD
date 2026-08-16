"""A scene that exercises the SHARED INSTANCED tessellation
(_InstGeomTable in ViewProviderExt.cpp): one object whose shape is a
compound of many leaves that all share a single TShape, differing only
in location.

That is what ViewProviderPartExt::buildInstanced qualifies on -- a
COMPOUND with at least two leaves whose TShapes repeat -- and it is the
only way to reach the instanced code paths, which no ordinary model in
the test set happens to hit (the rack model reports instancing 0.000s
throughout). The shared subgraph is built ONCE per (TShape,
orientation, tessellation) and referenced by every instance, so this
scene is also the one that shows whether the shared build registers
worker vertex-cache content (docs/WorkerVertexCache.md).

Part.Shape.translated() moves the location and shares the TShape
(TopoShapePy::translate -> TopoDS_Shape::Move), which is exactly the
sharing the instance table keys on -- a copy() would defeat it.

What to look for in the serve log:

  visual build: ... instancing 0.0NNs
      non-zero means the instanced path actually ran.
  capture budget: ... N adopted of M offered
      the instanced line and point sets adopt. The instanced FACE set
      does not and should not: it sets forceTexCoords so a build under
      an untextured sharer still serves a textured one, which puts the
      cache outside the prebuilt contract ("texture unit").

Env knobs: COUNT (leaves, default 200), BUDGET (capture budget ms,
default 50), SPACING.

Runs persistently so it can be streamed.
"""
import os
import traceback

import FreeCAD
import FreeCADGui

_out = os.environ.get("SMOKE_RESULT")


def note(msg):
    if _out:
        with open(_out, "a") as f:
            f.write(str(msg) + "\n")


try:
    import Part
    from PySide6 import QtCore

    COUNT = int(os.environ.get("COUNT", "200"))
    BUDGET = int(os.environ.get("BUDGET", "50"))
    SPACING = float(os.environ.get("SPACING", "6"))

    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)
    view.SetBool("ShowNaviCube", False)
    view.SetBool("ShowFPS", False)

    render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    render.SetString("Type", "bgfx - OpenGL")
    render.SetInt("CaptureBudgetMS", BUDGET)
    render.SetBool("ShapeVertices", True)
    render.SetBool("LevelDebug", True)
    render.SetBool("AO", False)
    render.SetBool("Volumetric", False)
    render.SetBool("WaterSurface", False)
    render.SetBool("Bloom", False)
    render.SetBool("GroundReflection", False)
    render.SetBool("PBR", False)

    def build():
        doc = FreeCAD.newDocument("Instanced")
        # An open sweep, as in demo-incomplete: curved faces cost real
        # capture time, and the open ends give the leaf edges and
        # vertices as well as faces.
        base = Part.makeCylinder(2, 5, FreeCAD.Vector(0, 0, 0),
                                 FreeCAD.Vector(0, 0, 1), 300)
        side = int(COUNT ** 0.5) + 1
        leaves = [base.translated(FreeCAD.Vector((i % side) * SPACING,
                                                 (i // side) * SPACING, 0))
                  for i in range(COUNT)]
        obj = doc.addObject("Part::Feature", "Instanced")
        obj.Shape = Part.Compound(leaves)
        doc.recompute()
        note("BUILT compound of %d leaves sharing one TShape, budget %dms"
             % (COUNT, BUDGET))
        return doc

    doc = build()
    _state = {"n": 0, "timers": []}

    def touch():
        """Repaint the object so it rebuilds and republishes. The scene
        is static otherwise, and a publish only reports what CHANGED --
        without this the log stays empty after the first frame."""
        obj = doc.getObject("Instanced")
        r, g, b = obj.ViewObject.ShapeColor[:3]
        obj.ViewObject.ShapeColor = (g, b, r)
        _state["n"] += 1
        FreeCADGui.updateGui()
        # A headless serve does not necessarily paint on its own, and a
        # publish only happens inside a render -- so force one. Without
        # this the scene changes and the log stays empty, which reads
        # exactly like a publish that captured nothing.
        try:
            view = FreeCADGui.activeDocument().activeView()
            view.saveImage(os.path.join(os.environ.get("TMPDIR", "/tmp"),
                                        "demo-instanced-frame.png"),
                           320, 240, "Current")
        except Exception:
            note(traceback.format_exc())
        note("TOUCHED %d" % _state["n"])
        return _state["n"]

    FreeCAD.touch_all = touch

    def setup_view():
        try:
            FreeCADGui.activeDocument().activeView()
            FreeCADGui.SendMsgToActiveView("ViewFit")
            note("SETUP OK - serving %d instances" % COUNT)
            timer = QtCore.QTimer()
            timer.timeout.connect(touch)
            timer.start(5000)
            _state["timers"].append(timer)   # unheld timers are collected
        except Exception:
            note(traceback.format_exc())

    QtCore.QTimer.singleShot(2500, setup_view)
except Exception:
    note(traceback.format_exc())
