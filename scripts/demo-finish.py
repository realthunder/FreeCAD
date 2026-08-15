"""Machined surface finish showcase (docs/ShapeAppearanceDesign.md 9).

One column per App::SurfaceFinish pattern -- knurled (diamond and
straight), brushed, blasted, turned. Each column carries the same
finish twice, authored two different ways and at two different sizes:

  * the CYLINDER states its finish through the four `Render_Finish*`
    dynamic view properties and lets the pitch default, so it shows
    the finish at the size that pattern really has on a part -- which
    at a whole-part camera distance is mostly below the pixel, and so
    is mostly shading as the roughness the filter converts it into.
    That is the honest picture, and the reason the pattern is stated
    in millimetres rather than as a normalised amplitude.

  * the PLATE states it on the appearance itself (App.Material's
    Finish/FinishPitch/FinishDepth, per-face capable storage), at an
    exaggerated pitch so the relief is actually resolved and the
    pattern geometry is visible in the picture.

Both routes end at the same SoFCRenderMaterial node; the appearance is
the authored one and wins where both are stated.

Usage: FreeCAD scripts/demo-finish.py            (GUI or xvfb)
Env:   FINISH_DOC   save path (default
                    data/examples/render/finish-showcase.FCStd)
       FINISH_SHOT  screenshot path (optional, saveRenderDump)
       FINISH_EXIT  "1" = exit after save/shot (for scripted runs)
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

# Must run before the first 3D view exists.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
    "RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
    "Type", "bgfx - OpenGL")
# Matcap shading paints one procedural studio material over the whole
# scene, which is exactly the material a finish is not: state the
# preference this scene depends on rather than inheriting it.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetBool(
    "Matcap", False)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC = os.environ.get("FINISH_DOC", os.path.join(
    REPO, "data", "examples", "render", "finish-showcase.FCStd"))
SHOT = os.environ.get("FINISH_SHOT", "")
EXIT = os.environ.get("FINISH_EXIT", "") == "1"

# name, exaggerated pitch (mm), exaggerated depth (mm), lay angle (deg)
PATTERNS = [
    ("knurl",          2.5, 0.75, 0.0),
    ("knurl-straight", 2.0, 0.60, 0.0),
    ("brushed",        1.2, 0.25, 90.0),
    ("blasted",        1.0, 0.18, 0.0),
    ("turned",         1.5, 0.25, 0.0),
]

STEP = 46.0         # column spacing
RADIUS = 11.0
HEIGHT = 34.0
PLATE = 34.0        # plate width and height
THICK = 6.0
STEEL = (0.62, 0.64, 0.67)


def pump(n=10):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()


def settle(rounds=25):
    for _ in range(rounds):
        time.sleep(0.2)
        pump(3)


def label(doc, name, text, x, z, size=15):
    ann = doc.addObject("App::Annotation", name)
    ann.LabelText = [text]
    ann.Position = FreeCAD.Vector(x, 0, z)
    ann.ViewObject.FontSize = size
    ann.ViewObject.TextColor = (0.88, 0.88, 0.88)
    return ann


def steel(vo, metallic, roughness):
    # A near-mirror steel (metallic 0.9, roughness 0.25) saturates flat
    # faces against the studio environment, and a saturated highlight
    # swallows the relief: every tilt the pattern applies still lands
    # on white. The plates are therefore a duller machined steel, which
    # is what lets the pattern read as shading rather than as a mirror.
    vo.ShapeColor = STEEL
    vo.addProperty("App::PropertyFloat", "Render_Metallic")
    vo.addProperty("App::PropertyFloat", "Render_Roughness")
    vo.Render_Metallic = metallic
    vo.Render_Roughness = roughness


def build():
    try:
        doc = FreeCAD.newDocument("FinishShowcase")

        for col, (name, pitch, depth, angle) in enumerate(PATTERNS):
            x = col * STEP

            # Top row: the finish through the Render_Finish* knobs, at
            # the pattern's own default size (pitch/depth left at 0).
            cyl = doc.addObject("Part::Cylinder", "Real_%d" % col)
            cyl.Radius = RADIUS
            cyl.Height = HEIGHT
            cyl.Placement.Base = FreeCAD.Vector(x, 0, 26.0)
            vo = cyl.ViewObject
            steel(vo, 0.85, 0.35)
            vo.addProperty("App::PropertyString", "Render_Finish")
            vo.Render_Finish = name

            # Bottom row: the finish on the appearance, exaggerated so
            # the relief resolves. A plate standing in the XZ plane, so
            # a front view sees the pattern face on.
            box = doc.addObject("Part::Box", "Shown_%d" % col)
            box.Length = PLATE
            box.Width = THICK
            box.Height = PLATE
            box.Placement.Base = FreeCAD.Vector(x - PLATE / 2.0,
                                                -THICK / 2.0, -50.0)
            vo = box.ViewObject
            steel(vo, 0.15, 0.50)
            mat = vo.ShapeAppearance[0]
            mat.Finish = name
            mat.FinishPitch = pitch
            mat.FinishDepth = depth
            mat.FinishAngle = angle
            vo.ShapeAppearance = mat

            label(doc, "Label_%d" % col, name, x - 14.0, -60.0)

        label(doc, "LabelReal", "as machined (default pitch)",
              -1.6 * STEP, 66.0, 17)
        label(doc, "LabelShown", "pattern exaggerated",
              -1.6 * STEP, -12.0, 17)

        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()

        # PBR with the image-based studio environment: a machined
        # finish is a specular effect, and it barely reads at all
        # under a single headlight.
        view.addProperty("App::PropertyBool", "Render_PBR")
        view.Render_PBR = True

        view.setCameraType("Perspective")
        view.viewFront()
        view.fitAll()
        # fitAll leaves generous margins; pull straight back from the
        # chart centre instead, keeping the front orientation.
        cx = (len(PATTERNS) - 1) * STEP / 2.0
        dist = 220.0
        cam = view.getCameraNode()
        cam.position.setValue(cx, -dist, 0.0)
        cam.focalDistance.setValue(dist)
        doc.recompute()
        settle()

        os.makedirs(os.path.dirname(DOC), exist_ok=True)
        doc.saveAs(DOC)
        FreeCAD.Console.PrintMessage("finish showcase saved: %s\n" % DOC)
        if SHOT:
            view.saveRenderDump(SHOT)
            FreeCAD.Console.PrintMessage("finish showcase shot: %s\n" % SHOT)
    except Exception:
        traceback.print_exc()
        FreeCAD.Console.PrintError("finish showcase FAILED\n")
    finally:
        if EXIT:
            os._exit(0)


QTimer.singleShot(1000, build)
